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

class CodeEmitter;

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
  std::vector<std::string> ParamNames;
  std::unordered_set<std::string> ParamNameSet;
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
bool ExprUsesParams(const clang::Expr *E,
                    const std::unordered_set<std::string> &ParamNames);

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
    const std::unordered_set<std::string> &ParamNames);

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

// Indent every line of a multi-line string by n spaces.
std::string Indent(const std::string &s, int n);

// Replace standalone parameter names in S with cur.<param>.
std::string ReplaceParamsWithCur(const std::string &S,
                                 const std::vector<std::string> &Params);

// Replace standalone occurrences of Param in S with Literal.
std::string ReplaceParamWithLiteral(const std::string &S,
                                    const std::string &Param,
                                    const std::string &Literal);

// Emit a list of statements as code lines.
void EmitStmts(CodeEmitter &e,
               const std::vector<const clang::Stmt *> &Stmts,
               const clang::ASTContext *Ctx);

// Emit parameter unpack statements (auto p = arg.p;).
void EmitUnpacksDefun(CodeEmitter &e, const std::string &ArgName,
                      const GenContext &Ctx);

// Emit a frame struct that stores all function parameters, with a constructor
// that initializes them. Returns the frame type name (FuncName + "Frame").
std::string EmitFrameStruct(CodeEmitter &e, const clang::FunctionDecl *FD,
                            const GenContext &Ctx);

// Emit a frame struct that stores function parameters plus a set of extra
// captured local variables. Returns the frame type name (FuncName + "Frame").
std::string EmitFrameStruct(CodeEmitter &e, const clang::FunctionDecl *FD,
                            const GenContext &Ctx,
                            const std::vector<const clang::VarDecl *> &ExtraFields);

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

  // Estimated runtime cost of the generated code. Lower is better.
  // Used by the rule engine to pick the best applicable rule.
  virtual int cost() const = 0;

  virtual const char *name() const = 0;
};

} // namespace cps

#endif // TRANSFORMATION_RULE_H
