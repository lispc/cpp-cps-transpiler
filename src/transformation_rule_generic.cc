#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace cps {

using namespace clang;

bool GenericStackRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                               const GenContext &Ctx) const {
  if (Ctx.RetType == "void")
    return false;
  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
  if (holes.empty())
    return false;
  // GenericStackRule cannot handle recursive calls inside a hole's arguments
  // (nested recursion); leave those to DefunctionalizedRule.
  for (CallExpr *CE : holes) {
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (ContainsRecursiveCall(CE->getArg(i), Ctx.FuncName))
        return false;
    }
  }
  return true;
}

std::string GenericStackRule::apply(const FunctionDecl *FD,
                                    const BodyAnalysis &BA,
                                    GenContext &Ctx) const {
  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);

  bool needsAlgorithm = false;
  std::string combinedExpr;
  if (const CallExpr *CE = dyn_cast<CallExpr>(BA.RecExpr)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      std::string name = Callee->getNameAsString();
      if ((name == "min" || name == "max") && CE->getNumArgs() == 2) {
        needsAlgorithm = true;
        std::unordered_map<const Expr *, std::string> repls;
        for (size_t i = 0; i < holes.size(); ++i)
          repls[holes[i]] = "v" + std::to_string(i);
        std::string a0 =
            StripOuterParens(PrintExprWithReplacements(CE->getArg(0), repls,
                                                        Ctx.ASTCtx));
        std::string a1 =
            StripOuterParens(PrintExprWithReplacements(CE->getArg(1), repls,
                                                        Ctx.ASTCtx));
        combinedExpr = "std::" + name + "(" + a0 + ", " + a1 + ")";
      }
    }
  }
  if (combinedExpr.empty()) {
    std::unordered_map<const Expr *, std::string> repls;
    for (size_t i = 0; i < holes.size(); ++i)
      repls[holes[i]] = "v" + std::to_string(i);
    combinedExpr = StripOuterParens(
        PrintExprWithReplacements(BA.RecExpr, repls, Ctx.ASTCtx));
  }

  // Capture local variables declared in leading/middle statements that are
  // referenced by the combine expression. Leading-statement locals are
  // initialized once at function entry; middle-statement locals are computed
  // inside each frame branch and captured when pushing the combine marker.
  std::vector<const VarDecl *> allLocals;
  CollectLocalVarDecls(BA.LeadingStmts, allLocals);
  CollectLocalVarDecls(BA.MiddleStmts, allLocals);
  std::vector<const VarDecl *> leadingCaptured;
  std::vector<const VarDecl *> middleCaptured;
  for (const VarDecl *VD : allLocals) {
    if (IdentifierUsedInCode(combinedExpr, VD->getNameAsString())) {
      if (IsLocalFromStmts(VD, BA.LeadingStmts))
        leadingCaptured.push_back(VD);
      else
        middleCaptured.push_back(VD);
    }
  }
  std::vector<const VarDecl *> capturedLocals = leadingCaptured;
  capturedLocals.insert(capturedLocals.end(), middleCaptured.begin(),
                        middleCaptured.end());

  CodeEmitter e;
  e.raw("// === Generated generic-stack code for function: " + Ctx.FuncName +
        " ===\n\n");
  e.line("#include <vector>");
  if (needsAlgorithm)
    e.line("#include <algorithm>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  // Frame struct with parameters + captured locals.
  std::string frameName = Ctx.FuncName + "Frame";
  e.block("struct " + frameName, [&](CodeEmitter &b) {
    for (unsigned i = 0; i < FD->getNumParams(); ++i)
      b.line(GetParamStorageType(FD->getParamDecl(i)) + " " +
             FD->getParamDecl(i)->getNameAsString() + ";");
    for (const VarDecl *VD : capturedLocals)
      b.line(NormalizeTypeName(VD->getType().getAsString()) + " " +
             VD->getNameAsString() + ";");
    std::string ctor = frameName + "(";
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        ctor += ", ";
      ctor += GetParamStorageType(FD->getParamDecl(i)) + " " +
              FD->getParamDecl(i)->getNameAsString() + "_";
    }
    for (const VarDecl *VD : capturedLocals) {
      if (!ctor.empty() && ctor.back() != '(')
        ctor += ", ";
      ctor += NormalizeTypeName(VD->getType().getAsString()) + " " +
              VD->getNameAsString() + "_";
    }
    ctor += ")";
    std::string init;
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      std::string p = FD->getParamDecl(i)->getNameAsString();
      if (!init.empty())
        init += ", ";
      init += p + "(" + p + "_)";
    }
    for (const VarDecl *VD : capturedLocals) {
      std::string n = VD->getNameAsString();
      if (!init.empty())
        init += ", ";
      init += n + "(" + n + "_)";
    }
    if (!init.empty())
      ctor += " : " + init;
    ctor += " {}";
    b.line(ctor);
  }, ";");
  e.nl();

  std::string entryName = Ctx.FuncName + "StackEntry";
  e.block("struct " + entryName, [&](CodeEmitter &b) {
    b.line("enum class Tag { Frame, Marker } tag;");
    b.line(frameName + " frame;");
    b.line("int count;");
    b.line(entryName + "(" + frameName + " f) : tag(Tag::Frame), " +
           "frame(std::move(f)), count(0) {}");
    b.line(entryName + "(int c, " + frameName + " f) : tag(Tag::Marker), " +
           "frame(std::move(f)), count(c) {}");
  }, ";");
  e.nl();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("std::vector<" + entryName + "> stack;");
    b.line("std::vector<" + Ctx.RetType + "> values;");

    // Execute leading statements, then push the initial frame with captured
    // locals.
    EmitStmts(b, BA.LeadingStmts, Ctx.ASTCtx);
    {
      std::string init = "stack.emplace_back(" + frameName + "(";
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        if (i > 0)
          init += ", ";
        init += FD->getParamDecl(i)->getNameAsString();
      }
      for (const VarDecl *VD : leadingCaptured) {
        if (!init.empty() && init.back() != '(')
          init += ", ";
        init += VD->getNameAsString();
      }
      for (const VarDecl *VD : middleCaptured) {
        if (!init.empty() && init.back() != '(')
          init += ", ";
        init += "0";
      }
      init += "));";
      b.line(init);
    }

    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("auto entry = stack.back();");
      w.line("stack.pop_back();");
      w.block("if (entry.tag == " + entryName + "::Tag::Marker)",
              [&](CodeEmitter &iw) {
        iw.line("auto cur = entry.frame;");
        for (const auto &p : Ctx.ParamNames)
          iw.line("auto " + p + " = cur." + p + ";");
        for (const VarDecl *VD : capturedLocals)
          iw.line("auto " + VD->getNameAsString() + " = cur." +
                  VD->getNameAsString() + ";");
        for (size_t i = 0; i < holes.size(); ++i) {
          iw.line(Ctx.RetType + " v" + std::to_string(i) +
                  " = values.back();");
          iw.line("values.pop_back();");
        }
        iw.line("values.push_back(" + combinedExpr + ");");
      });
      w.block("else", [&](CodeEmitter &iw) {
        iw.line("auto cur = entry.frame;");
        for (const auto &p : Ctx.ParamNames)
          iw.line("auto " + p + " = cur." + p + ";");
        for (const VarDecl *VD : capturedLocals)
          iw.line("auto " + VD->getNameAsString() + " = cur." +
                  VD->getNameAsString() + ";");
        for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
          std::string prefix = (bi == 0) ? "if (" : "else if (";
          const auto &bc = BA.BaseCases[bi];
          iw.line(prefix + bc.CondStr + ")");
          iw.inc();
          iw.line("values.push_back(" + bc.ValueStr +
                  ");");
          iw.dec();
        }
        iw.line("else {");
        iw.inc();
        EmitStmts(iw, BA.MiddleStmts, Ctx.ASTCtx);
        {
          std::string marker = "stack.emplace_back(" + entryName + "(" +
                               std::to_string(holes.size()) + ", " +
                               frameName + "(";
          for (unsigned i = 0; i < FD->getNumParams(); ++i) {
            if (i > 0)
              marker += ", ";
            marker += FD->getParamDecl(i)->getNameAsString();
          }
          for (const VarDecl *VD : capturedLocals) {
            if (!marker.empty() && marker.back() != '(')
              marker += ", ";
            marker += VD->getNameAsString();
          }
          marker += ")));";
          iw.line(marker);
        }
        for (size_t i = 0; i < holes.size(); ++i) {
          std::string push = "stack.emplace_back(" + frameName + "(";
          for (unsigned a = 0;
               a < FD->getNumParams() && a < holes[i]->getNumArgs(); ++a) {
            if (a > 0)
              push += ", ";
            push += PrintExpr(holes[i]->getArg(a), Ctx.ASTCtx);
          }
          for (const VarDecl *VD : capturedLocals) {
            (void)VD;
            if (!push.empty() && push.back() != '(')
              push += ", ";
            push += "0";
          }
          push += "));";
          iw.line(push);
        }
        iw.dec();
        iw.line("}");
      });
    });
    b.line("return values.back();");
  });

  return e.str();
}

int GenericStackRule::cost() const { return 200; }

const char *GenericStackRule::name() const { return "GenericStackRule"; }

} // namespace cps
