#include "cps_generator.h"
#include "output_ir.h"
#include "transformation_rule.h"
#include "transformation_rules.h"
#include "transformation_rules_helpers.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;
using namespace llvm;

namespace cps {

// ============================================================
// Saved argument analysis
// ============================================================

bool NeedsSavedArg(
    const Expr *E, const std::vector<CallExpr *> &Holes,
    size_t HoleIdx,
    const std::unordered_set<const ValueDecl *> &ParamDecls) {
  if (ExprUsesParams(E, ParamDecls))
    return true;
  for (size_t i = HoleIdx + 1; i < Holes.size(); ++i) {
    for (unsigned a = 0; a < Holes[i]->getNumArgs(); ++a) {
      if (ExprUsesParams(Holes[i]->getArg(a), ParamDecls))
        return true;
    }
  }
  return false;
}

// ============================================================
// Code generation state helpers
// ============================================================

std::string NormalizeTypeName(const std::string &TypeStr) {
  // Clang prints C++ bool as "_Bool" in some contexts, which is not valid
  // C++ without <stdbool.h>. Normalize it to the proper C++ keyword.
  if (TypeStr == "_Bool")
    return "bool";
  return TypeStr;
}

std::string GetParamStorageType(const ParmVarDecl *PVD) {
  QualType T = PVD->getType();
  if (T->isReferenceType())
    T = T.getNonReferenceType();
  return NormalizeTypeName(T.getAsString());
}

std::string BuildFunctionSignature(const FunctionDecl *FD,
                                   const std::string &RetType) {
  std::string sig = RetType + " " + FD->getNameAsString() + "(";
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (i > 0)
      sig += ", ";
    sig += NormalizeTypeName(FD->getParamDecl(i)->getType().getAsString()) +
           " " + FD->getParamDecl(i)->getNameAsString();
  }
  sig += ")";
  return sig;
}

std::string ArgCtorDefun(const std::vector<std::string> &ParamValues,
                         const GenContext &Ctx) {
  std::string s = Ctx.ArgType + "(";
  for (size_t i = 0; i < ParamValues.size(); ++i) {
    if (i > 0) s += ", ";
    s += ParamValues[i];
  }
  s += ")";
  return s;
}

std::string ReplaceParamsWithCur(const Expr *E, const GenContext &Ctx) {
  std::unordered_map<const ValueDecl *, std::string> repls;
  for (const ParmVarDecl *P : Ctx.Params)
    repls[P] = "__cps_cur." + P->getNameAsString();
  return PrintExprWithDeclReplacements(E, repls, Ctx.ASTCtx);
}

// Legacy string-based fallback for synthetic conditions (e.g. switch-derived
// BaseCase::CondStr) that do not have an AST CondExpr.
std::string ReplaceParamsWithCurInString(
    const std::string &S, const std::vector<std::string> &Params) {
  std::string result = S;
  for (const auto &p : Params)
    result = ReplaceWholeWord(result, p, "__cps_cur." + p);
  return result;
}

std::string ReplaceParamWithLiteral(const Expr *E, const ParmVarDecl *Param,
                                    const std::string &Literal,
                                    const ASTContext *Ctx) {
  std::unordered_map<const ValueDecl *, std::string> repls;
  repls[Param] = Literal;
  return PrintExprWithDeclReplacements(E, repls, Ctx);
}

void EmitStmtsToIR(IRBuilder &builder, IRBlock *blk,
                   const std::vector<const Stmt *> &Stmts,
                   const ASTContext *Ctx) {
  for (const Stmt *S : Stmts) {
    std::string line = PrintStmt(S, Ctx);
    if (isa<clang::Expr>(S) && !line.empty() && line.back() != ';')
      line += ';';
    IRBuilder::add(blk, IRBuilder::rawStmt(line));
  }
}

void EmitUnpacksDefun(IRBlock *blk, const std::string &ArgName,
                      const GenContext &Ctx) {
  for (const auto &p : Ctx.ParamNames) {
    IRBuilder::add(blk, IRBuilder::var("auto", p,
                                       IRExpr(ArgName + "." + p)));
  }
}

// ============================================================
// Public API
// ============================================================

CpsResult GenerateCPS(const FunctionDecl *FD,
                      const std::string &ForceRule,
                      bool ExplainSelection) {
  if (!FD || !FD->hasBody())
    return MakeError(CpsErrorCode::InternalError, "invalid function decl");

  GenContext Ctx;
  Ctx.FuncName = FD->getNameAsString();
  Ctx.ArgType = Ctx.FuncName + "Arg";
  Ctx.ASTCtx = &FD->getASTContext();
  Ctx.RetType = NormalizeTypeName(FD->getReturnType().getAsString());
  Ctx.ForceRule = ForceRule;
  Ctx.ExplainSelection = ExplainSelection;

  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    const ParmVarDecl *PVD = FD->getParamDecl(i);
    Ctx.Params.push_back(PVD);
    Ctx.ParamDeclSet.insert(PVD);
    std::string pname = PVD->getNameAsString();
    Ctx.ParamNames.push_back(pname);
  }

  bool isVoid = FD->getReturnType()->isVoidType();
  BodyAnalysis BA;
  bool bodyAnalyzed =
      AnalyzeBody(FD->getBody(), BA, Ctx.ASTCtx, Ctx.FuncName, isVoid);

  auto rules = CreateDefaultRules();
  const RuleEntry *bestRule = nullptr;
  for (const auto &entry : rules) {
    if (!Ctx.ForceRule.empty() &&
        std::string(entry.Info->Name) != Ctx.ForceRule) {
      continue;
    }
    if (entry.Rule->applies(FD, BA, Ctx)) {
      if (!bestRule || entry.Info->Cost < bestRule->Info->Cost)
        bestRule = &entry;
    }
  }

  if (Ctx.ExplainSelection) {
    if (bestRule) {
      llvm::outs() << "[Rule selection] " << Ctx.FuncName << " -> "
                   << bestRule->Info->Name << "\n";
    } else {
      llvm::outs() << "[Rule selection] " << Ctx.FuncName
                   << " -> no applicable rule\n";
    }
  }

  if (bestRule)
    return bestRule->Rule->apply(FD, BA, Ctx);

  if (!bodyAnalyzed) {
    return MakeError(CpsErrorCode::UnsupportedBodyShape,
                     "function body not in supported shape "
                     "(expected: leading-stmts? (if-return)* return recursive;)",
                     Ctx.FuncName);
  }
  return MakeError(CpsErrorCode::NoApplicableRule,
                   "no applicable transformation rule", Ctx.FuncName);
}

} // namespace cps
