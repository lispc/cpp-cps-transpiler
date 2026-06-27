#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace cps {

using namespace clang;

bool TuplingRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                          const GenContext &Ctx) const {
  if (Ctx.RetType == "void")
    return false;
  if (Ctx.ParamNames.empty())
    return false;

  // Switch-derived base cases have synthetic string conditions; tupling
  // needs AST expressions to evaluate them for n=0..k-1.
  for (const auto &bc : BA.BaseCases) {
    if (!bc.CondExpr)
      return false;
    if (!bc.ValueExpr || !IsPureExpr(bc.ValueExpr))
      return false;
  }

  std::vector<LinearTerm> terms;
  int maxOrder = 0;
  if (!ParseLinearTerms(BA.RecExpr, Ctx.FuncName, Ctx.ParamNames[0], terms,
                        maxOrder))
    return false;
  if (maxOrder < 2)
    return false;

  // Every order 1..maxOrder must appear exactly once.
  std::vector<int> orderCount(maxOrder + 1, 0);
  for (const auto &t : terms) {
    if (t.Order > maxOrder)
      return false;
    orderCount[t.Order]++;
  }
  for (int c = 1; c <= maxOrder; ++c) {
    if (orderCount[c] != 1)
      return false;
  }

  // Base cases must cover parameter values 0..maxOrder-1.
  for (int j = 0; j < maxOrder; ++j) {
    bool covered = false;
    for (const auto &bc : BA.BaseCases) {
      auto r = EvalConditionForParam(bc.CondExpr, Ctx.ParamNames[0], j);
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

std::string TuplingRule::apply(const FunctionDecl *FD, const BodyAnalysis &BA,
                               GenContext &Ctx) const {
  std::string pName = Ctx.ParamNames[0];
  std::string pType = GetParamStorageType(FD->getParamDecl(0));

  std::vector<LinearTerm> terms;
  int maxOrder = 0;
  ParseLinearTerms(BA.RecExpr, Ctx.FuncName, pName, terms, maxOrder);
  int k = maxOrder;

  std::unordered_map<const Expr *, std::string> repls;
  for (const auto &t : terms)
    repls[t.Hole] = "vals[" + std::to_string(k - t.Order) + "]";
  std::string nextExpr =
      StripOuterParens(PrintExprWithReplacements(BA.RecExpr, repls, Ctx.ASTCtx));

  CodeEmitter e;
  e.raw("// === Generated tupling code for function: " + Ctx.FuncName +
        " ===\n\n");
  e.line("#include <array>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  e.block(sig, [&](CodeEmitter &b) {
    // Early base-case return.
    b.block("if (" + pName + " <= " + std::to_string(k - 1) + ")",
            [&](CodeEmitter &iw) {
              for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
                std::string prefix = (bi == 0) ? "if (" : "else if (";
                iw.line(prefix + BA.BaseCases[bi].CondStr + ") return " +
                        BA.BaseCases[bi].ValueStr + ";");
              }
              iw.line("return 0;");
            });

    b.line("std::array<" + Ctx.RetType + ", " + std::to_string(k) + "> vals;");

    for (int j = 0; j < k; ++j) {
      for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
        if (EvalConditionForParam(BA.BaseCases[bi].CondExpr, pName, j) ==
            EvalResult::True) {
          std::string baseExpr =
              ReplaceParamWithLiteral(BA.BaseCases[bi].ValueStr, pName,
                                      std::to_string(j));
          b.line("vals[" + std::to_string(j) + "] = " + baseExpr + ";");
          break;
        }
      }
    }

    b.block("for (" + pType + " i = " + std::to_string(k) + "; i <= " +
                pName + "; ++i)",
            [&](CodeEmitter &fw) {
              fw.line(Ctx.RetType + " next = " + nextExpr + ";");
              for (int j = 0; j < k - 1; ++j)
                fw.line("vals[" + std::to_string(j) + "] = vals[" +
                        std::to_string(j + 1) + "];");
              fw.line("vals[" + std::to_string(k - 1) + "] = next;");
            });
    b.line("return vals[" + std::to_string(k - 1) + "];");
  });

  return e.str();
}

int TuplingRule::cost() const { return 30; }

const char *TuplingRule::name() const { return "TuplingRule"; }

} // namespace cps
