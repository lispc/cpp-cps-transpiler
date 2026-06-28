#include "cps_generator.h"
#include "code_emitter.h"
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

std::string Indent(const std::string &s, int n) {
  std::string prefix(n, ' ');
  std::string result;
  bool first = true;
  std::istringstream iss(s);
  std::string line;
  while (std::getline(iss, line)) {
    if (!first) result += "\n";
    result += prefix + line;
    first = false;
  }
  return result;
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

void EmitStmts(CodeEmitter &e, const std::vector<const Stmt *> &Stmts,
               const ASTContext *Ctx) {
  for (const Stmt *S : Stmts) {
    std::string line = PrintStmt(S, Ctx);
    // Clang's printPretty does not append a semicolon when printing an Expr
    // that happens to be used as a full statement. Add it manually.
    if (isa<Expr>(S) && !line.empty() && line.back() != ';')
      line += ';';
    e.line(line);
  }
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

void EmitUnpacksDefun(CodeEmitter &e, const std::string &ArgName,
                      const GenContext &Ctx) {
  for (const auto &p : Ctx.ParamNames) {
    e.line("auto " + p + " = " + ArgName + "." + p + ";");
  }
}

std::string EmitFrameStruct(CodeEmitter &e, const FunctionDecl *FD,
                            const GenContext &Ctx) {
  return EmitFrameStruct(e, FD, Ctx, {});
}

std::string EmitFrameStruct(CodeEmitter &e, const FunctionDecl *FD,
                            const GenContext &Ctx,
                            const std::vector<const VarDecl *> &ExtraFields) {
  std::string frameName = Ctx.FuncName + "Frame";
  e.block("struct " + frameName, [&](CodeEmitter &b) {
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      b.line(GetParamStorageType(FD->getParamDecl(i)) + " " +
             FD->getParamDecl(i)->getNameAsString() + ";");
    }
    for (const VarDecl *VD : ExtraFields)
      b.line(NormalizeTypeName(VD->getType().getAsString()) + " " +
             VD->getNameAsString() + ";");
    std::string ctor = frameName + "(";
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        ctor += ", ";
      ctor += GetParamStorageType(FD->getParamDecl(i)) + " " +
              FD->getParamDecl(i)->getNameAsString() + "_";
    }
    for (const VarDecl *VD : ExtraFields) {
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
    for (const VarDecl *VD : ExtraFields) {
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
  return frameName;
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
    Ctx.ParamNameSet.insert(pname);
  }

  bool isVoid = FD->getReturnType()->isVoidType();
  BodyAnalysis BA;
  bool bodyAnalyzed =
      AnalyzeBody(FD->getBody(), BA, Ctx.ASTCtx, Ctx.FuncName, isVoid);

  auto rules = CreateDefaultRules();
  const TransformationRule *bestRule = nullptr;
  for (const auto &rule : rules) {
    if (!Ctx.ForceRule.empty() &&
        std::string(rule->name()) != Ctx.ForceRule) {
      continue;
    }
    if (rule->applies(FD, BA, Ctx)) {
      if (!bestRule || rule->cost() < bestRule->cost())
        bestRule = rule.get();
    }
  }

  if (Ctx.ExplainSelection) {
    if (bestRule) {
      llvm::outs() << "[Rule selection] " << Ctx.FuncName << " -> "
                   << bestRule->name() << "\n";
    } else {
      llvm::outs() << "[Rule selection] " << Ctx.FuncName
                   << " -> no applicable rule\n";
    }
  }

  if (bestRule)
    return bestRule->apply(FD, BA, Ctx);

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
