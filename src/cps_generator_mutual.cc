#include "cps_generator.h"
#include "code_emitter.h"
#include "transformation_rules_helpers.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;

namespace cps {

namespace {

std::string CommonPrefix(const std::vector<std::string> &Names) {
  if (Names.empty())
    return "";
  std::string prefix = Names[0];
  for (size_t i = 1; i < Names.size(); ++i) {
    size_t len = 0;
    while (len < prefix.size() && len < Names[i].size() &&
           prefix[len] == Names[i][len])
      ++len;
    prefix.resize(len);
  }
  // Trim trailing underscore if present for nicer naming.
  if (!prefix.empty() && prefix.back() == '_')
    prefix.pop_back();
  return prefix;
}

void CollectGroupHoles(
    const Expr *E, const std::unordered_set<std::string> &GroupNames,
    std::vector<CallExpr *> &Holes) {
  if (!E)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (GroupNames.count(Callee->getNameAsString())) {
        Holes.push_back(const_cast<CallExpr *>(CE));
        return;
      }
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child))
      CollectGroupHoles(ChildExpr, GroupNames, Holes);
  }
}

bool ContainsGroupCall(
    const Expr *E, const std::unordered_set<std::string> &GroupNames) {
  if (!E)
    return false;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (GroupNames.count(Callee->getNameAsString()))
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ContainsGroupCall(dyn_cast_or_null<Expr>(Child), GroupNames))
      return true;
  }
  return false;
}

} // anonymous namespace

// Forward declaration.
CpsResult GenerateMutualGenericStackCPS(
    const std::vector<const FunctionDecl *> &FDs,
    const std::unordered_map<std::string, BodyAnalysis> &Analyses,
    const std::string &retType, const std::vector<std::string> &paramTypes,
    const std::vector<std::string> &paramNames);

CpsResult GenerateMutualCPS(
    const std::vector<const FunctionDecl *> &FDs) {
  if (FDs.empty())
    return MakeError(CpsErrorCode::InternalError, "empty mutual recursion group");

  const ASTContext *Ctx = &FDs[0]->getASTContext();

  // Require identical signatures (same return type and parameter types).
  std::string retType = NormalizeTypeName(FDs[0]->getReturnType().getAsString());
  std::vector<std::string> paramTypes;
  std::vector<std::string> paramNames;
  for (unsigned i = 0; i < FDs[0]->getNumParams(); ++i) {
    paramTypes.push_back(
        NormalizeTypeName(FDs[0]->getParamDecl(i)->getType().getAsString()));
    paramNames.push_back(FDs[0]->getParamDecl(i)->getNameAsString());
  }
  std::string groupName = FDs[0]->getNameAsString();

  for (size_t f = 1; f < FDs.size(); ++f) {
    if (NormalizeTypeName(FDs[f]->getReturnType().getAsString()) != retType)
      return MakeError(CpsErrorCode::UnsupportedBodyShape,
                       "mutual recursion group members must have identical "
                       "return types",
                       groupName);
    if (FDs[f]->getNumParams() != paramTypes.size())
      return MakeError(CpsErrorCode::UnsupportedBodyShape,
                       "mutual recursion group members must have identical "
                       "parameter counts",
                       groupName);
    for (unsigned i = 0; i < FDs[f]->getNumParams(); ++i) {
      if (NormalizeTypeName(FDs[f]->getParamDecl(i)->getType().getAsString()) !=
          paramTypes[i])
        return MakeError(CpsErrorCode::UnsupportedBodyShape,
                         "mutual recursion group members must have identical "
                         "parameter types",
                         groupName);
      if (FDs[f]->getParamDecl(i)->getNameAsString() != paramNames[i])
        return MakeError(CpsErrorCode::UnsupportedBodyShape,
                         "mutual recursion group members must have identical "
                         "parameter names",
                         groupName);
    }
  }

  // Analyze each function body.
  std::unordered_map<std::string, BodyAnalysis> Analyses;
  for (const FunctionDecl *FD : FDs) {
    BodyAnalysis BA;
    bool isVoid = FD->getReturnType()->isVoidType();
    if (!AnalyzeBody(FD->getBody(), BA, Ctx, FD->getNameAsString(), isVoid))
      return MakeError(CpsErrorCode::UnsupportedBodyShape,
                       "function body not in supported shape", groupName);
    Analyses[FD->getNameAsString()] = BA;
  }

  // Determine whether all mutual calls are tail calls. If not, fall back to
  // a generic-stack dispatcher.
  bool allTailCalls = true;
  for (const FunctionDecl *FD : FDs) {
    const BodyAnalysis &BA = Analyses[FD->getNameAsString()];
    const Expr *RecExpr = BA.RecExpr;
    const CallExpr *CE = dyn_cast<CallExpr>(RecExpr);
    if (!CE)
      allTailCalls = false;
    else if (!IsInTailPosition(CE, FD->getBody(), FD->getNameAsString()))
      allTailCalls = false;
    if (!allTailCalls)
      break;
  }

  if (!allTailCalls)
    return GenerateMutualGenericStackCPS(FDs, Analyses, retType, paramTypes,
                                         paramNames);

  CodeEmitter e;
  e.raw("// === Generated mutual-recursion code ===\n\n");

  // Enum tag.
  std::string enumName = FDs[0]->getNameAsString() + "MutualTag";
  e.raw("enum class " + enumName + " {\n");
  for (const FunctionDecl *FD : FDs)
    e.raw("  " + FD->getNameAsString() + ",\n");
  e.raw("};\n\n");

  // Dispatcher name: use common prefix if available, otherwise first function.
  std::vector<std::string> names;
  for (const FunctionDecl *FD : FDs)
    names.push_back(FD->getNameAsString());
  std::string prefix = CommonPrefix(names);
  std::string dispatcherName = prefix.empty() ? names[0] : prefix;
  dispatcherName += "_dispatch";

  // Dispatcher signature.
  std::string sig = retType + " " + dispatcherName +
                    "(" + enumName + " tag";
  for (size_t i = 0; i < paramNames.size(); ++i)
    sig += ", " + paramTypes[i] + " " + paramNames[i];
  sig += ")";

  e.block(sig, [&](CodeEmitter &b) {
    b.block("while (1)", [&](CodeEmitter &w) {
      w.block("switch (tag)", [&](CodeEmitter &sw) {
        for (const FunctionDecl *FD : FDs) {
          const BodyAnalysis &BA = Analyses[FD->getNameAsString()];
          sw.line("case " + enumName + "::" + FD->getNameAsString() + ": {");
          sw.inc();
          for (const auto &bc : BA.BaseCases) {
            sw.line("if (" + bc.CondStr + ") return " + bc.ValueStr + ";");
          }
          const CallExpr *CE = dyn_cast<CallExpr>(BA.RecExpr);
          std::string nextTag = CE->getDirectCallee()->getNameAsString();
          sw.line("tag = " + enumName + "::" + nextTag + ";");
          for (unsigned i = 0;
               i < FD->getNumParams() && i < CE->getNumArgs(); ++i) {
            sw.line("auto next_" + paramNames[i] + " = " +
                    PrintExpr(CE->getArg(i), Ctx) + ";");
          }
          for (unsigned i = 0;
               i < FD->getNumParams() && i < CE->getNumArgs(); ++i) {
            sw.line(paramNames[i] + " = next_" + paramNames[i] + ";");
          }
          sw.line("break;");
          sw.dec();
          sw.line("}");
        }
      });
    });
  });
  e.nl();

  // Wrapper functions.
  for (const FunctionDecl *FD : FDs) {
    std::string wrapperSig = retType + " " + FD->getNameAsString() + "(";
    for (size_t i = 0; i < paramNames.size(); ++i) {
      if (i > 0) wrapperSig += ", ";
      wrapperSig += paramTypes[i] + " " + paramNames[i];
    }
    wrapperSig += ")";
    e.block(wrapperSig, [&](CodeEmitter &b) {
      std::string call = "return " + dispatcherName +
                         "(" + enumName + "::" +
                         FD->getNameAsString();
      for (const auto &p : paramNames)
        call += ", " + p;
      call += ");";
      b.line(call);
    });
    e.nl();
  }

  return e.str();
}

CpsResult GenerateMutualGenericStackCPS(
    const std::vector<const FunctionDecl *> &FDs,
    const std::unordered_map<std::string, BodyAnalysis> &Analyses,
    const std::string &retType, const std::vector<std::string> &paramTypes,
    const std::vector<std::string> &paramNames) {
  const ASTContext *Ctx = &FDs[0]->getASTContext();
  std::string groupName = FDs[0]->getNameAsString();

  std::unordered_set<std::string> groupNames;
  for (const FunctionDecl *FD : FDs)
    groupNames.insert(FD->getNameAsString());

  // Precompute holes per function and combined expressions.
  std::unordered_map<std::string, std::vector<CallExpr *>> HolesByFunc;
  std::unordered_map<std::string, std::string> CombinedByFunc;
  for (const FunctionDecl *FD : FDs) {
    const std::string &name = FD->getNameAsString();
    const BodyAnalysis &BA = Analyses.at(name);
    std::vector<CallExpr *> holes;
    CollectGroupHoles(BA.RecExpr, groupNames, holes);
    if (holes.empty())
      return MakeError(CpsErrorCode::NoApplicableRule,
                       "mutual generic-stack rule found no recursive calls",
                       groupName);
    // Reject nested group calls inside hole arguments.
    for (CallExpr *CE : holes) {
      for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
        if (ContainsGroupCall(CE->getArg(i), groupNames))
          return MakeError(CpsErrorCode::UnsupportedBodyShape,
                           "nested mutual recursive calls are not supported",
                           groupName);
      }
    }
    HolesByFunc[name] = holes;
    std::unordered_map<const Expr *, std::string> repls;
    for (size_t i = 0; i < holes.size(); ++i)
      repls[holes[i]] = "v" + std::to_string(i);
    CombinedByFunc[name] =
        StripOuterParens(PrintExprWithReplacements(BA.RecExpr, repls, Ctx));
  }

  // Identify tail-call members: their recursive expression is exactly one
  // direct call to another group member. These can be dispatched without a
  // combine marker.
  std::unordered_map<std::string, bool> isTailByFunc;
  for (const FunctionDecl *FD : FDs) {
    const std::string &name = FD->getNameAsString();
    const auto &holes = HolesByFunc[name];
    isTailByFunc[name] =
        (holes.size() == 1 && holes[0] == Analyses.at(name).RecExpr);
  }

  CodeEmitter e;
  e.raw("// === Generated mutual-recursion code (generic stack) ===\n\n");
  e.line("#include <vector>");
  e.nl();

  // Function tag enum.
  std::string enumName = FDs[0]->getNameAsString() + "MutualTag";
  e.raw("enum class " + enumName + " {\n");
  for (const FunctionDecl *FD : FDs)
    e.raw("  " + FD->getNameAsString() + ",\n");
  e.raw("};\n\n");

  // Dispatcher name.
  std::vector<std::string> names;
  for (const FunctionDecl *FD : FDs)
    names.push_back(FD->getNameAsString());
  std::string prefix = CommonPrefix(names);
  std::string dispatcherName = prefix.empty() ? names[0] : prefix;
  dispatcherName += "_dispatch";

  // Frame struct.
  std::string frameName = dispatcherName + "Frame";
  e.block("struct " + frameName, [&](CodeEmitter &b) {
    b.line(enumName + " tag;");
    for (size_t i = 0; i < paramNames.size(); ++i)
      b.line(paramTypes[i] + " " + paramNames[i] + ";");
    std::string ctor = frameName + "(" + enumName + " t";
    for (size_t i = 0; i < paramNames.size(); ++i)
      ctor += ", " + paramTypes[i] + " " + paramNames[i];
    ctor += ") : tag(t)";
    for (const auto &p : paramNames)
      ctor += ", " + p + "(" + p + ")";
    ctor += " {}";
    b.line(ctor);
  }, ";");
  e.nl();

  // Stack entry struct.
  std::string entryName = dispatcherName + "StackEntry";
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

  // Dispatcher signature.
  std::string sig = retType + " " + dispatcherName + "(" + enumName + " tag";
  for (size_t i = 0; i < paramNames.size(); ++i)
    sig += ", " + paramTypes[i] + " " + paramNames[i];
  sig += ")";

  e.block(sig, [&](CodeEmitter &b) {
    b.line("std::vector<" + entryName + "> stack;");
    {
      std::string init = "stack.emplace_back(" + frameName + "(tag";
      for (const auto &p : paramNames)
        init += ", " + p;
      init += "));";
      b.line(init);
    }
    b.line("std::vector<" + retType + "> values;");

    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("auto entry = stack.back();");
      w.line("stack.pop_back();");
      w.block("if (entry.tag == " + entryName + "::Tag::Marker)",
              [&](CodeEmitter &iw) {
        iw.line("auto cur = entry.frame;");
        for (const auto &p : paramNames)
          iw.line("auto " + p + " = cur." + p + ";");
        if (paramNames.empty())
          iw.line("(void)cur;");
        iw.line(enumName + " ftag = cur.tag;");
        for (size_t i = 0; i < FDs.size(); ++i) {
          const std::string &name = FDs[i]->getNameAsString();
          iw.line((i == 0 ? "if (" : "else if (") +
                  std::string("ftag == ") + enumName + "::" + name + ") {");
          iw.inc();
          size_t hcount = HolesByFunc.at(name).size();
          for (size_t j = 0; j < hcount; ++j) {
            iw.line(retType + " v" + std::to_string(j) +
                    " = values.back(); values.pop_back();");
          }
          iw.line("values.push_back(" + CombinedByFunc.at(name) + ");");
          iw.dec();
          iw.line("}");
        }
      });
      w.block("else", [&](CodeEmitter &iw) {
        iw.line("auto cur = entry.frame;");
        for (const auto &p : paramNames)
          iw.line("auto " + p + " = cur." + p + ";");
        iw.line(enumName + " ftag = cur.tag;");
        for (size_t fi = 0; fi < FDs.size(); ++fi) {
          const FunctionDecl *FD = FDs[fi];
          const std::string &name = FD->getNameAsString();
          const BodyAnalysis &BA = Analyses.at(name);
          iw.line((fi == 0 ? "if (" : "else if (") +
                  std::string("ftag == ") + enumName + "::" + name + ") {");
          iw.inc();
          EmitStmts(iw, BA.LeadingStmts, Ctx);
          for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
            std::string prefix = (bi == 0) ? "if (" : "else if (";
            const auto &bc = BA.BaseCases[bi];
            iw.line(prefix + bc.CondStr + ")");
            iw.inc();
            iw.line("values.push_back(" + bc.ValueStr + ");");
            iw.dec();
          }
          iw.line("else {");
          iw.inc();
          EmitStmts(iw, BA.MiddleStmts, Ctx);
          const std::vector<CallExpr *> &holes = HolesByFunc.at(name);
          if (isTailByFunc[name]) {
            // Tail-call member: directly push the callee frame, no marker.
            const CallExpr *CE = holes[0];
            std::string push = "stack.emplace_back(" + frameName + "(" +
                               enumName + "::" +
                               CE->getDirectCallee()->getNameAsString();
            for (unsigned a = 0;
                 a < FD->getNumParams() && a < CE->getNumArgs(); ++a)
              push += ", " + PrintExpr(CE->getArg(a), Ctx);
            push += "));";
            iw.line(push);
          } else {
            iw.line("stack.emplace_back(" + entryName + "(" +
                    std::to_string(holes.size()) + ", cur));");
            for (size_t hi = 0; hi < holes.size(); ++hi) {
              std::string push = "stack.emplace_back(" + frameName + "(" +
                                 enumName + "::" +
                                 holes[hi]->getDirectCallee()->getNameAsString();
              for (unsigned a = 0;
                   a < FD->getNumParams() && a < holes[hi]->getNumArgs(); ++a)
                push += ", " + PrintExpr(holes[hi]->getArg(a), Ctx);
              push += "));";
              iw.line(push);
            }
          }
          iw.dec();
          iw.line("}");
          iw.dec();
          iw.line("}");
        }
      });
    });
    b.line("return values.back();");
  });
  e.nl();

  // Wrapper functions.
  for (const FunctionDecl *FD : FDs) {
    std::string wrapperSig = retType + " " + FD->getNameAsString() + "(";
    for (size_t i = 0; i < paramNames.size(); ++i) {
      if (i > 0) wrapperSig += ", ";
      wrapperSig += paramTypes[i] + " " + paramNames[i];
    }
    wrapperSig += ")";
    e.block(wrapperSig, [&](CodeEmitter &b) {
      std::string call = "return " + dispatcherName +
                         "(" + enumName + "::" + FD->getNameAsString();
      for (const auto &p : paramNames)
        call += ", " + p;
      call += ");";
      b.line(call);
    });
    e.nl();
  }

  return e.str();
}

} // namespace cps
