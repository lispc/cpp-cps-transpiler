#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace cps {

using namespace clang;

bool MemoizationRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                              const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  // Single-parameter functions only for now.
  if (Ctx.ParamNames.size() != 1)
    return false;

  // Require pure recursive expression (treating recursive calls as pure,
  // since they will be replaced by table lookups).
  if (!IsPureExprIgnoringRecursiveCalls(BA.RecExpr, Ctx.FuncName))
    return false;

  // Avoid pulling user-defined helper functions into the generated code;
  // leave expressions like min(f(n-1), f(n-2)) to GenericStackRule.
  if (ContainsNonRecursiveCall(BA.RecExpr, Ctx.FuncName))
    return false;

  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
  // Need overlapping subproblems; a single recursive call is better handled
  // by AccumulatorRule or TailRecursionRule.
  if (holes.size() < 2)
    return false;

  const std::string &pName = Ctx.ParamNames[0];
  int maxOffset = 0;
  for (CallExpr *CE : holes) {
    if (CE->getNumArgs() != 1)
      return false;
    int offset = 0;
    if (!IsParamMinusConst(CE->getArg(0), pName, offset))
      return false;
    maxOffset = std::max(maxOffset, offset);
  }

  // Base cases must cover parameter values 0..maxOffset-1.
  for (int i = 0; i < maxOffset; ++i) {
    bool covered = false;
    for (const auto &bc : BA.BaseCases) {
      if (!bc.CondExpr)
        return false; // switch-derived base cases not supported here
      auto r = EvalConditionForParam(bc.CondExpr, pName, i);
      if (r == EvalResult::True) {
        covered = true;
        break;
      }
      if (r == EvalResult::Unknown)
        return false;
    }
    if (!covered)
      return false;
  }

  return true;
}

CpsResult MemoizationRule::apply(const FunctionDecl *FD,
                                   const BodyAnalysis &BA,
                                   GenContext &Ctx) const {
  const std::string &pName = Ctx.ParamNames[0];
  std::string pType = GetParamStorageType(FD->getParamDecl(0));

  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);

  int maxOffset = 0;
  std::unordered_map<const Expr *, std::string> repls;
  for (CallExpr *CE : holes) {
    int offset = 0;
    IsParamMinusConst(CE->getArg(0), pName, offset);
    maxOffset = std::max(maxOffset, offset);
    repls[CE] = "dp[i - " + std::to_string(offset) + "]";
  }
  std::string loopExpr =
      StripOuterParens(PrintExprWithReplacements(BA.RecExpr, repls, Ctx.ASTCtx));

  CodeEmitter e;
  e.raw("// === Generated memoized code for function: " + Ctx.FuncName +
        " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  const ParmVarDecl *Param = FD->getParamDecl(0);

  e.block(sig, [&](CodeEmitter &b) {
    // Early base-case return for small inputs.
    b.block("if (" + pName + " <= " + std::to_string(maxOffset - 1) + ")",
            [&](CodeEmitter &iw) {
              for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
                std::string prefix = (bi == 0) ? "if (" : "else if (";
                const auto &bc = BA.BaseCases[bi];
                std::string condStr =
                    bc.CondExpr ? PrintExpr(bc.CondExpr, Ctx.ASTCtx) : bc.CondStr;
                std::string valStr =
                    bc.ValueExpr ? PrintExpr(bc.ValueExpr, Ctx.ASTCtx) : bc.ValueStr;
                iw.line(prefix + condStr + ") return " + valStr + ";");
              }
              iw.line("return 0;");
            });

    b.line("std::vector<" + Ctx.RetType + "> dp(" + pName + " + 1);");

    for (int i = 0; i < maxOffset; ++i) {
      for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
        if (EvalConditionForParam(BA.BaseCases[bi].CondExpr, pName, i) ==
            EvalResult::True) {
          std::string baseExpr = ReplaceParamWithLiteral(
              BA.BaseCases[bi].ValueExpr, Param, std::to_string(i),
              Ctx.ASTCtx);
          b.line("dp[" + std::to_string(i) + "] = " + baseExpr + ";");
          break;
        }
      }
    }

    b.block("for (int i = " + std::to_string(maxOffset) + "; i <= " +
                pName + "; ++i)",
            [&](CodeEmitter &fw) {
              fw.line("dp[i] = " + loopExpr + ";");
            });
    b.line("return dp[" + pName + "];");
  });

  return e.str();
}

int MemoizationRule::cost() const { return RuleCatalog::Memoization.Cost; }

const char *MemoizationRule::name() const {
  return RuleCatalog::Memoization.Name;
}

} // namespace cps
