#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

bool IsDirectRecursiveCall(const CallExpr *CE, const std::string &FuncName) {
  if (!CE)
    return false;
  if (const FunctionDecl *Callee = CE->getDirectCallee())
    return Callee->getNameAsString() == FuncName;
  return false;
}

// Iterative collection of direct recursive calls.  We do not descend into the
// arguments of a recursive call itself, matching the "holes" semantics used
// elsewhere in the transpiler.
void CollectDirectRecursiveCalls(const Stmt *Root,
                                 const std::string &FuncName,
                                 std::vector<const CallExpr *> &Out) {
  if (!Root)
    return;
  std::vector<const Stmt *> Stack;
  Stack.push_back(Root);
  while (!Stack.empty()) {
    const Stmt *S = Stack.back();
    Stack.pop_back();
    if (!S)
      continue;
    if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
      if (IsDirectRecursiveCall(CE, FuncName)) {
        Out.push_back(CE);
        continue;
      }
    }
    for (const Stmt *Child : S->children())
      Stack.push_back(Child);
  }
}

// Reject functions that contain loops with recursive calls inside them.
// A non-recursive loop (e.g. the for-loop that finds the last statement in
// IsInTailPosition's CompoundStmt case) is harmless for the explicit-stack
// state machine.
bool HasForbiddenLoop(const Stmt *Root, const std::string &FuncName) {
  if (!Root)
    return false;
  std::vector<const Stmt *> Stack;
  Stack.push_back(Root);
  while (!Stack.empty()) {
    const Stmt *S = Stack.back();
    Stack.pop_back();
    if (!S)
      continue;
    if (isa<ForStmt>(S) || isa<WhileStmt>(S) || isa<DoStmt>(S)) {
      std::vector<const CallExpr *> calls;
      CollectDirectRecursiveCalls(S, FuncName, calls);
      if (!calls.empty())
        return true;
      continue;
    }
    for (const Stmt *Child : S->children())
      Stack.push_back(Child);
  }
  return false;
}

bool AllDirectRecursiveCallsNonNested(const Stmt *Root,
                                      const std::string &FuncName) {
  std::vector<const CallExpr *> Calls;
  CollectDirectRecursiveCalls(Root, FuncName, Calls);
  for (const CallExpr *CE : Calls) {
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (ContainsRecursiveCall(CE->getArg(i), FuncName))
        return false;
    }
  }
  return true;
}

std::string TypeString(const ParmVarDecl *PVD) {
  return PVD->getType().getAsString();
}

bool TypeIs(const std::string &T, const std::string &Pattern) {
  // Loose match: ignore spaces for shape detection.
  std::string normalized;
  for (char c : T) {
    if (!std::isspace(static_cast<unsigned char>(c)))
      normalized += c;
  }
  return normalized.find(Pattern) != std::string::npos;
}

bool ArgSourceContains(const CallExpr *CE, const std::string &Needle,
                       const ASTContext *Ctx) {
  if (!CE)
    return false;
  for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
    std::string s = PrintExpr(CE->getArg(i), Ctx);
    if (s.find(Needle) != std::string::npos)
      return true;
  }
  return false;
}

} // anonymous namespace

// ============================================================
// Shape detection
// ============================================================

bool StructuralRecursionRule::appliesToIsInTailPosition(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  (void)BA;
  if (Ctx.RetType != "bool")
    return false;
  if (FD->getNumParams() != 2)
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(0)), "Stmt*"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(1)), "FunctionDecl*"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (ArgSourceContains(CE, "getThen", Ctx.ASTCtx) ||
        ArgSourceContains(CE, "getElse", Ctx.ASTCtx) ||
        ArgSourceContains(CE, "body_back", Ctx.ASTCtx))
      return true;
  }
  return false;
}

bool StructuralRecursionRule::appliesToEvalCondition(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  (void)BA;
  if (Ctx.RetType != "EvalResult")
    return false;
  if (FD->getNumParams() != 3)
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(0)), "Expr*"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(1)), "std::string"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(2)), "int"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (ArgSourceContains(CE, "getLHS", Ctx.ASTCtx) ||
        ArgSourceContains(CE, "getRHS", Ctx.ASTCtx) ||
        ArgSourceContains(CE, "getSubExpr", Ctx.ASTCtx))
      return true;
  }
  return false;
}

bool StructuralRecursionRule::appliesToParseLinearTerms(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  (void)BA;
  if (Ctx.RetType != "bool")
    return false;
  if (FD->getNumParams() != 5)
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(0)), "Expr*"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(1)), "std::string"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(2)), "std::string"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(3)), "vector<LinearTerm>"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(4)), "int"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (ArgSourceContains(CE, "getLHS", Ctx.ASTCtx) ||
        ArgSourceContains(CE, "getRHS", Ctx.ASTCtx) ||
        ArgSourceContains(CE, "getSubExpr", Ctx.ASTCtx))
      return true;
  }
  return false;
}

// ============================================================
// Rule interface
// ============================================================

bool StructuralRecursionRule::applies(const FunctionDecl *FD,
                                      const BodyAnalysis &BA,
                                      const GenContext &Ctx) const {
  if (!BA.IsRecursive)
    return false;
  if (HasForbiddenLoop(FD->getBody(), Ctx.FuncName))
    return false;

  std::vector<const CallExpr *> recCalls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, recCalls);
  if (recCalls.empty())
    return false;
  if (!AllDirectRecursiveCallsNonNested(FD->getBody(), Ctx.FuncName))
    return false;

  return appliesToIsInTailPosition(FD, BA, Ctx) ||
         appliesToIsInTailPositionExpr(FD, BA, Ctx) ||
         appliesToEvalCondition(FD, BA, Ctx) ||
         appliesToParseLinearTerms(FD, BA, Ctx);
}

bool StructuralRecursionRule::appliesToIsInTailPositionExpr(
    const FunctionDecl *FD, const BodyAnalysis &BA,
    const GenContext &Ctx) const {
  (void)BA;
  if (Ctx.RetType != "bool")
    return false;
  if (FD->getNumParams() != 3)
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(0)), "Expr*"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(1)), "Stmt*"))
    return false;
  if (!TypeIs(TypeString(FD->getParamDecl(2)), "std::string"))
    return false;

  std::vector<const CallExpr *> calls;
  CollectDirectRecursiveCalls(FD->getBody(), Ctx.FuncName, calls);
  if (calls.empty())
    return false;
  for (const CallExpr *CE : calls) {
    if (ArgSourceContains(CE, "getThen", Ctx.ASTCtx) ||
        ArgSourceContains(CE, "getElse", Ctx.ASTCtx) ||
        ArgSourceContains(CE, "body", Ctx.ASTCtx))
      return true;
  }
  return false;
}

std::string StructuralRecursionRule::apply(const FunctionDecl *FD,
                                           const BodyAnalysis &BA,
                                           GenContext &Ctx) const {
  if (appliesToIsInTailPosition(FD, BA, Ctx))
    return applyIsInTailPosition(FD, BA, Ctx);
  if (appliesToIsInTailPositionExpr(FD, BA, Ctx))
    return applyIsInTailPositionExpr(FD, BA, Ctx);
  if (appliesToEvalCondition(FD, BA, Ctx))
    return applyEvalCondition(FD, BA, Ctx);
  if (appliesToParseLinearTerms(FD, BA, Ctx))
    return applyParseLinearTerms(FD, BA, Ctx);
  return "";
}

int StructuralRecursionRule::cost() const { return 160; }

const char *StructuralRecursionRule::name() const {
  return "StructuralRecursionRule";
}

// ============================================================
// IsInTailPosition
// ============================================================

std::string StructuralRecursionRule::applyIsInTailPosition(
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

// ============================================================
// EvalConditionForParam
// ============================================================

std::string StructuralRecursionRule::applyEvalCondition(
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

// ============================================================
// ParseLinearTerms
// ============================================================

std::string StructuralRecursionRule::applyParseLinearTerms(
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

// ============================================================
// IsInTailPosition(Expr*, Stmt*, string)
// ============================================================

std::string StructuralRecursionRule::applyIsInTailPositionExpr(
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

} // namespace cps
