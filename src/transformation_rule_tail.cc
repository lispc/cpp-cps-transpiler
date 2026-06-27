#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <functional>
#include <string>
#include <vector>

namespace cps {

using namespace clang;

bool TailRecursionRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                                const GenContext &Ctx) const {
  std::vector<const CallExpr *> recCalls;
  std::function<void(const Stmt *)> collect = [&](const Stmt *S) {
    if (!S)
      return;
    if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
      if (const FunctionDecl *Callee = CE->getDirectCallee()) {
        if (Callee->getNameAsString() == Ctx.FuncName)
          recCalls.push_back(CE);
      }
    }
    for (const Stmt *Child : S->children())
      collect(Child);
  };
  collect(FD->getBody());
  if (recCalls.empty())
    return false;
  for (const CallExpr *CE : recCalls) {
    if (!IsInTailPosition(CE, FD->getBody(), Ctx.FuncName))
      return false;
  }
  return true;
}

std::string TailRecursionRule::apply(const FunctionDecl *FD,
                                     const BodyAnalysis &BA,
                                     GenContext &Ctx) const {
  CodeEmitter e;
  e.raw("// === Generated tail-recursion optimized code for function: " +
        Ctx.FuncName + " ===\n\n");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  e.block(sig, [&](CodeEmitter &b) {
    b.block("while (1)", [&](CodeEmitter &w) {
      EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
      for (const auto &bc : BA.BaseCases) {
        if (bc.ValueStr.empty())
          w.line("if (" + bc.CondStr + ") return;");
        else
          w.line("if (" + bc.CondStr + ") return " +
                 bc.ValueStr + ";");
      }
      EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
      if (const CallExpr *RecCall = dyn_cast<CallExpr>(BA.RecExpr)) {
        for (unsigned i = 0;
             i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
          std::string pName = FD->getParamDecl(i)->getNameAsString();
          w.line("auto next_" + pName + " = " +
                 PrintExpr(RecCall->getArg(i), Ctx.ASTCtx) + ";");
        }
        for (unsigned i = 0;
             i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
          std::string pName = FD->getParamDecl(i)->getNameAsString();
          w.line(pName + " = next_" + pName + ";");
        }
      }
    });
  });

  return e.str();
}

int TailRecursionRule::cost() const { return 10; }

const char *TailRecursionRule::name() const { return "TailRecursionRule"; }

} // namespace cps
