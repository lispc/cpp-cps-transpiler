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

namespace {

struct DefunClosureInfo {
  std::string Name;
  bool NeedsSavedArg = false;
};

} // anonymous namespace

bool DefunctionalizedRule::applies(const FunctionDecl *FD,
                                   const BodyAnalysis &BA,
                                   const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  if (!BA.IsRecursive)
    return false;
  // The final recursive expression must actually contain a recursive call.
  // AnalyzeBody may mark a function recursive even when the extracted RecExpr
  // is just a constant (e.g. helper functions with recursive if-returns).
  return ContainsRecursiveCall(BA.RecExpr, Ctx.FuncName);
}

CpsResult DefunctionalizedRule::apply(const FunctionDecl *FD,
                                        const BodyAnalysis &BA,
                                        GenContext &Ctx) const {
  const Expr *RecExpr = BA.RecExpr;

  CodeEmitter e;
  e.raw("// === Generated defunctionalized code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  // Arg struct.
  {
    std::string header = "struct " + Ctx.ArgType;
    e.block(header, [&](CodeEmitter &b) {
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        b.line(GetParamStorageType(FD->getParamDecl(i)) + " " +
               FD->getParamDecl(i)->getNameAsString() + ";");
      }
      std::string ctor = Ctx.ArgType + "(";
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        if (i > 0)
          ctor += ", ";
        ctor += GetParamStorageType(FD->getParamDecl(i)) + " " +
                FD->getParamDecl(i)->getNameAsString();
      }
      ctor += ") : ";
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        if (i > 0)
          ctor += ", ";
        std::string pName = FD->getParamDecl(i)->getNameAsString();
        ctor += pName + "(" + pName + ")";
      }
      ctor += " {}";
      b.line(ctor);
      {
        std::string dtor = Ctx.ArgType + "() : ";
        bool first = true;
        for (unsigned i = 0; i < FD->getNumParams(); ++i) {
          if (!first)
            dtor += ", ";
          std::string pName = FD->getParamDecl(i)->getNameAsString();
          dtor += pName + "(0)";
          first = false;
        }
        dtor += " {}";
        b.line(dtor);
      }
    }, ";");
  }
  e.nl();

  std::vector<CallExpr *> holes;
  CollectHolesDeep(RecExpr, Ctx.FuncName, holes);

  if (holes.size() == 1 && holes[0] == RecExpr->IgnoreParenImpCasts()) {
    e.block(sig, [&](CodeEmitter &b) {
      b.line(Ctx.ArgType + " arg = " +
             ArgCtorDefun(std::vector<std::string>(Ctx.ParamNames.size(), "0"),
                          Ctx) +
             ";");
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        b.line("arg." + Ctx.ParamNames[i] + " = " +
               FD->getParamDecl(i)->getNameAsString() + ";");
      }
      b.block("while (1)", [&](CodeEmitter &w) {
        EmitUnpacksDefun(w, "arg", Ctx);
        EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
        for (const auto &bc : BA.BaseCases) {
          w.line("if (" + bc.CondStr + ") return " +
                 bc.ValueStr + ";");
        }
        EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
        if (const CallExpr *RecCall = dyn_cast<CallExpr>(RecExpr)) {
          for (unsigned i = 0;
               i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
            std::string pName = FD->getParamDecl(i)->getNameAsString();
            w.line("arg." + pName + " = " +
                   PrintExpr(RecCall->getArg(i), Ctx.ASTCtx) + ";");
          }
        }
      });
    });
    return e.str();
  }

  std::vector<DefunClosureInfo> closures;
  for (size_t i = 0; i < holes.size(); ++i) {
    closures.push_back(
        {"K" + std::to_string(i),
         NeedsSavedArg(RecExpr, holes, i, Ctx.ParamNameSet)});
  }

  {
    e.raw("enum class " + Ctx.FuncName + "Cont {\n");
    e.raw("  Done,\n");
    for (const auto &c : closures)
      e.raw("  " + c.Name + ",\n");
    e.raw("};\n");
  }
  e.nl();

  {
    std::string frameName = Ctx.FuncName + "Frame";
    e.block("struct " + frameName, [&](CodeEmitter &b) {
      b.line(Ctx.FuncName + "Cont tag;");
      b.line("std::vector<" + Ctx.RetType + "> vals;");
      b.line("bool has_arg;");
      b.line(Ctx.ArgType + " saved_arg;");
      b.line(frameName + "(" + Ctx.FuncName +
             "Cont t) : tag(t), has_arg(false) {}");
      b.line(frameName + "(" + Ctx.FuncName +
             "Cont t, std::vector<" + Ctx.RetType +
             "> v) : tag(t), vals(std::move(v)), has_arg(false) {}");
      b.line(frameName + "(" + Ctx.FuncName +
             "Cont t, std::vector<" + Ctx.RetType + "> v, " + Ctx.ArgType +
             " a) : tag(t), vals(std::move(v)), has_arg(true), saved_arg(a) "
             "{}");
    }, ";");
  }
  e.nl();

  CodeEmitter casesEmitter;
  casesEmitter.line("case " + Ctx.FuncName + "Cont::Done:");
  casesEmitter.inc();
  casesEmitter.line("return val;");
  casesEmitter.dec();
  for (size_t i = 0; i < closures.size(); ++i) {
    const auto &cls = closures[i];
    casesEmitter.line("case " + Ctx.FuncName + "Cont::" + cls.Name + ": {");
    casesEmitter.inc();
    if (i == closures.size() - 1) {
      std::unordered_map<const Expr *, std::string> repls;
      for (size_t j = 0; j < holes.size(); ++j)
        repls[holes[j]] =
            (j == i) ? "val" : "f.vals[" + std::to_string(j) + "]";
      std::string finalExpr =
          StripOuterParens(PrintExprWithReplacements(RecExpr, repls, Ctx.ASTCtx));
      auto used = ParamsUsedInCode(finalExpr, Ctx.ParamNames);
      EmitTargetedUnpacks(casesEmitter, "f.saved_arg", used);
      // Avoid redundant "val = val;" when the recursive call itself is the
      // entire expression.
      if (finalExpr != "val")
        casesEmitter.line("val = " + finalExpr + ";");
      casesEmitter.line("break;");
    } else {
      std::vector<std::string> captured;
      for (size_t j = 0; j < i; ++j)
        captured.push_back("f.vals[" + std::to_string(j) + "]");
      captured.push_back("val");
      std::string push = "k.emplace_back(" + Ctx.FuncName + "Cont::K" +
                         std::to_string(i + 1) + ", ";
      push += "std::vector<" + Ctx.RetType + ">{";
      for (size_t j = 0; j < captured.size(); ++j) {
        if (j > 0)
          push += ", ";
        push += captured[j];
      }
      push += "}";
      if (closures[i + 1].NeedsSavedArg)
        push += ", f.saved_arg";
      push += ");";

      std::unordered_map<const Expr *, std::string> argRepls;
      for (size_t j = 0; j <= i; ++j)
        argRepls[holes[j]] =
            (j == i) ? "val" : "f.vals[" + std::to_string(j) + "]";
      std::vector<std::string> newParams;
      for (unsigned a = 0;
           a < FD->getNumParams() && a < holes[i + 1]->getNumArgs(); ++a)
        newParams.push_back(PrintExprWithReplacements(
            holes[i + 1]->getArg(a), argRepls, Ctx.ASTCtx));
      std::string argUpdate =
          "arg = " + ArgCtorDefun(newParams, Ctx) + ";";

      std::string codeToCheck = push + " " + argUpdate;
      auto used = ParamsUsedInCode(codeToCheck, Ctx.ParamNames);
      EmitTargetedUnpacks(casesEmitter, "f.saved_arg", used);

      casesEmitter.line(push);
      casesEmitter.line(argUpdate);
      casesEmitter.line("goto dispatch;");
    }
    casesEmitter.dec();
    casesEmitter.line("}");
  }

  std::string initialSetup;
  {
    std::string push = "k.emplace_back(" + Ctx.FuncName + "Cont::K0";
    if (closures[0].NeedsSavedArg)
      push += ", std::vector<" + Ctx.RetType + ">{}, arg";
    else
      push += ", std::vector<" + Ctx.RetType + ">{}";
    push += ");";
    initialSetup += push + "\n";
    std::vector<std::string> newParams;
    for (unsigned a = 0;
         a < FD->getNumParams() && a < holes[0]->getNumArgs(); ++a)
      newParams.push_back(PrintExpr(holes[0]->getArg(a), Ctx.ASTCtx));
    initialSetup += "arg = " + ArgCtorDefun(newParams, Ctx) + ";";
  }

  e.block(sig, [&](CodeEmitter &b) {
    b.line("std::vector<" + Ctx.FuncName + "Frame> k;");
    b.line("k.emplace_back(" + Ctx.FuncName + "Cont::Done);");
    b.line(Ctx.ArgType + " arg = " +
           ArgCtorDefun(std::vector<std::string>(Ctx.ParamNames.size(), "0"),
                        Ctx) +
           ";");
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      b.line("arg." + Ctx.ParamNames[i] + " = " +
             FD->getParamDecl(i)->getNameAsString() + ";");
    }
    b.line(Ctx.RetType + " val = 0;");
    b.line("dispatch:");
    b.block("while (1)", [&](CodeEmitter &w) {
      EmitUnpacksDefun(w, "arg", Ctx);
      EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
      for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
        std::string prefix = (bi == 0) ? "if (" : "else if (";
        const auto &bc = BA.BaseCases[bi];
        w.block(prefix + bc.CondStr + ")",
                [&](CodeEmitter &iw) {
                  iw.line("val = " + bc.ValueStr + ";");
                });
      }
      w.line("else {");
      w.inc();
      EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
      w.raw(Indent(initialSetup, 6));
      w.nl();
      w.line("goto dispatch;");
      w.dec();
      w.line("}");
      w.block("while (!k.empty())", [&](CodeEmitter &iw) {
        iw.line("auto f = k.back();");
        iw.line("k.pop_back();");
        iw.block("switch (f.tag)", [&](CodeEmitter &sw) {
          sw.raw(Indent(casesEmitter.str(), 6));
          sw.nl();
        });
      });
      w.line("return val;");
    });
  });

  return e.str();
}

int DefunctionalizedRule::cost() const {
  return RuleCatalog::Defunctionalized.Cost;
}

const char *DefunctionalizedRule::name() const {
  return RuleCatalog::Defunctionalized.Name;
}

} // namespace cps
