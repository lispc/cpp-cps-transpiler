#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

bool ExtractTwoRecursiveCalls(const Expr *LHS, const Expr *RHS,
                              const std::string &FuncName,
                              std::vector<std::string> &LeftCallArgs,
                              std::vector<std::string> &RightCallArgs,
                              const ASTContext *Ctx) {
  const CallExpr *LeftCall = dyn_cast<CallExpr>(LHS);
  const CallExpr *RightCall = dyn_cast<CallExpr>(RHS);
  if (!LeftCall || !RightCall)
    return false;

  const FunctionDecl *LeftCallee = LeftCall->getDirectCallee();
  const FunctionDecl *RightCallee = RightCall->getDirectCallee();
  if (!LeftCallee || !RightCallee)
    return false;
  if (LeftCallee->getNameAsString() != FuncName ||
      RightCallee->getNameAsString() != FuncName)
    return false;

  for (unsigned i = 0; i < LeftCall->getNumArgs(); ++i)
    LeftCallArgs.push_back(PrintExpr(LeftCall->getArg(i), Ctx));
  for (unsigned i = 0; i < RightCall->getNumArgs(); ++i)
    RightCallArgs.push_back(PrintExpr(RightCall->getArg(i), Ctx));
  return true;
}

} // anonymous namespace

bool BinaryStackRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                              const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  const Expr *E = BA.RecExpr->IgnoreParenImpCasts();
  const BinaryOperator *BO = dyn_cast<BinaryOperator>(E);
  if (!BO)
    return false;
  std::string op = BO->getOpcodeStr().str();
  if (op != "+" && op != "*" && op != "|" && op != "^")
    return false;

  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  std::vector<std::string> leftArgs, rightArgs;
  return ExtractTwoRecursiveCalls(LHS, RHS, Ctx.FuncName, leftArgs,
                                  rightArgs, Ctx.ASTCtx);
}

std::string BinaryStackRule::apply(const FunctionDecl *FD,
                                   const BodyAnalysis &BA,
                                   GenContext &Ctx) const {
  const BinaryOperator *BO =
      dyn_cast<BinaryOperator>(BA.RecExpr->IgnoreParenImpCasts());
  std::string op = BO->getOpcodeStr().str();

  const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  std::vector<std::string> leftArgs, rightArgs;
  ExtractTwoRecursiveCalls(LHS, RHS, Ctx.FuncName, leftArgs, rightArgs,
                           Ctx.ASTCtx);

  std::string identity;
  std::string combine;
  if (op == "+") {
    identity = "0";
    combine = "result += ";
  } else if (op == "*") {
    identity = "1";
    combine = "result *= ";
  } else if (op == "|") {
    identity = "0";
    combine = "result |= ";
  } else { // ^
    identity = "0";
    combine = "result ^= ";
  }

  CodeEmitter e;
  e.raw("// === Generated binary-stack code for function: " + Ctx.FuncName +
        " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string frameName = EmitFrameStruct(e, FD, Ctx);

  e.block(sig, [&](CodeEmitter &b) {
    b.line("std::vector<" + frameName + "> stack;");
    {
      std::string init = "stack.emplace_back(";
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        if (i > 0)
          init += ", ";
        init += FD->getParamDecl(i)->getNameAsString();
      }
      init += ");";
      b.line(init);
    }
    b.line(Ctx.RetType + " result = " + identity + ";");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("auto cur = stack.back();");
      w.line("stack.pop_back();");
      EmitStmts(w, BA.LeadingStmts, Ctx.ASTCtx);
      for (size_t bi = 0; bi < BA.BaseCases.size(); ++bi) {
        std::string prefix = (bi == 0) ? "if (" : "else if (";
        const auto &bc = BA.BaseCases[bi];
        w.line(prefix + ReplaceParamsWithCur(bc.CondStr,
                                             Ctx.ParamNames) +
               ") {");
        w.inc();
        w.line(combine +
               ReplaceParamsWithCur(bc.ValueStr,
                                    Ctx.ParamNames) +
               ";");
        w.dec();
        w.line("}");
      }
      w.line("else {");
      w.inc();
      EmitStmts(w, BA.MiddleStmts, Ctx.ASTCtx);
      {
        std::string push = "stack.emplace_back(";
        for (unsigned i = 0;
             i < FD->getNumParams() && i < rightArgs.size(); ++i) {
          if (i > 0)
            push += ", ";
          push += ReplaceParamsWithCur(rightArgs[i], Ctx.ParamNames);
        }
        push += ");";
        w.line(push);
      }
      {
        std::string push = "stack.emplace_back(";
        for (unsigned i = 0;
             i < FD->getNumParams() && i < leftArgs.size(); ++i) {
          if (i > 0)
            push += ", ";
          push += ReplaceParamsWithCur(leftArgs[i], Ctx.ParamNames);
        }
        push += ");";
        w.line(push);
      }
      w.dec();
      w.line("}");
    });
    b.line("return result;");
  });

  return e.str();
}

int BinaryStackRule::cost() const { return 100; }

const char *BinaryStackRule::name() const { return "BinaryStackRule"; }

} // namespace cps
