// transformation_rules_helpers.h
// Shared helper declarations used by individual transformation rule files.

#ifndef TRANSFORMATION_RULES_HELPERS_H
#define TRANSFORMATION_RULES_HELPERS_H

#include "transformation_rule.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace cps {

// Tail-position detection.
bool IsInTailPosition(const clang::Expr *E, const clang::Stmt *S,
                      const std::string &FuncName);

// Local variable collection from statement lists.
void CollectLocalVarDecls(const clang::Stmt *S,
                          std::vector<const clang::VarDecl *> &Out);
void CollectLocalVarDecls(const std::vector<const clang::Stmt *> &Stmts,
                          std::vector<const clang::VarDecl *> &Out);
bool IsLocalFromStmts(const clang::VarDecl *VD,
                      const std::vector<const clang::Stmt *> &Stmts);
bool IdentifierUsedInCode(const std::string &Code,
                          const std::string &Name);

// Whole-word string utilities for identifier replacement/search.
bool ContainsWholeWord(const std::string &Code, const std::string &Word);
std::string ReplaceWholeWord(const std::string &S, const std::string &Old,
                             const std::string &New);

// Recursive call detection.
bool IsDirectRecursiveCall(const clang::Expr *E, const std::string &FuncName);
bool ContainsRecursiveCall(const clang::Expr *E, const std::string &FuncName);
void CollectHoles(const clang::Expr *E, const std::string &FuncName,
                  std::vector<clang::CallExpr *> &Holes);
void CollectHolesDeep(const clang::Expr *E, const std::string &FuncName,
                      std::vector<clang::CallExpr *> &Holes);

// Purity analysis.
bool IsKnownPureFunction(const std::string &Name);
bool IsPureExpr(const clang::Expr *E);
bool IsPureExprIgnoringRecursiveCalls(const clang::Expr *E,
                                      const std::string &FuncName);

// Parameter usage.
bool ExprUsesParams(const clang::Expr *E,
                    const std::unordered_set<std::string> &ParamNames);

// Argument shape checks.
bool IsParamMinusConst(const clang::Expr *E, const std::string &ParamName,
                       int &OutConst);
bool ContainsNonRecursiveCall(const clang::Expr *E,
                              const std::string &FuncName);

// Condition evaluation for base-case coverage checks.
enum class EvalResult { True, False, Unknown };
bool ExtractParamOrLiteral(const clang::Expr *E, const std::string &ParamName,
                           int ParamValue, int &Out);
EvalResult EvalConditionForParam(const clang::Expr *E,
                                 const std::string &ParamName,
                                 int ParamValue);

// Linear term parsing for TuplingRule/MemoizationRule.
struct LinearTerm {
  int Order;
  int Sign;
  clang::CallExpr *Hole;
};
bool ParseLinearTerms(const clang::Expr *E, const std::string &FuncName,
                      const std::string &ParamName,
                      std::vector<LinearTerm> &Terms, int &MaxOrder);

// Defunctionalized-rule helpers.
std::vector<std::string> ParamsUsedInCode(
    const std::string &Code,
    const std::vector<std::string> &ParamNames);
void EmitTargetedUnpacks(CodeEmitter &e, const std::string &ArgName,
                         const std::vector<std::string> &Params);

} // namespace cps

#endif // TRANSFORMATION_RULES_HELPERS_H
