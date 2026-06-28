#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include <cctype>
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

std::string TypeString(const ParmVarDecl *PVD) {
  return PVD->getType().getAsString();
}

bool TypeContains(const std::string &T, const std::string &Pattern) {
  std::string normalized;
  for (char c : T) {
    if (!std::isspace(static_cast<unsigned char>(c)))
      normalized += c;
  }
  return normalized.find(Pattern) != std::string::npos;
}

static const std::vector<std::string> kSubexprAccessors = {
    "getLHS",     "getRHS",      "getSubExpr", "getArg",
    "getCond",    "getTrueExpr", "getFalseExpr", "getBase",
    "getIdx",     "getCallee"};

// Return true if E is a DeclRefExpr that names ParamName or a variable in the
// derived set.
bool IsDerivedExpr(const Expr *E, const std::string &ParamName,
                   const std::unordered_set<std::string> &Derived) {
  if (!E)
    return false;
  const Expr *Clean = E->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Clean)) {
    std::string Name = DRE->getDecl()->getNameAsString();
    return Name == ParamName || Derived.count(Name);
  }
  return false;
}

// Check whether E is a sub-expression access of the first parameter or of a
// variable already known to be derived from the first parameter.
bool IsSubexpressionAccess(const Expr *E, const std::string &ParamName,
                           const std::unordered_set<std::string> &Derived,
                           const ASTContext *Ctx) {
  (void)Ctx;
  if (!E)
    return false;
  E = E->IgnoreParenImpCasts();

  const CallExpr *CE = dyn_cast<CallExpr>(E);
  if (!CE)
    return false;

  std::string Name = GetCalleeName(CE);

  // dyn_cast<T>(base)
  if (Name == "dyn_cast" || Name == "dyn_cast_or_null") {
    if (CE->getNumArgs() >= 1)
      return IsDerivedExpr(CE->getArg(0), ParamName, Derived);
    return false;
  }

  // base->accessor()
  for (const std::string &Acc : kSubexprAccessors) {
    if (Name == Acc) {
      if (const CXXMemberCallExpr *MCE = dyn_cast<CXXMemberCallExpr>(CE))
        return IsDerivedExpr(MCE->getImplicitObjectArgument(), ParamName,
                             Derived);
      return false;
    }
  }

  return false;
}

void CollectDerivedVariables(const Stmt *S, const std::string &ParamName,
                             std::unordered_set<std::string> &Derived,
                             const ASTContext *Ctx) {
  if (!S)
    return;

  if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
        if (const Expr *Init = VD->getInit()) {
          if (IsSubexpressionAccess(Init, ParamName, Derived, Ctx))
            Derived.insert(VD->getNameAsString());
        }
      }
    }
  }

  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    if (const VarDecl *VD = IfS->getConditionVariable()) {
      if (const Expr *Init = VD->getInit()) {
        if (IsSubexpressionAccess(Init, ParamName, Derived, Ctx))
          Derived.insert(VD->getNameAsString());
      }
    }
  }

  for (const Stmt *Child : S->children())
    CollectDerivedVariables(Child, ParamName, Derived, Ctx);
}

bool IsStringStructuralRecursionShape(const FunctionDecl *FD,
                                      const GenContext &Ctx) {
  if (!TypeContains(Ctx.RetType, "string"))
    return false;
  if (FD->getNumParams() != 3)
    return false;

  std::string t0 = TypeString(FD->getParamDecl(0));
  std::string t1 = TypeString(FD->getParamDecl(1));
  std::string t2 = TypeString(FD->getParamDecl(2));

  if (!TypeContains(t0, "Expr*"))
    return false;
  if (!TypeContains(t1, "unordered_map") ||
      !TypeContains(t1, "Expr*") ||
      !TypeContains(t1, "string"))
    return false;
  if (!TypeContains(t2, "ASTContext*"))
    return false;

  std::vector<CallExpr *> recCalls;
  CollectRecursiveCallsInStmt(FD->getBody(), Ctx.FuncName, recCalls);
  if (recCalls.empty())
    return false;

  std::string paramName = FD->getParamDecl(0)->getNameAsString();
  std::string replsName = FD->getParamDecl(1)->getNameAsString();
  std::string ctxName = FD->getParamDecl(2)->getNameAsString();

  std::unordered_set<std::string> derived;
  CollectDerivedVariables(FD->getBody(), paramName, derived, Ctx.ASTCtx);

  for (CallExpr *CE : recCalls) {
    if (CE->getNumArgs() != 3)
      return false;

    std::string arg0 = PrintExpr(CE->getArg(0), Ctx.ASTCtx);
    std::string arg1 = PrintExpr(CE->getArg(1), Ctx.ASTCtx);
    std::string arg2 = PrintExpr(CE->getArg(2), Ctx.ASTCtx);

    if (arg1 != replsName)
      return false;
    if (arg2 != ctxName)
      return false;

    // The first argument must be a sub-expression of the first parameter,
    // either directly or via a derived local variable.
    std::string arg0Base = StripOuterParens(arg0);
    if (arg0Base == paramName)
      return false;
    if (derived.count(arg0Base))
      continue;
    if (IsSubexpressionAccess(CE->getArg(0), paramName, derived, Ctx.ASTCtx))
      continue;

    return false;
  }

  return true;
}

} // anonymous namespace

bool StringStructuralRecursionRule::applies(const FunctionDecl *FD,
                                            const BodyAnalysis &BA,
                                            const GenContext &Ctx) const {
  (void)BA;
  return IsStringStructuralRecursionShape(FD, Ctx);
}

CpsResult StringStructuralRecursionRule::apply(const FunctionDecl *FD,
                                                 const BodyAnalysis &BA,
                                                 GenContext &Ctx) const {
  (void)BA;
  CodeEmitter e;
  e.raw("// === Generated structural-recursion code for function: " +
        Ctx.FuncName + " ===\n\n");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string replsName = FD->getParamDecl(1)->getNameAsString();
  std::string ctxName = FD->getParamDecl(2)->getNameAsString();

  e.block(sig, [&](CodeEmitter &b) {
    b.line("struct Frame {");
    b.inc();
    b.line("const Expr *E;");
    b.line("int state;");
    b.line("std::string s1;");
    b.line("std::string s2;");
    b.line("bool b;");
    b.line("unsigned count;");
    b.dec();
    b.line("};");
    b.line("std::vector<Frame> stack;");
    b.line("std::vector<std::string> values;");
    b.line("stack.push_back({" + eName + ", 0, \"\", \"\", false, 0});");
    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("Frame &f = stack.back();");
      w.block("switch (f.state)", [&](CodeEmitter &sw) {
        sw.line("case 0: {");
        sw.inc();
        sw.line("const Expr *" + eName + " = f.E;");
        sw.line("if (!" + eName + ") { values.push_back(\"\"); stack.pop_back(); break; }");
        sw.line("auto It = " + replsName + ".find(" + eName + ");");
        sw.line("if (It != " + replsName + ".end()) { values.push_back(It->second); stack.pop_back(); break; }");
        sw.line("if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 1; f.s1 = BO->getOpcodeStr().str();");
        sw.line("stack.push_back({BO->getRHS(), 0, \"\", \"\", false, 0});");
        sw.line("stack.push_back({BO->getLHS(), 0, \"\", \"\", false, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 2; f.s1 = UO->getOpcodeStr(UO->getOpcode()).str(); f.b = UO->isPostfix();");
        sw.line("stack.push_back({UO->getSubExpr(), 0, \"\", \"\", false, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const CallExpr *CE = dyn_cast<CallExpr>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 3; f.count = CE->getNumArgs();");
        sw.line("const Expr *Callee = CE->getCallee();");
        sw.line("f.b = (Callee != nullptr);");
        sw.line("if (Callee) stack.push_back({Callee, 0, \"\", \"\", false, 0});");
        sw.line("for (int i = static_cast<int>(f.count) - 1; i >= 0; --i)");
        sw.inc();
        sw.line("stack.push_back({CE->getArg(i), 0, \"\", \"\", false, 0});");
        sw.dec();
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const ConditionalOperator *CO = dyn_cast<ConditionalOperator>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 4; f.count = 3;");
        sw.line("stack.push_back({CO->getFalseExpr(), 0, \"\", \"\", false, 0});");
        sw.line("stack.push_back({CO->getTrueExpr(), 0, \"\", \"\", false, 0});");
        sw.line("stack.push_back({CO->getCond(), 0, \"\", \"\", false, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const ArraySubscriptExpr *ASE = dyn_cast<ArraySubscriptExpr>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 5;");
        sw.line("stack.push_back({ASE->getIdx(), 0, \"\", \"\", false, 0});");
        sw.line("stack.push_back({ASE->getBase(), 0, \"\", \"\", false, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const MemberExpr *ME = dyn_cast<MemberExpr>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 6; f.s1 = ME->getMemberNameInfo().getAsString(); f.b = ME->isArrow();");
        sw.line("stack.push_back({ME->getBase(), 0, \"\", \"\", false, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const ParenExpr *PE = dyn_cast<ParenExpr>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 7;");
        sw.line("stack.push_back({PE->getSubExpr(), 0, \"\", \"\", false, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 8;");
        sw.line("stack.push_back({ICE->getSubExpr(), 0, \"\", \"\", false, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("if (const CStyleCastExpr *CCE = dyn_cast<CStyleCastExpr>(" + eName + ")) {");
        sw.inc();
        sw.line("f.state = 9; f.s1 = CCE->getTypeAsWritten().getAsString();");
        sw.line("stack.push_back({CCE->getSubExpr(), 0, \"\", \"\", false, 0});");
        sw.line("break;");
        sw.dec();
        sw.line("}");
        sw.line("values.push_back(PrintExpr(" + eName + ", " + ctxName + "));");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 1: {");
        sw.inc();
        sw.line("std::string rhs = values.back(); values.pop_back();");
        sw.line("std::string lhs = values.back(); values.pop_back();");
        sw.line("values.push_back(\"(\" + lhs + \" \" + f.s1 + \" \" + rhs + \")\");");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 2: {");
        sw.inc();
        sw.line("std::string sub = values.back(); values.pop_back();");
        sw.line("if (!f.b) values.push_back(f.s1 + \"(\" + sub + \")\");");
        sw.line("else values.push_back(\"(\" + sub + \")\" + f.s1);");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 3: {");
        sw.inc();
        sw.line("std::vector<std::string> args(f.count);");
        sw.line("for (unsigned i = 0; i < f.count; ++i) {");
        sw.inc();
        sw.line("args[i] = values.back(); values.pop_back();");
        sw.dec();
        sw.line("}");
        sw.line("std::string callee;");
        sw.line("if (f.b) { callee = values.back(); values.pop_back(); }");
        sw.line("std::string s = callee + \"(\";");
        sw.line("for (unsigned i = 0; i < f.count; ++i) {");
        sw.inc();
        sw.line("if (i > 0) s += \", \";");
        sw.line("s += args[i];");
        sw.dec();
        sw.line("}");
        sw.line("s += \")\";");
        sw.line("values.push_back(s);");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 4: {");
        sw.inc();
        sw.line("std::string falseExpr = values.back(); values.pop_back();");
        sw.line("std::string trueExpr = values.back(); values.pop_back();");
        sw.line("std::string cond = values.back(); values.pop_back();");
        sw.line("values.push_back(\"(\" + cond + \" ? \" + trueExpr + \" : \" + falseExpr + \")\");");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 5: {");
        sw.inc();
        sw.line("std::string idx = values.back(); values.pop_back();");
        sw.line("std::string base = values.back(); values.pop_back();");
        sw.line("values.push_back(base + \"[\" + idx + \"]\");");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 6: {");
        sw.inc();
        sw.line("std::string base = values.back(); values.pop_back();");
        sw.line("values.push_back(base + (f.b ? \"->\" : \".\") + f.s1);");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 7: {");
        sw.inc();
        sw.line("std::string sub = values.back(); values.pop_back();");
        sw.line("values.push_back(\"(\" + sub + \")\");");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 8: {");
        sw.inc();
        sw.line("std::string sub = values.back(); values.pop_back();");
        sw.line("values.push_back(sub);");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");

        sw.line("case 9: {");
        sw.inc();
        sw.line("std::string sub = values.back(); values.pop_back();");
        sw.line("values.push_back(\"(\" + f.s1 + \")(\" + sub + \")\");");
        sw.line("stack.pop_back(); break;");
        sw.dec();
        sw.line("}");
      });
    });
    b.line("return values.empty() ? \"\" : values.back();");
  });

  return e.str();
}

int StringStructuralRecursionRule::cost() const {
  return RuleCatalog::StringStructuralRecursion.Cost;
}

const char *StringStructuralRecursionRule::name() const {
  return RuleCatalog::StringStructuralRecursion.Name;
}

} // namespace cps
