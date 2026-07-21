#ifndef TRANSFORMATION_RULE_H
#define TRANSFORMATION_RULE_H

#include "cps_result.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cps {

// ============================================================
// Body analysis
// ============================================================

struct BaseCase {
  // Original AST expressions. May be null for synthetic base cases (e.g.,
  // derived from a switch statement).
  const clang::Expr *CondExpr = nullptr;
  const clang::Expr *ValueExpr = nullptr;

  // String representations, always valid for generation.
  std::string CondStr;
  std::string ValueStr;
};

struct BodyAnalysis {
  // Statements that appear before any base case.
  std::vector<const clang::Stmt *> LeadingStmts;

  // Base cases, checked in order.
  std::vector<BaseCase> BaseCases;

  // Statements that appear after the last base case but before the recursive
  // return. These are re-evaluated each iteration after base-case checks.
  std::vector<const clang::Stmt *> MiddleStmts;

  // The final recursive return expression (may contain multiple recursive
  // calls). Null if the function never returns recursively.
  const clang::Expr *RecExpr = nullptr;

  // True if the body ends with a recursive return.
  bool IsRecursive = false;
};

// Analyze a function body and extract base cases and recursive expression.
// Supported shape:
//   [leading-stmts]
//   (if-return)*
//   [middle-stmts]
//   return recursive-expr;
// A ternary expression "return cond ? base : rec;" is also normalized into
// a base case plus a recursive return.
bool AnalyzeBody(const clang::Stmt *Body, BodyAnalysis &BA,
                 const clang::ASTContext *Ctx,
                 const std::string &FuncName,
                 bool IsVoid);

// ============================================================
// Code generation state
// ============================================================

struct GenContext {
  std::string FuncName;
  std::string ArgType;
  std::string RetType;

  // Primary parameter identity.  Rules should prefer Params/ParamDeclSet over
  // ParamNames to avoid name-shadowing and overload issues.
  std::vector<const clang::ParmVarDecl *> Params;
  std::unordered_set<const clang::ValueDecl *> ParamDeclSet;

  // Convenience strings derived from Params; kept for frame-field naming and
  // diagnostics.
  std::vector<std::string> ParamNames;
  const clang::ASTContext *ASTCtx;

  // Optional user overrides.
  std::string ForceRule;  // If non-empty, only consider this rule.
  bool ExplainSelection = false;
};

// ============================================================
// Shared AST/codegen helpers
// ============================================================

std::string PrintExpr(const clang::Expr *E, const clang::ASTContext *Ctx);

// Print expression, replacing specific sub-expressions with variable names.
std::string PrintExprWithReplacements(
    const clang::Expr *E,
    const std::unordered_map<const clang::Expr *, std::string> &Repls,
    const clang::ASTContext *Ctx);

// Print expression, replacing references to specific ValueDecls with strings.
// This is the AST-level equivalent of whole-word string replacement and avoids
// name-shadowing / member-access / comment-substitution bugs.
std::string PrintExprWithDeclReplacements(
    const clang::Expr *E,
    const std::unordered_map<const clang::ValueDecl *, std::string> &DeclRepls,
    const clang::ASTContext *Ctx);

// Combined expression printer: replace specific sub-expressions first, then
// replace references to specific ValueDecls.  Used when a recursive expression
// has both holes (e.g. recursive calls) and parameters that need renaming.
std::string PrintExprWithReplacements(
    const clang::Expr *E,
    const std::unordered_map<const clang::Expr *, std::string> &ExprRepls,
    const std::unordered_map<const clang::ValueDecl *, std::string> &DeclRepls,
    const clang::ASTContext *Ctx);

std::string PrintStmt(const clang::Stmt *S, const clang::ASTContext *Ctx);

// Strip matching outer parentheses from a printed expression, e.g. "(x)" -> "x".
// Only removes parentheses that wrap the entire string without changing
// precedence.
std::string StripOuterParens(std::string s);

// Gather direct recursive calls inside E. Do not descend into arguments of a
// recursive call itself.
void CollectHoles(const clang::Expr *E, const std::string &FuncName,
                  std::vector<clang::CallExpr *> &Holes);

// Gather direct recursive calls inside E in post-order, including those nested
// in the arguments of another recursive call.
void CollectHolesDeep(const clang::Expr *E, const std::string &FuncName,
                      std::vector<clang::CallExpr *> &Holes);

// Returns true if E contains a direct recursive call.
bool ContainsRecursiveCall(const clang::Expr *E, const std::string &FuncName);

// Check whether an expression references any function parameters.
bool ExprUsesParams(
    const clang::Expr *E,
    const std::unordered_set<const clang::ValueDecl *> &ParamDecls);

// Check whether an expression contains a reference to a specific ValueDecl.
bool ExprContainsDeclRef(const clang::Expr *E,
                         const clang::ValueDecl *VD);

// Check whether E contains a reference to VD outside of any of the given
// "hole" sub-expressions.  Used when E will be rewritten by replacing holes
// with synthetic variables and we want to know which original locals are still
// referenced in the resulting expression.
bool ExprContainsDeclRefOutsideHoles(
    const clang::Expr *E, const clang::ValueDecl *VD,
    const std::vector<clang::CallExpr *> &Holes);

// Conservatively check whether an expression is side-effect free.
// Known-pure calls like min/max/std::min/std::max are whitelisted.
bool IsPureExpr(const clang::Expr *E);

// Same as IsPureExpr, but treats direct recursive calls to FuncName as pure.
// Useful when analyzing a recursive expression for transformations like
// memoization, where the recursive calls themselves will be replaced.
bool IsPureExprIgnoringRecursiveCalls(const clang::Expr *E,
                                      const std::string &FuncName);

// Does this expression (with given holes replaced) or any remaining hole
// arguments reference parameters?
bool NeedsSavedArg(
    const clang::Expr *E, const std::vector<clang::CallExpr *> &Holes,
    size_t HoleIdx,
    const std::unordered_set<const clang::ValueDecl *> &ParamDecls);

// Return the type to store in the Arg struct for a parameter.
// References are stored by their underlying value type.
std::string GetParamStorageType(const clang::ParmVarDecl *PVD);

// Normalize Clang-printed type names (e.g., "_Bool" -> "bool").
std::string NormalizeTypeName(const std::string &TypeStr);

// Build a function signature string "RetType name(T0 p0, T1 p1, ...)".
std::string BuildFunctionSignature(const clang::FunctionDecl *FD,
                                   const std::string &RetType);

// Arg constructor for defunctionalized backend.
std::string ArgCtorDefun(const std::vector<std::string> &ParamValues,
                         const GenContext &Ctx);

// Print an expression with every parameter reference rewritten as cur.<param>.
// AST-level replacement avoids name-shadowing and member-access collisions.
std::string ReplaceParamsWithCur(const clang::Expr *E, const GenContext &Ctx);

// String-based fallback for synthetic base-case fragments that have no AST.
std::string ReplaceParamsWithCurInString(
    const std::string &S, const std::vector<std::string> &ParamNames);

// Print an expression with every reference to Param replaced by Literal.
std::string ReplaceParamWithLiteral(const clang::Expr *E,
                                    const clang::ParmVarDecl *Param,
                                    const std::string &Literal,
                                    const clang::ASTContext *Ctx);

class IRBuilder;
class IRBlock;

// Emit a list of statements as raw IR statements inside the given block.
void EmitStmtsToIR(IRBuilder &builder, IRBlock *blk,
                   const std::vector<const clang::Stmt *> &Stmts,
                   const clang::ASTContext *Ctx);

// Emit parameter unpack statements (auto p = arg.p;).
void EmitUnpacksDefun(IRBlock *blk, const std::string &ArgName,
                      const GenContext &Ctx);

// ============================================================
// Rule interface
// ============================================================

class TransformationRule {
public:
  virtual ~TransformationRule() = default;

  // Return true if this rule can handle the given function body.
  virtual bool applies(const clang::FunctionDecl *FD,
                       const BodyAnalysis &BA,
                       const GenContext &Ctx) const = 0;

  // Generate iterative code for the function. Returns either the generated
  // source string or a structured error describing why generation failed.
  virtual CpsResult apply(const clang::FunctionDecl *FD,
                          const BodyAnalysis &BA,
                          GenContext &Ctx) const = 0;
};

} // namespace cps

#endif // TRANSFORMATION_RULE_H
