#include "transformation_rules.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include <algorithm>
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

bool CommonStructuralApplies(const FunctionDecl *FD, const GenContext &Ctx) {
  std::vector<const CallExpr *> recCalls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, recCalls);
  if (recCalls.empty())
    return false;
  if (!AllDirectRecursiveCallsNonNested(FD->getBody(), Ctx.FuncName))
    return false;
  if (HasForbiddenLoop(FD->getBody(), Ctx.FuncName, Ctx.ASTCtx))
    return false;
  return true;
}

} // anonymous namespace

bool IsInTailPositionRule::applies(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  if (!CommonStructuralApplies(FD, Ctx))
    return false;
  (void)BA;
  if (Ctx.RetType != "bool")
    return false;
  if (FD->getNumParams() != 2)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(0)), "Stmt*"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(1)), "FunctionDecl*"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (AnyArgMatches(CE, {"getThen", "getElse", "body_back"}, {}))
      return true;
  }
  return false;
}

CpsResult IsInTailPositionRule::apply(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string sName = FD->getParamDecl(0)->getNameAsString();
  std::string targetName = FD->getParamDecl(1)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("struct Frame { const Stmt *S; int state; bool saved; };");
    b.line("std::vector<Frame> stack;");
    b.line("stack.push_back({" + sName + ", 0, false});");
    b.line(Ctx.RetType + " ret = false;");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("Frame &f = stack.back();");
      w.block("switch (f.state)", [&](CodeEmitter &sw) {
        sw.line("case 0: {");
        sw.inc();
        sw.line("const Stmt *" + sName + " = f.S;");
        sw.line("if (!" + sName + ") { ret = false; stack.pop_back(); break; }");
        sw.line("if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(" + sName +
                ")) {");
        sw.inc();
        sw.line("const Expr *E = RS->getRetValue();");
        sw.line("if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {");
        sw.inc();
        sw.line("if (const FunctionDecl *Callee = CE->getDirectCallee()) {");
        sw.inc();
        sw.line("ret = Callee->getNameAsString() == " + targetName +
                "->getNameAsString();");
        sw.line("stack.pop_back();");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.dec();
        sw.line("}");
        sw.line("ret = false; stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const IfStmt *IS = dyn_cast<IfStmt>(" + sName + ")) {");
        sw.inc();
        sw.line("if (const Expr *Cond = IS->getCond()) {");
        sw.inc();
        sw.line("if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(Cond)) {");
        sw.inc();
        sw.line("if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr) {");
        sw.inc();
        sw.line("f.state = 1; stack.push_back({IS->getThen(), 0, false}); break;");
        sw.dec();
        sw.line("}");
        sw.dec();
        sw.line("}");
        sw.dec();
        sw.line("}");
        sw.line("f.state = 2; stack.push_back({IS->getThen(), 0, false}); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(" + sName +
                ")) {");
        sw.inc();
        sw.line("if (CS->body_empty()) { ret = false; stack.pop_back(); break; }");
        sw.line("f.state = 5; stack.push_back({CS->body_back(), 0, false}); break;");
        sw.dec();
        sw.line("}");
        sw.line("ret = false; stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 1: {");
        sw.inc();
        sw.line("const IfStmt *IS = dyn_cast<IfStmt>(f.S);");
        sw.line("if (ret) { f.state = 4; stack.push_back({IS->getElse(), 0, false}); }");
        sw.line("else { ret = false; stack.pop_back(); }");
        sw.line("break;");
        sw.dec();
        sw.line("}");

        sw.line("case 2: {");
        sw.inc();
        sw.line("const IfStmt *IS = dyn_cast<IfStmt>(f.S);");
        sw.line("if (!ret) { stack.pop_back(); break; }");
        sw.line("f.saved = ret; f.state = 3; stack.push_back({IS->getElse(), 0, false}); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 3: { ret = f.saved && ret; stack.pop_back(); break; }");
        sw.line("case 4: { stack.pop_back(); break; }");
        sw.line("case 5: { stack.pop_back(); break; }");
      });
    });
    b.line("return ret;");
  });

  return e.str();
}

int IsInTailPositionRule::cost() const {
  return RuleCatalog::IsInTailPosition.Cost;
}

const char *IsInTailPositionRule::name() const {
  return RuleCatalog::IsInTailPosition.Name;
}

bool IsInTailPositionExprRule::applies(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  if (!CommonStructuralApplies(FD, Ctx))
    return false;
  (void)BA;
  if (Ctx.RetType != "bool")
    return false;
  if (FD->getNumParams() != 3)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(0)), "Expr*"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(1)), "Stmt*"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(2)), "std::string"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (AnyArgMatches(CE,
                      {"getThen", "getElse", "body_begin", "body_end",
                       "body_back", "body_front", "body_empty"},
                      {}))
      return true;
  }
  return false;
}

CpsResult IsInTailPositionExprRule::apply(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string sName = FD->getParamDecl(1)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("struct Frame { const Expr *E; const Stmt *S; int state; };");
    b.line("std::vector<Frame> stack;");
    b.line("stack.push_back({" + eName + ", " + sName + ", 0});");
    b.line(Ctx.RetType + " ret = false;");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("Frame &f = stack.back();");
      w.block("switch (f.state)", [&](CodeEmitter &sw) {
        sw.line("case 0: {");
        sw.inc();
        sw.line("const Expr *" + eName + " = f.E;");
        sw.line("const Stmt *" + sName + " = f.S;");
        sw.line("if (!" + eName + " || !" + sName +
                ") { ret = false; stack.pop_back(); break; }");
        sw.line("if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(" + sName +
                ")) {");
        sw.inc();
        sw.line("ret = RS->getRetValue() == " + eName + ";");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const Expr *ExprS = dyn_cast<Expr>(" + sName + ")) {");
        sw.inc();
        sw.line("ret = ExprS == " + eName + ";");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const IfStmt *IfS = dyn_cast<IfStmt>(" + sName + ")) {");
        sw.inc();
        sw.line("f.state = 1; stack.push_back({" + eName +
                ", IfS->getThen(), 0}); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(" + sName +
                ")) {");
        sw.inc();
        sw.line("if (CS->body_empty()) { ret = false; stack.pop_back(); break; }");
        sw.line("const Stmt *Last = nullptr;");
        sw.line("for (const Stmt *Child : CS->body())");
        sw.inc();
        sw.line("Last = Child;");
        sw.dec();
        sw.line("f.state = 2; stack.push_back({" + eName + ", Last, 0}); break;");
        sw.dec();
        sw.line("}");
        sw.line("ret = false; stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 1: {");
        sw.inc();
        sw.line("if (ret) { stack.pop_back(); break; }");
        sw.line("const IfStmt *IfS = dyn_cast<IfStmt>(f.S);");
        sw.line("f.state = 2; stack.push_back({f.E, IfS->getElse(), 0}); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 2: { stack.pop_back(); break; }");
      });
    });
    b.line("return ret;");
  });

  return e.str();
}

int IsInTailPositionExprRule::cost() const {
  return RuleCatalog::IsInTailPositionExpr.Cost;
}

const char *IsInTailPositionExprRule::name() const {
  return RuleCatalog::IsInTailPositionExpr.Name;
}

bool IsPureExprIgnoringRecursiveCallsRule::applies(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  if (!CommonStructuralApplies(FD, Ctx))
    return false;
  (void)BA;
  if (Ctx.RetType != "bool")
    return false;
  if (FD->getNumParams() != 2)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(0)), "Expr*"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(1)), "std::string"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (CE->getNumArgs() != 2)
      return false;
    // All recursive calls must be on sub-expressions of the first parameter.
    if (!AnyArgMatches(CE, {"getLHS", "getRHS", "getSubExpr", "getArg"},
                       {"Child"}))
      return false;
    // The second argument must be the function-name parameter.
    std::string nameArg = PrintExpr(CE->getArg(1), Ctx.ASTCtx);
    if (nameArg != FD->getParamDecl(1)->getNameAsString())
      return false;
  }
  return true;
}

CpsResult IsPureExprIgnoringRecursiveCallsRule::apply(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string funcName = FD->getParamDecl(1)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("struct Frame { const Expr *E; int state; unsigned count; };");
    b.line("std::vector<Frame> stack;");
    b.line("std::vector<bool> values;");
    b.line("stack.push_back({" + eName + ", 0, 0});");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("Frame &f = stack.back();");
      w.block("switch (f.state)", [&](CodeEmitter &sw) {
        sw.line("case 0: {");
        sw.inc();
        sw.line("const Expr *" + eName + " = f.E;");
        sw.line("if (!" + eName + ") { values.push_back(true); stack.pop_back(); break; }");
        sw.line(eName + " = " + eName + "->IgnoreParenImpCasts();");
        sw.line("if (const CallExpr *CE = dyn_cast<CallExpr>(" + eName + ")) {");
        sw.inc();
        sw.line("if (const FunctionDecl *Callee = CE->getDirectCallee()) {");
        sw.inc();
        sw.line("if (Callee->getNameAsString() == " + funcName + ") {");
        sw.inc();
        sw.line("values.push_back(true); stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (IsKnownPureFunction(Callee->getNameAsString())) {");
        sw.inc();
        sw.line("unsigned n = CE->getNumArgs();");
        sw.line("f.count = n;");
        sw.line("if (n == 0) { values.push_back(true); stack.pop_back(); break; }");
        sw.line("f.state = 1;");
        sw.line("for (int i = static_cast<int>(n) - 1; i >= 0; --i)");
        sw.inc();
        sw.line("stack.push_back({CE->getArg(i), 0, 0});");
        sw.dec();
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.dec();
        sw.line("}");
        sw.line("values.push_back(false); stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(" +
                eName + ")) {");
        sw.inc();
        sw.line("if (BO->isAssignmentOp() || BO->getOpcode() == BO_Comma) {");
        sw.inc();
        sw.line("values.push_back(false); stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.line("f.state = 2;");
        sw.line("stack.push_back({BO->getRHS(), 0, 0});");
        sw.line("stack.push_back({BO->getLHS(), 0, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(" + eName +
                ")) {");
        sw.inc();
        sw.line("if (UO->isIncrementDecrementOp()) {");
        sw.inc();
        sw.line("values.push_back(false); stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.line("f.state = 3;");
        sw.line("stack.push_back({UO->getSubExpr(), 0, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("std::vector<const Expr *> __cps_children;");
        sw.line("for (const Stmt *Child : " + eName + "->children()) {");
        sw.inc();
        sw.line("if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child))");
        sw.inc();
        sw.line("__cps_children.push_back(ChildExpr);");
        sw.dec();
        sw.dec();
        sw.line("}");
        sw.line("f.count = static_cast<unsigned>(__cps_children.size());");
        sw.line("if (f.count == 0) { values.push_back(true); stack.pop_back(); break; }");
        sw.line("f.state = 4;");
        sw.line("for (auto it = __cps_children.rbegin(); it != __cps_children.rend(); ++it)");
        sw.inc();
        sw.line("stack.push_back({*it, 0, 0});");
        sw.dec();
        sw.line("break;");
        sw.dec();
        sw.line("}");

        sw.line("case 1: {");
        sw.inc();
        sw.line("bool res = true;");
        sw.line("for (unsigned i = 0; i < f.count; ++i) {");
        sw.inc();
        sw.line("bool v = values.back(); values.pop_back();");
        sw.line("res = res && v;");
        sw.dec();
        sw.line("}");
        sw.line("values.push_back(res);");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 2: {");
        sw.inc();
        sw.line("bool rhs = values.back(); values.pop_back();");
        sw.line("bool lhs = values.back(); values.pop_back();");
        sw.line("values.push_back(lhs && rhs);");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 3: {");
        sw.inc();
        sw.line("bool v = values.back(); values.pop_back();");
        sw.line("values.push_back(v);");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 4: {");
        sw.inc();
        sw.line("bool res = true;");
        sw.line("for (unsigned i = 0; i < f.count; ++i) {");
        sw.inc();
        sw.line("bool v = values.back(); values.pop_back();");
        sw.line("res = res && v;");
        sw.dec();
        sw.line("}");
        sw.line("values.push_back(res);");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
      });
    });
    b.line("return values.empty() ? true : values.back();");
  });

  return e.str();
}

int IsPureExprIgnoringRecursiveCallsRule::cost() const {
  return RuleCatalog::IsPureExprIgnoringRecursiveCalls.Cost;
}

const char *IsPureExprIgnoringRecursiveCallsRule::name() const {
  return RuleCatalog::IsPureExprIgnoringRecursiveCalls.Name;
}

bool IsReturnOrIfReturnOrSwitchRule::applies(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  if (!CommonStructuralApplies(FD, Ctx))
    return false;
  (void)BA;
  if (Ctx.RetType != "bool")
    return false;
  if (FD->getNumParams() != 1)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(0)), "Stmt*"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (CE->getNumArgs() != 1)
      return false;
    if (!ContainsCallTo(CE->getArg(0), "body_begin"))
      return false;
  }
  return true;
}

CpsResult IsReturnOrIfReturnOrSwitchRule::apply(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string sName = FD->getParamDecl(0)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("struct Frame { const Stmt *S; };");
    b.line("std::vector<Frame> stack;");
    b.line("stack.push_back({" + sName + "});");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("Frame f = stack.back(); stack.pop_back();");
      w.line("if (isa<ReturnStmt>(f.S)) return true;");
      w.line("if (isa<IfStmt>(f.S)) return true;");
      w.line("if (isa<SwitchStmt>(f.S)) return true;");
      w.line("if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(f.S)) {");
      w.inc();
      w.line("if (CS->size() == 1) stack.push_back({CS->body_begin()[0]});");
      w.line("else return false;");
      w.dec();
      w.line("} else {");
      w.inc();
      w.line("return false;");
      w.dec();
      w.line("}");
    });
    b.line("return false;");
  });

  return e.str();
}

int IsReturnOrIfReturnOrSwitchRule::cost() const {
  return RuleCatalog::IsReturnOrIfReturnOrSwitch.Cost;
}

const char *IsReturnOrIfReturnOrSwitchRule::name() const {
  return RuleCatalog::IsReturnOrIfReturnOrSwitch.Name;
}

bool UnwrapTrailingStmtRule::applies(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  if (!CommonStructuralApplies(FD, Ctx))
    return false;
  (void)BA;
  if (!TypeContains(Ctx.RetType, "Stmt*"))
    return false;
  if (FD->getNumParams() != 1)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(0)), "Stmt*"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  if (!AllDirectRecursiveCallsNonNested(FD->getBody(), Ctx.FuncName))
    return false;
  if (HasForbiddenLoop(FD->getBody(), Ctx.FuncName, Ctx.ASTCtx))
    return false;

  for (const CallExpr *CE : calls) {
    if (CE->getNumArgs() != 1)
      return false;
    // Accept recursion on getThen() or on a local variable (e.g. the last
    // statement of a CompoundStmt body, as in UnwrapTrailingStmt).
    if (!ContainsCallTo(CE->getArg(0), "getThen") &&
        !isa<DeclRefExpr>(CE->getArg(0)->IgnoreParenImpCasts()))
      return false;
  }
  return true;
}

CpsResult UnwrapTrailingStmtRule::apply(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string sName = FD->getParamDecl(0)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.block("while (true)", [&](CodeEmitter &w) {
      w.line("if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(" + sName +
              ")) {");
      w.inc();
      w.line("if (CS->body_empty()) return nullptr;");
      w.line("const Stmt *Last = nullptr;");
      w.line("for (const Stmt *B : CS->body()) Last = B;");
      w.line(sName + " = Last;");
      w.line("continue;");
      w.dec();
      w.line("}");
      w.line("if (const IfStmt *IfS = dyn_cast<IfStmt>(" + sName + ")) {");
      w.inc();
      w.line("if (IfS->getElse()) return " + sName + ";");
      w.line(sName + " = IfS->getThen();");
      w.line("continue;");
      w.dec();
      w.line("}");
      w.line("return " + sName + ";");
    });
  });

  return e.str();
}

int UnwrapTrailingStmtRule::cost() const {
  return RuleCatalog::UnwrapTrailingStmt.Cost;
}

const char *UnwrapTrailingStmtRule::name() const {
  return RuleCatalog::UnwrapTrailingStmt.Name;
}

bool FlattenIfElseRule::applies(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  if (!CommonStructuralApplies(FD, Ctx))
    return false;
  (void)BA;
  if (Ctx.RetType != "void")
    return false;
  if (FD->getNumParams() != 3)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(0)), "Stmt*"))
    return false;
  if (TypeString(FD->getParamDecl(1)).find("BodyAnalysis") ==
      std::string::npos)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(2)), "ASTContext*"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (CE->getNumArgs() != 3)
      return false;
    // The recursive call must be on the IfStmt's else-branch variable.
    // In the original source the argument is a DeclRefExpr named "Else"
    // (from "if (const Stmt *Else = IfS->getElse()) ...").
    bool elseArg = ContainsCallTo(CE->getArg(0), "getElse") ||
                   ContainsDeclRefNamed(CE->getArg(0), "Else");
    if (!elseArg)
      return false;
    std::string arg1 = PrintExpr(CE->getArg(1), Ctx.ASTCtx);
    if (arg1 != FD->getParamDecl(1)->getNameAsString())
      return false;
    std::string arg2 = PrintExpr(CE->getArg(2), Ctx.ASTCtx);
    if (arg2 != FD->getParamDecl(2)->getNameAsString())
      return false;
  }
  return true;
}

CpsResult FlattenIfElseRule::apply(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string sName = FD->getParamDecl(0)->getNameAsString();
  std::string baName = FD->getParamDecl(1)->getNameAsString();
  std::string ctxName = FD->getParamDecl(2)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("struct Frame { const Stmt *S; };");
    b.line("std::vector<Frame> stack;");
    b.line("stack.push_back({" + sName + "});");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("Frame f = stack.back(); stack.pop_back();");
      w.line("if (!f.S) continue;");
      w.line("if (const IfStmt *IfS = dyn_cast<IfStmt>(f.S)) {");
      w.inc();
      w.line("const Expr *BaseExpr = ExtractReturnExpr(IfS->getThen());");
      w.line("if (BaseExpr) " + baName +
             ".BaseCases.push_back(MakeBaseCase(IfS->getCond(), BaseExpr, " +
             ctxName + "));");
      w.line("if (const Stmt *Else = IfS->getElse()) stack.push_back({Else});");
      w.line("continue;");
      w.dec();
      w.line("}");
      w.line("if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(f.S)) {");
      w.inc();
      w.line(baName + ".RecExpr = RS->getRetValue();");
      w.line(baName + ".IsRecursive = true;");
      w.dec();
      w.line("}");
    });
  });

  return e.str();
}

int FlattenIfElseRule::cost() const {
  return RuleCatalog::FlattenIfElse.Cost;
}

const char *FlattenIfElseRule::name() const {
  return RuleCatalog::FlattenIfElse.Name;
}

bool EvalConditionRule::applies(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  if (!CommonStructuralApplies(FD, Ctx))
    return false;
  (void)BA;
  if (Ctx.RetType != "EvalResult")
    return false;
  if (FD->getNumParams() != 3)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(0)), "Expr*"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(1)), "std::string"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(2)), "int"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (AnyArgMatches(CE, {"getLHS", "getRHS", "getSubExpr"}, {}))
      return true;
  }
  return false;
}

CpsResult EvalConditionRule::apply(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string pName = FD->getParamDecl(1)->getNameAsString();
  std::string vName = FD->getParamDecl(2)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("struct Frame { const Expr *E; int state; EvalResult saved; };");
    b.line("std::vector<Frame> stack;");
    b.line("stack.push_back({" + eName + ", 0, EvalResult::Unknown});");
    b.line(Ctx.RetType + " ret = EvalResult::Unknown;");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("Frame &f = stack.back();");
      w.block("switch (f.state)", [&](CodeEmitter &sw) {
        sw.line("case 0: {");
        sw.inc();
        sw.line("const Expr *" + eName + " = f.E;");
        sw.line(eName + " = " + eName + "->IgnoreParenImpCasts();");
        sw.line("if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(" +
                eName + ")) {");
        sw.inc();
        sw.line("if (BO->getOpcode() == BO_LAnd) {");
        sw.inc();
        sw.line("f.state = 1; stack.push_back({BO->getLHS(), 0, EvalResult::Unknown}); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (BO->getOpcode() == BO_LOr) {");
        sw.inc();
        sw.line("f.state = 2; stack.push_back({BO->getLHS(), 0, EvalResult::Unknown}); break;");
        sw.dec();
        sw.line("}");
        sw.line("int lhsVal = 0, rhsVal = 0;");
        sw.line("bool lhsKnown = ExtractParamOrLiteral(BO->getLHS(), " + pName +
                ", " + vName + ", lhsVal);");
        sw.line("bool rhsKnown = ExtractParamOrLiteral(BO->getRHS(), " + pName +
                ", " + vName + ", rhsVal);");
        sw.line("if (!lhsKnown || !rhsKnown) { ret = EvalResult::Unknown; stack.pop_back(); break; }");
        sw.line("switch (BO->getOpcode()) {");
        sw.inc();
        sw.line("case BO_EQ: ret = (lhsVal == rhsVal) ? EvalResult::True : EvalResult::False; break;");
        sw.line("case BO_NE: ret = (lhsVal != rhsVal) ? EvalResult::True : EvalResult::False; break;");
        sw.line("case BO_LT: ret = (lhsVal <  rhsVal) ? EvalResult::True : EvalResult::False; break;");
        sw.line("case BO_GT: ret = (lhsVal >  rhsVal) ? EvalResult::True : EvalResult::False; break;");
        sw.line("case BO_LE: ret = (lhsVal <= rhsVal) ? EvalResult::True : EvalResult::False; break;");
        sw.line("case BO_GE: ret = (lhsVal >= rhsVal) ? EvalResult::True : EvalResult::False; break;");
        sw.line("default: ret = EvalResult::Unknown; break;");
        sw.dec();
        sw.line("}");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(" + eName + ")) {");
        sw.inc();
        sw.line("if (UO->getOpcode() == UO_LNot) {");
        sw.inc();
        sw.line("f.state = 3; stack.push_back({UO->getSubExpr(), 0, EvalResult::Unknown}); break;");
        sw.dec();
        sw.line("}");
        sw.dec();
        sw.line("}");
        sw.line("ret = EvalResult::Unknown; stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 1: {");
        sw.inc();
        sw.line("if (ret == EvalResult::False) { stack.pop_back(); break; }");
        sw.line("f.saved = ret; f.state = 4;");
        sw.line("const BinaryOperator *BO = dyn_cast<BinaryOperator>(f.E);");
        sw.line("stack.push_back({BO->getRHS(), 0, EvalResult::Unknown}); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 2: {");
        sw.inc();
        sw.line("if (ret == EvalResult::True) { stack.pop_back(); break; }");
        sw.line("f.saved = ret; f.state = 5;");
        sw.line("const BinaryOperator *BO = dyn_cast<BinaryOperator>(f.E);");
        sw.line("stack.push_back({BO->getRHS(), 0, EvalResult::Unknown}); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 3: {");
        sw.inc();
        sw.line("if (ret == EvalResult::True) ret = EvalResult::False;");
        sw.line("else if (ret == EvalResult::False) ret = EvalResult::True;");
        sw.line("else ret = EvalResult::Unknown;");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 4: {");
        sw.inc();
        sw.line("if (f.saved == EvalResult::False || ret == EvalResult::False)");
        sw.inc();
        sw.line("ret = EvalResult::False;");
        sw.dec();
        sw.line("else if (f.saved == EvalResult::Unknown || ret == EvalResult::Unknown)");
        sw.inc();
        sw.line("ret = EvalResult::Unknown;");
        sw.dec();
        sw.line("else");
        sw.inc();
        sw.line("ret = EvalResult::True;");
        sw.dec();
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 5: {");
        sw.inc();
        sw.line("if (f.saved == EvalResult::True || ret == EvalResult::True)");
        sw.inc();
        sw.line("ret = EvalResult::True;");
        sw.dec();
        sw.line("else if (f.saved == EvalResult::Unknown || ret == EvalResult::Unknown)");
        sw.inc();
        sw.line("ret = EvalResult::Unknown;");
        sw.dec();
        sw.line("else");
        sw.inc();
        sw.line("ret = EvalResult::False;");
        sw.dec();
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
      });
    });
    b.line("return ret;");
  });

  return e.str();
}

int EvalConditionRule::cost() const {
  return RuleCatalog::EvalCondition.Cost;
}

const char *EvalConditionRule::name() const {
  return RuleCatalog::EvalCondition.Name;
}

bool ParseLinearTermsRule::applies(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  if (!CommonStructuralApplies(FD, Ctx))
    return false;
  (void)BA;
  if (Ctx.RetType != "bool")
    return false;
  if (FD->getNumParams() != 5)
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(0)), "Expr*"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(1)), "std::string"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(2)), "std::string"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(3)), "vector<LinearTerm>"))
    return false;
  if (!TypeContains(TypeString(FD->getParamDecl(4)), "int"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (AnyArgMatches(CE, {"getLHS", "getRHS", "getSubExpr"}, {}))
      return true;
  }
  return false;
}

CpsResult ParseLinearTermsRule::apply(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");
  e.line("#include <vector>");
  e.line("#include <algorithm>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string funcName = FD->getParamDecl(1)->getNameAsString();
  std::string pName = FD->getParamDecl(2)->getNameAsString();
  std::string termsName = FD->getParamDecl(3)->getNameAsString();
  std::string maxName = FD->getParamDecl(4)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("struct Frame {");
    b.inc();
    b.line("const Expr *E;");
    b.line("int state;");
    b.line("std::size_t terms_start;");
    b.line("std::size_t rhs_start;");
    b.line("int saved_max;");
    b.dec();
    b.line("};");
    b.line("std::vector<Frame> stack;");
    b.line("stack.push_back({" + eName + ", 0, " + termsName + ".size(), 0, " +
            maxName + "});");
    b.line(Ctx.RetType + " ret = false;");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("Frame &f = stack.back();");
      w.block("switch (f.state)", [&](CodeEmitter &sw) {
        sw.line("case 0: {");
        sw.inc();
        sw.line("const Expr *" + eName + " = f.E;");
        sw.line(eName + " = " + eName + "->IgnoreParenImpCasts();");
        sw.line("if (const CallExpr *CE = dyn_cast<CallExpr>(" + eName + ")) {");
        sw.inc();
        sw.line("if (const FunctionDecl *Callee = CE->getDirectCallee()) {");
        sw.inc();
        sw.line("if (Callee->getNameAsString() == " + funcName + ") {");
        sw.inc();
        sw.line("if (CE->getNumArgs() != 1) { ret = false; stack.pop_back(); break; }");
        sw.line("const Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();");
        sw.line("const BinaryOperator *BO = dyn_cast<BinaryOperator>(Arg);");
        sw.line("if (!BO || BO->getOpcode() != BO_Sub) { ret = false; stack.pop_back(); break; }");
        sw.line("const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();");
        sw.line("const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();");
        sw.line("const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(LHS);");
        sw.line("if (!DRE || DRE->getDecl()->getNameAsString() != " + pName +
                ") { ret = false; stack.pop_back(); break; }");
        sw.line("const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(RHS);");
        sw.line("if (!IL) { ret = false; stack.pop_back(); break; }");
        sw.line("int c = static_cast<int>(IL->getValue().getSExtValue());");
        sw.line("if (c <= 0) { ret = false; stack.pop_back(); break; }");
        sw.line(termsName + ".push_back({c, 1, const_cast<CallExpr *>(CE)});");
        sw.line(maxName + " = std::max(" + maxName + ", c);");
        sw.line("ret = true; stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
        sw.dec();
        sw.line("}");
        sw.dec();
        sw.line("}");
        sw.line("if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(" + eName + ")) {");
        sw.inc();
        sw.line("if (UO->getOpcode() == UO_Minus) {");
        sw.inc();
        sw.line("f.terms_start = " + termsName + ".size(); f.saved_max = " + maxName + ";");
        sw.line("f.state = 1; stack.push_back({UO->getSubExpr(), 0, 0, 0, 0}); break;");
        sw.dec();
        sw.line("}");
        sw.dec();
        sw.line("}");
        sw.line("if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(" + eName + ")) {");
        sw.inc();
        sw.line("if (BO->getOpcode() != BO_Add && BO->getOpcode() != BO_Sub) { ret = false; stack.pop_back(); break; }");
        sw.line("f.terms_start = " + termsName + ".size(); f.saved_max = " + maxName + ";");
        sw.line("f.state = 2; stack.push_back({BO->getLHS(), 0, 0, 0, 0}); break;");
        sw.dec();
        sw.line("}");
        sw.line("ret = false; stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 1: {");
        sw.inc();
        sw.line("if (!ret) { stack.pop_back(); break; }");
        sw.line("for (std::size_t i = f.terms_start; i < " + termsName +
                ".size(); ++i)");
        sw.inc();
        sw.line(termsName + "[i].Sign = -" + termsName + "[i].Sign;");
        sw.dec();
        sw.line(maxName + " = std::max(f.saved_max, " + maxName + ");");
        sw.line("ret = true; stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 2: {");
        sw.inc();
        sw.line("if (!ret) { stack.pop_back(); break; }");
        sw.line("f.rhs_start = " + termsName + ".size();");
        sw.line("const BinaryOperator *BO = dyn_cast<BinaryOperator>(f.E);");
        sw.line("f.state = 3; stack.push_back({BO->getRHS(), 0, 0, 0, 0}); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 3: {");
        sw.inc();
        sw.line("if (!ret) { stack.pop_back(); break; }");
        sw.line("const BinaryOperator *BO = dyn_cast<BinaryOperator>(f.E);");
        sw.line("if (BO->getOpcode() == BO_Sub) {");
        sw.inc();
        sw.line("for (std::size_t i = f.rhs_start; i < " + termsName +
                ".size(); ++i)");
        sw.inc();
        sw.line(termsName + "[i].Sign = -" + termsName + "[i].Sign;");
        sw.dec();
        sw.dec();
        sw.line("}");
        sw.line(maxName + " = std::max({f.saved_max, " + maxName + "});");
        sw.line("ret = true; stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
      });
    });
    b.line("return ret;");
  });

  return e.str();
}

int ParseLinearTermsRule::cost() const {
  return RuleCatalog::ParseLinearTerms.Cost;
}

const char *ParseLinearTermsRule::name() const {
  return RuleCatalog::ParseLinearTerms.Name;
}


} // namespace cps
