// transformation_rules_helpers.h
// Shared helper declarations used by individual transformation rule files.

#ifndef TRANSFORMATION_RULES_HELPERS_H
#define TRANSFORMATION_RULES_HELPERS_H

#include "code_emitter.h"
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
void CollectRecursiveCallsInStmt(const clang::Stmt *S,
                                 const std::string &FuncName,
                                 std::vector<clang::CallExpr *> &Calls);
const clang::IfStmt *FindRecursiveCallReturnIf(const clang::Stmt *S,
                                               const std::string &FuncName,
                                               clang::CallExpr *&OutCall);

// Collect direct recursive calls inside Root without descending into the
// arguments of a recursive call itself (top-level "holes" semantics).
void CollectDirectRecursiveCalls(const clang::Stmt *Root,
                                 const std::string &FuncName,
                                 std::vector<const clang::CallExpr *> &Out);

// Return true if any loop in Root contains a recursive call, unless the loop is
// a simple argument-iteration pattern (recursive call on getArg(i)).
bool HasForbiddenLoop(const clang::Stmt *Root, const std::string &FuncName,
                      const clang::ASTContext *Ctx);

// Return true if no recursive call has another recursive call nested in its
// arguments.
bool AllDirectRecursiveCallsNonNested(const clang::Stmt *Root,
                                      const std::string &FuncName);

// Loop helpers.
bool IsLoopStmt(const clang::Stmt *S);
const clang::Stmt *GetLoopBody(const clang::Stmt *S);

// Tree-traversal shape detection.
// Matches functions whose body consists of (optional) leading/base-case
// if-return statements, a single for-loop that contains exactly one direct
// recursive call used as the condition of an if-return, and a final return.
//
// For shapes like the original CollectHolesDeep, a leading if-block may
// contain recursion on arguments before a non-recursive post-order action.
// When such a block is detected, *OutPostLoopIf is set to the outer IfStmt
// and *OutPostLoopAction is set to the statement immediately before its
// innermost then-block's trailing return.
bool IsTreeTraversalShape(const clang::CompoundStmt *CS,
                          const std::string &FuncName,
                          const clang::Stmt *&OutLoop,
                          const clang::IfStmt *&OutRecIf,
                          clang::CallExpr *&OutRecCall,
                          bool IsVoid = false,
                          const clang::IfStmt **OutPostLoopIf = nullptr,
                          const clang::Stmt **OutPostLoopAction = nullptr,
                          bool *OutIsBoolAllAny = nullptr,
                          bool *OutIsAnd = nullptr,
                          bool *OutIsFindFirst = nullptr,
                          const clang::Expr **OutFindFirstReturnExpr = nullptr);

// Purity analysis.
bool IsKnownPureFunction(const std::string &Name);
bool IsPureExpr(const clang::Expr *E);
bool IsPureExprIgnoringRecursiveCalls(const clang::Expr *E,
                                      const std::string &FuncName);

// Parameter usage.
bool ExprUsesParams(
    const clang::Expr *E,
    const std::unordered_set<const clang::ValueDecl *> &ParamDecls);

// Check whether an expression contains a reference to a specific ValueDecl.
bool ExprContainsDeclRef(const clang::Expr *E,
                         const clang::ValueDecl *VD);

// Check whether E contains a reference to VD outside of any of the given holes.
bool ExprContainsDeclRefOutsideHoles(
    const clang::Expr *E, const clang::ValueDecl *VD,
    const std::vector<clang::CallExpr *> &Holes);

// Argument shape checks.
bool IsParamMinusConst(const clang::Expr *E, const std::string &ParamName,
                       int &OutConst);
bool ContainsNonRecursiveCall(const clang::Expr *E,
                              const std::string &FuncName);

// Type-string helpers (loose matching for shape detection).
std::string TypeString(const clang::ParmVarDecl *PVD);
bool TypeContains(const std::string &T, const std::string &Pattern);

// Return a default-initializer literal suitable for a placeholder of the given
// type.  Used when stack frames need a dummy value for locals that will be
// initialized later by the loop body.
std::string GetDefaultValueForType(const clang::QualType &QT);

// AST predicates: helpers that inspect Clang AST nodes directly instead of
// relying on PrintExpr() + string matching.

// Return the name of the callee for a call expression, or an empty string if
// the callee cannot be determined. Handles direct calls and C++ member calls.
std::string GetCalleeName(const clang::CallExpr *CE);

// Return true if E is a call whose callee name matches Name.
bool IsCallTo(const clang::Expr *E, const std::string &Name);

// Return true if any argument of CE is a call whose callee name matches Name.
bool AnyArgIsCallTo(const clang::CallExpr *CE, const std::string &Name);

// Return true if any argument of CE is a call whose callee name starts with
// Prefix.
bool AnyArgIsCallToPrefix(const clang::CallExpr *CE,
                          const std::string &Prefix);

// Return true if any argument of CE is a call whose callee name is one of the
// provided names.
bool AnyArgIsCallToOneOf(const clang::CallExpr *CE,
                         const std::vector<std::string> &Names);

// Recursively search E for a CallExpr whose callee name matches Name.
bool ContainsCallTo(const clang::Expr *E, const std::string &Name);

// Recursively search E for a CallExpr whose callee name starts with Prefix.
bool ContainsCallToPrefix(const clang::Expr *E, const std::string &Prefix);

// Recursively search E for a CallExpr whose callee name is one of Names.
bool ContainsCallToOneOf(const clang::Expr *E,
                         const std::vector<std::string> &Names);

// Recursively search E for a DeclRefExpr that refers to a value named Name.
bool ContainsDeclRefNamed(const clang::Expr *E, const std::string &Name);

// Return true if any argument of CE either is (or contains) a call to one of
// CallNames, or is (or contains) a DeclRefExpr named in DeclNames.
bool AnyArgMatches(const clang::CallExpr *CE,
                   const std::vector<std::string> &CallNames,
                   const std::vector<std::string> &DeclNames = {});

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

// ============================================================
// Shared code-generation helpers
// ============================================================

// Emit the "// === Generated <kind> code for function: <name> ===" banner.
void EmitGeneratedBanner(CodeEmitter &e, const std::string &Kind,
                         const std::string &FuncName);

// Emit one or more #include <...> lines followed by a blank line.
void EmitIncludes(CodeEmitter &e, const std::vector<std::string> &Headers);

// Emit the tail-recursion parameter update pattern:
//   auto next_p = <arg>;
//   p = next_p;
void EmitTailRecParamUpdate(CodeEmitter &e, const clang::FunctionDecl *FD,
                            const clang::CallExpr *RecCall,
                            const clang::ASTContext *Ctx);

} // namespace cps

#endif // TRANSFORMATION_RULES_HELPERS_H
