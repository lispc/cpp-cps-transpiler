#include "cps_generator.h"
#include "clang/AST/AST.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <string>

using namespace clang;
using namespace llvm;

namespace cps {

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

} // anonymous namespace

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

} // namespace cps
