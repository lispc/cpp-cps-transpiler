#include "cps_generator.h"
#include "clang/AST/AST.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <string>
#include <unordered_map>

using namespace clang;
using namespace llvm;

namespace cps {

// Forward declarations for helpers used inside the anonymous namespace.
std::string PrintExpr(const Expr *E, const ASTContext *Ctx);

namespace {

std::string Trim(const std::string &S) {
  size_t a = 0;
  while (a < S.size() && std::isspace(static_cast<unsigned char>(S[a])))
    ++a;
  size_t b = S.size();
  while (b > a && std::isspace(static_cast<unsigned char>(S[b - 1])))
    --b;
  return S.substr(a, b - a);
}

// Common implementation for expression printing with optional Expr-level and
// Decl-level replacements.
std::string PrintExprWithReplacementsImpl(
    const Expr *E,
    const std::unordered_map<const Expr *, std::string> *ExprRepls,
    const std::unordered_map<const ValueDecl *, std::string> *DeclRepls,
    const ASTContext *Ctx) {
  if (!E)
    return "";

  if (ExprRepls) {
    auto It = ExprRepls->find(E);
    if (It != ExprRepls->end())
      return It->second;
  }

  if (DeclRepls) {
    if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
      auto It = DeclRepls->find(DRE->getDecl());
      if (It != DeclRepls->end())
        return It->second;
    }
  }

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    return "(" +
           PrintExprWithReplacementsImpl(BO->getLHS(), ExprRepls, DeclRepls,
                                         Ctx) +
           " " + BO->getOpcodeStr().str() + " " +
           PrintExprWithReplacementsImpl(BO->getRHS(), ExprRepls, DeclRepls,
                                         Ctx) +
           ")";
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    std::string op = UO->getOpcodeStr(UO->getOpcode()).str();
    std::string sub = PrintExprWithReplacementsImpl(UO->getSubExpr(), ExprRepls,
                                                    DeclRepls, Ctx);
    if (!UO->isPostfix())
      return op + "(" + sub + ")";
    return "(" + sub + ")" + op;
  }

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    std::string s;
    if (const Expr *Callee = CE->getCallee()) {
      s += PrintExprWithReplacementsImpl(Callee, ExprRepls, DeclRepls, Ctx);
    }
    s += "(";
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (i > 0)
        s += ", ";
      s += PrintExprWithReplacementsImpl(CE->getArg(i), ExprRepls, DeclRepls,
                                         Ctx);
    }
    s += ")";
    return s;
  }

  if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    return "(" +
           PrintExprWithReplacementsImpl(CO->getCond(), ExprRepls, DeclRepls,
                                         Ctx) +
           " ? " +
           PrintExprWithReplacementsImpl(CO->getTrueExpr(), ExprRepls,
                                         DeclRepls, Ctx) +
           " : " +
           PrintExprWithReplacementsImpl(CO->getFalseExpr(), ExprRepls,
                                         DeclRepls, Ctx) +
           ")";
  }

  if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    return PrintExprWithReplacementsImpl(ASE->getBase(), ExprRepls, DeclRepls,
                                         Ctx) +
           "[" +
           PrintExprWithReplacementsImpl(ASE->getIdx(), ExprRepls, DeclRepls,
                                         Ctx) +
           "]";
  }

  if (const MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
    return PrintExprWithReplacementsImpl(ME->getBase(), ExprRepls, DeclRepls,
                                         Ctx) +
           (ME->isArrow() ? "->" : ".") +
           ME->getMemberNameInfo().getAsString();
  }

  if (const ParenExpr *PE = dyn_cast<ParenExpr>(E)) {
    return "(" +
           PrintExprWithReplacementsImpl(PE->getSubExpr(), ExprRepls, DeclRepls,
                                         Ctx) +
           ")";
  }

  if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    return PrintExprWithReplacementsImpl(ICE->getSubExpr(), ExprRepls,
                                         DeclRepls, Ctx);
  }

  if (const CStyleCastExpr *CCE = dyn_cast<CStyleCastExpr>(E)) {
    return "(" + CCE->getTypeAsWritten().getAsString() + ")" +
           "(" +
           PrintExprWithReplacementsImpl(CCE->getSubExpr(), ExprRepls,
                                         DeclRepls, Ctx) +
           ")";
  }

  return PrintExpr(E, Ctx);
}

} // namespace

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

std::string StripOuterParens(std::string s) {
  while (true) {
    s = Trim(s);
    if (s.size() < 2 || s.front() != '(' || s.back() != ')')
      break;
    int depth = 0;
    bool canStrip = true;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '(')
        ++depth;
      else if (s[i] == ')') {
        --depth;
        if (depth == 0 && i != s.size() - 1) {
          canStrip = false;
          break;
        }
      }
    }
    if (!canStrip)
      break;
    s = s.substr(1, s.size() - 2);
  }
  return s;
}

std::string PrintExprWithReplacements(
    const Expr *E,
    const std::unordered_map<const Expr *, std::string> &Repls,
    const ASTContext *Ctx) {
  return PrintExprWithReplacementsImpl(E, &Repls, nullptr, Ctx);
}

std::string PrintExprWithDeclReplacements(
    const Expr *E,
    const std::unordered_map<const ValueDecl *, std::string> &DeclRepls,
    const ASTContext *Ctx) {
  return PrintExprWithReplacementsImpl(E, nullptr, &DeclRepls, Ctx);
}

std::string PrintExprWithReplacements(
    const Expr *E,
    const std::unordered_map<const Expr *, std::string> &ExprRepls,
    const std::unordered_map<const ValueDecl *, std::string> &DeclRepls,
    const ASTContext *Ctx) {
  return PrintExprWithReplacementsImpl(E, &ExprRepls, &DeclRepls, Ctx);
}

} // namespace cps
