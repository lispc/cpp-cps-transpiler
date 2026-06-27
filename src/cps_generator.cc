#include "cps_generator.h"
#include "code_emitter.h"
#include "transformation_rule.h"
#include "transformation_rules.h"
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
// AST printing helpers
// ============================================================

std::string PrintExpr(const Expr *E, const ASTContext *Ctx) {
  std::string s;
  llvm::raw_string_ostream os(s);
  E->printPretty(os, nullptr, Ctx->getPrintingPolicy());
  os.flush();
  return s;
}

std::string PrintStmt(const Stmt *S, const ASTContext *Ctx) {
  std::string s;
  llvm::raw_string_ostream os(s);
  S->printPretty(os, nullptr, Ctx->getPrintingPolicy());
  os.flush();
  return s;
}

std::string PrintExprWithReplacements(
    const Expr *E,
    const std::unordered_map<const Expr *, std::string> &Repls,
    const ASTContext *Ctx) {
  if (!E)
    return "";

  auto It = Repls.find(E);
  if (It != Repls.end())
    return It->second;

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    return "(" + PrintExprWithReplacements(BO->getLHS(), Repls, Ctx) + " " +
           BO->getOpcodeStr().str() + " " +
           PrintExprWithReplacements(BO->getRHS(), Repls, Ctx) + ")";
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    std::string op = UO->getOpcodeStr(UO->getOpcode()).str();
    std::string sub = PrintExprWithReplacements(UO->getSubExpr(), Repls, Ctx);
    if (!UO->isPostfix())
      return op + "(" + sub + ")";
    return "(" + sub + ")" + op;
  }

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    std::string s;
    if (const Expr *Callee = CE->getCallee()) {
      s += PrintExprWithReplacements(Callee, Repls, Ctx);
    }
    s += "(";
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (i > 0)
        s += ", ";
      s += PrintExprWithReplacements(CE->getArg(i), Repls, Ctx);
    }
    s += ")";
    return s;
  }

  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    return "(" + PrintExprWithReplacements(CO->getCond(), Repls, Ctx) + " ? " +
           PrintExprWithReplacements(CO->getTrueExpr(), Repls, Ctx) + " : " +
           PrintExprWithReplacements(CO->getFalseExpr(), Repls, Ctx) + ")";
  }

  if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    return PrintExprWithReplacements(ASE->getBase(), Repls, Ctx) + "[" +
           PrintExprWithReplacements(ASE->getIdx(), Repls, Ctx) + "]";
  }

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
    return PrintExprWithReplacements(ME->getBase(), Repls, Ctx) +
           (ME->isArrow() ? "->" : ".") +
           ME->getMemberNameInfo().getAsString();
  }

  if (const ParenExpr *PE = dyn_cast<ParenExpr>(E)) {
    return "(" + PrintExprWithReplacements(PE->getSubExpr(), Repls, Ctx) + ")";
  }

  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    return PrintExprWithReplacements(ICE->getSubExpr(), Repls, Ctx);
  }

  if (const CStyleCastExpr *CCE = dyn_cast<CStyleCastExpr>(E)) {
    return "(" + CCE->getTypeAsWritten().getAsString() + ")" +
           "(" + PrintExprWithReplacements(CCE->getSubExpr(), Repls, Ctx) + ")";
  }

  return PrintExpr(E, Ctx);
}

// ============================================================
// Hole collection
// ============================================================

void CollectHoles(const Expr *E, const std::string &FuncName,
                  std::vector<CallExpr *> &Holes) {
  if (!E)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName) {
        Holes.push_back(const_cast<CallExpr *>(CE));
        return;
      }
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      CollectHoles(ChildExpr, FuncName, Holes);
    }
  }
}

bool ContainsRecursiveCall(const Expr *E, const std::string &FuncName) {
  if (!E)
    return false;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName)
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      if (ContainsRecursiveCall(ChildExpr, FuncName))
        return true;
    }
  }
  return false;
}

// ============================================================
// Parameter usage helpers
// ============================================================

bool ExprUsesParams(const Expr *E,
                    const std::unordered_set<std::string> &ParamNames) {
  if (!E)
    return false;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const ValueDecl *VD = DRE->getDecl()) {
      if (ParamNames.count(VD->getNameAsString()))
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ExprUsesParams(dyn_cast_or_null<Expr>(Child), ParamNames))
      return true;
  }
  return false;
}

bool NeedsSavedArg(
    const Expr *E, const std::vector<CallExpr *> &Holes,
    size_t HoleIdx,
    const std::unordered_set<std::string> &ParamNames) {
  if (ExprUsesParams(E, ParamNames))
    return true;
  for (size_t i = HoleIdx + 1; i < Holes.size(); ++i) {
    for (unsigned a = 0; a < Holes[i]->getNumArgs(); ++a) {
      if (ExprUsesParams(Holes[i]->getArg(a), ParamNames))
        return true;
    }
  }
  return false;
}

// ============================================================
// Code generation state helpers
// ============================================================

std::string GetParamStorageType(const ParmVarDecl *PVD) {
  QualType T = PVD->getType();
  if (T->isReferenceType())
    T = T.getNonReferenceType();
  return T.getAsString();
}

std::string BuildFunctionSignature(const FunctionDecl *FD,
                                   const std::string &RetType) {
  std::string sig = RetType + " " + FD->getNameAsString() + "(";
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (i > 0)
      sig += ", ";
    sig += FD->getParamDecl(i)->getType().getAsString() + " " +
           FD->getParamDecl(i)->getNameAsString();
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

std::string ReplaceParamsWithCur(const std::string &S,
                                 const std::vector<std::string> &Params) {
  std::string result = S;
  for (const auto &p : Params) {
    size_t pos = 0;
    while ((pos = result.find(p, pos)) != std::string::npos) {
      bool leftOK = (pos == 0) ||
                    (!std::isalnum(result[pos - 1]) && result[pos - 1] != '_');
      size_t end = pos + p.length();
      bool rightOK = (end == result.size()) ||
                     (!std::isalnum(result[end]) && result[end] != '_');
      if (leftOK && rightOK) {
        result.replace(pos, p.length(), "cur." + p);
        pos += 4 + p.length();
      } else {
        ++pos;
      }
    }
  }
  return result;
}

std::string ReplaceParamWithLiteral(const std::string &S,
                                    const std::string &Param,
                                    const std::string &Literal) {
  std::string result = S;
  size_t pos = 0;
  while ((pos = result.find(Param, pos)) != std::string::npos) {
    bool leftOK = (pos == 0) ||
                  (!std::isalnum(result[pos - 1]) && result[pos - 1] != '_');
    size_t end = pos + Param.length();
    bool rightOK = (end == result.size()) ||
                   (!std::isalnum(result[end]) && result[end] != '_');
    if (leftOK && rightOK) {
      result.replace(pos, Param.length(), Literal);
      pos += Literal.length();
    } else {
      ++pos;
    }
  }
  return result;
}

void EmitStmts(CodeEmitter &e, const std::vector<const Stmt *> &Stmts,
               const ASTContext *Ctx) {
  for (const Stmt *S : Stmts) {
    e.line(PrintStmt(S, Ctx));
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
  std::string frameName = Ctx.FuncName + "Frame";
  e.block("struct " + frameName, [&](CodeEmitter &b) {
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      b.line(GetParamStorageType(FD->getParamDecl(i)) + " " +
             FD->getParamDecl(i)->getNameAsString() + ";");
    }
    std::string ctor = frameName + "(";
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
  }, ";");
  e.nl();
  return frameName;
}

// ============================================================
// Body analysis
// ============================================================

namespace {

const Expr *ExtractReturnExpr(const Stmt *S) {
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S))
    return RS->getRetValue();
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->size() == 1)
      return ExtractReturnExpr(CS->body_begin()[0]);
  }
  return nullptr;
}

void FlattenIfElse(const Stmt *S, BodyAnalysis &BA) {
  if (!S)
    return;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    const Expr *BaseExpr = ExtractReturnExpr(IfS->getThen());
    if (BaseExpr)
      BA.BaseCases.emplace_back(IfS->getCond(), BaseExpr);
    if (const Stmt *Else = IfS->getElse())
      FlattenIfElse(Else, BA);
    return;
  }
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
    BA.RecExpr = RS->getRetValue();
    BA.IsRecursive = true;
  }
}

bool IsReturnOrIfReturn(const Stmt *S) {
  if (isa<ReturnStmt>(S))
    return true;
  if (isa<IfStmt>(S))
    return true;
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->size() == 1)
      return IsReturnOrIfReturn(CS->body_begin()[0]);
  }
  return false;
}

} // anonymous namespace

bool AnalyzeBody(const Stmt *Body, BodyAnalysis &BA) {
  BA = BodyAnalysis();
  const CompoundStmt *CS = dyn_cast<CompoundStmt>(Body);
  if (!CS)
    return false;

  size_t idx = 0;
  while (idx < CS->size()) {
    const Stmt *S = CS->body_begin()[idx];
    if (IsReturnOrIfReturn(S))
      break;
    BA.LeadingStmts.push_back(S);
    ++idx;
  }

  while (idx < CS->size()) {
    const Stmt *S = CS->body_begin()[idx];
    if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
      const Expr *BaseExpr = ExtractReturnExpr(IfS->getThen());
      if (!BaseExpr)
        return false;
      BA.BaseCases.emplace_back(IfS->getCond(), BaseExpr);
      if (const Stmt *Else = IfS->getElse()) {
        FlattenIfElse(Else, BA);
        ++idx;
        break;
      }
      ++idx;
      continue;
    }
    if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
      BA.RecExpr = RS->getRetValue();
      BA.IsRecursive = true;
      ++idx;
      break;
    }
    BA.MiddleStmts.push_back(S);
    ++idx;
  }

  if (idx != CS->size())
    return false;

  return !BA.BaseCases.empty() && BA.IsRecursive;
}

// ============================================================
// Public API
// ============================================================

std::string GenerateCPS(const FunctionDecl *FD) {
  if (!FD || !FD->hasBody())
    return "";

  GenContext Ctx;
  Ctx.FuncName = FD->getNameAsString();
  Ctx.ArgType = Ctx.FuncName + "Arg";
  Ctx.ASTCtx = &FD->getASTContext();
  Ctx.RetType = FD->getReturnType().getAsString();

  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    std::string pname = FD->getParamDecl(i)->getNameAsString();
    Ctx.ParamNames.push_back(pname);
    Ctx.ParamNameSet.insert(pname);
  }

  BodyAnalysis BA;
  if (!AnalyzeBody(FD->getBody(), BA)) {
    errs() << "[cps-transpiler] Function body not in supported shape "
              "(expected: leading-stmts? (if-return)* return recursive;)\n";
    return "";
  }

  auto rules = CreateDefaultRules();
  for (const auto &rule : rules) {
    if (rule->applies(FD, BA, Ctx)) {
      return rule->apply(FD, BA, Ctx);
    }
  }

  return "";
}

} // namespace cps
