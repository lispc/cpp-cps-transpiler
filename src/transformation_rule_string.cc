#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

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

// Small local conveniences over IRBuilder for the leaf statements this rule
// emits (mirrors transformation_rule_structural_subrules.cc).

void AddStmt(IRBlock *blk, std::unique_ptr<IRStmt> s) {
  IRBuilder::add(blk, std::move(s));
}

// Add an expression statement (the IR printer appends the semicolon).
void EmitExpr(IRBlock *blk, const std::string &e) {
  AddStmt(blk, IRBuilder::expr(IRExpr(e)));
}

// Add a raw statement line; the text must carry its own semicolons.
void EmitRaw(IRBlock *blk, const std::string &text) {
  AddStmt(blk, IRBuilder::rawStmt(text));
}

// Add a `type name = init;` variable declaration.
void EmitVar(IRBlock *blk, const std::string &type, const std::string &name,
             const std::string &init) {
  AddStmt(blk, IRBuilder::var(type, name, IRExpr(init)));
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string replsName = FD->getParamDecl(1)->getNameAsString();
  std::string ctxName = FD->getParamDecl(2)->getNameAsString();

  auto body = IRBuilder::block();
  {
    IRStructData frame;
    frame.name = "Frame";
    frame.fields = {{"const Expr *", "E"}, {"int", "state"},
                    {"std::string", "s1"}, {"std::string", "s2"},
                    {"bool", "b"},         {"unsigned", "count"}};
    AddStmt(body.get(), IRBuilder::localStruct(std::move(frame)));
  }
  AddStmt(body.get(), IRBuilder::var("std::vector<Frame>", "stack"));
  AddStmt(body.get(), IRBuilder::var("std::vector<std::string>", "values"));
  EmitExpr(body.get(),
           "stack.push_back({" + eName + ", 0, \"\", \"\", false, 0})");

  auto w = IRBuilder::block();
  EmitVar(w.get(), "Frame &", "f", "stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    EmitVar(c0.get(), "const Expr *", eName, "f.E");
    EmitRaw(c0.get(), "if (!" + eName +
                          ") { values.push_back(\"\"); stack.pop_back(); "
                          "break; }");
    EmitVar(c0.get(), "auto", "It", replsName + ".find(" + eName + ")");
    EmitRaw(c0.get(), "if (It != " + replsName +
                          ".end()) { values.push_back(It->second); "
                          "stack.pop_back(); break; }");
    {
      auto boBlk = IRBuilder::block();
      EmitRaw(boBlk.get(), "f.state = 1; f.s1 = BO->getOpcodeStr().str();");
      EmitExpr(boBlk.get(),
               "stack.push_back({BO->getRHS(), 0, \"\", \"\", false, 0})");
      EmitExpr(boBlk.get(),
               "stack.push_back({BO->getLHS(), 0, \"\", \"\", false, 0})");
      AddStmt(boBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                    "dyn_cast<BinaryOperator>(" +
                                    eName + ")"),
                             std::move(boBlk)));
    }
    {
      auto uoBlk = IRBuilder::block();
      EmitRaw(uoBlk.get(),
              "f.state = 2; f.s1 = UO->getOpcodeStr(UO->getOpcode()).str(); "
              "f.b = UO->isPostfix();");
      EmitExpr(uoBlk.get(),
               "stack.push_back({UO->getSubExpr(), 0, \"\", \"\", false, 0})");
      AddStmt(uoBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const UnaryOperator *UO = "
                                    "dyn_cast<UnaryOperator>(" +
                                    eName + ")"),
                             std::move(uoBlk)));
    }
    {
      auto ceBlk = IRBuilder::block();
      EmitRaw(ceBlk.get(), "f.state = 3; f.count = CE->getNumArgs();");
      EmitVar(ceBlk.get(), "const Expr *", "Callee", "CE->getCallee()");
      EmitRaw(ceBlk.get(), "f.b = (Callee != nullptr);");
      EmitRaw(ceBlk.get(),
              "if (Callee) stack.push_back({Callee, 0, \"\", \"\", false, "
              "0});");
      AddStmt(ceBlk.get(),
              IRBuilder::for_("int i = static_cast<int>(f.count) - 1",
                              IRExpr("i >= 0"), "--i",
                              IRBuilder::expr(IRExpr(
                                  "stack.push_back({CE->getArg(i), 0, \"\", "
                                  "\"\", false, 0})"))));
      AddStmt(ceBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const CallExpr *CE = dyn_cast<CallExpr>(" +
                                    eName + ")"),
                             std::move(ceBlk)));
    }
    {
      auto coBlk = IRBuilder::block();
      EmitRaw(coBlk.get(), "f.state = 4; f.count = 3;");
      EmitExpr(coBlk.get(), "stack.push_back({CO->getFalseExpr(), 0, \"\", "
                            "\"\", false, 0})");
      EmitExpr(coBlk.get(), "stack.push_back({CO->getTrueExpr(), 0, \"\", "
                            "\"\", false, 0})");
      EmitExpr(coBlk.get(), "stack.push_back({CO->getCond(), 0, \"\", \"\", "
                            "false, 0})");
      AddStmt(coBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const ConditionalOperator *CO = "
                                    "dyn_cast<ConditionalOperator>(" +
                                    eName + ")"),
                             std::move(coBlk)));
    }
    {
      auto aseBlk = IRBuilder::block();
      EmitRaw(aseBlk.get(), "f.state = 5;");
      EmitExpr(aseBlk.get(), "stack.push_back({ASE->getIdx(), 0, \"\", \"\", "
                             "false, 0})");
      EmitExpr(aseBlk.get(), "stack.push_back({ASE->getBase(), 0, \"\", \"\", "
                             "false, 0})");
      AddStmt(aseBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const ArraySubscriptExpr *ASE = "
                                    "dyn_cast<ArraySubscriptExpr>(" +
                                    eName + ")"),
                             std::move(aseBlk)));
    }
    {
      auto meBlk = IRBuilder::block();
      EmitRaw(meBlk.get(),
              "f.state = 6; f.s1 = ME->getMemberNameInfo().getAsString(); "
              "f.b = ME->isArrow();");
      EmitExpr(meBlk.get(),
               "stack.push_back({ME->getBase(), 0, \"\", \"\", false, 0})");
      AddStmt(meBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const MemberExpr *ME = "
                                    "dyn_cast<MemberExpr>(" +
                                    eName + ")"),
                             std::move(meBlk)));
    }
    {
      auto peBlk = IRBuilder::block();
      EmitRaw(peBlk.get(), "f.state = 7;");
      EmitExpr(peBlk.get(),
               "stack.push_back({PE->getSubExpr(), 0, \"\", \"\", false, 0})");
      AddStmt(peBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const ParenExpr *PE = "
                                    "dyn_cast<ParenExpr>(" +
                                    eName + ")"),
                             std::move(peBlk)));
    }
    {
      auto iceBlk = IRBuilder::block();
      EmitRaw(iceBlk.get(), "f.state = 8;");
      EmitExpr(iceBlk.get(),
               "stack.push_back({ICE->getSubExpr(), 0, \"\", \"\", false, "
               "0})");
      AddStmt(iceBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const ImplicitCastExpr *ICE = "
                                    "dyn_cast<ImplicitCastExpr>(" +
                                    eName + ")"),
                             std::move(iceBlk)));
    }
    {
      auto cceBlk = IRBuilder::block();
      EmitRaw(cceBlk.get(),
              "f.state = 9; f.s1 = CCE->getTypeAsWritten().getAsString();");
      EmitExpr(cceBlk.get(),
               "stack.push_back({CCE->getSubExpr(), 0, \"\", \"\", false, "
               "0})");
      AddStmt(cceBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const CStyleCastExpr *CCE = "
                                    "dyn_cast<CStyleCastExpr>(" +
                                    eName + ")"),
                             std::move(cceBlk)));
    }
    EmitExpr(c0.get(),
             "values.push_back(PrintExpr(" + eName + ", " + ctxName + "))");
    EmitRaw(c0.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    EmitRaw(c1.get(), "std::string rhs = values.back(); values.pop_back();");
    EmitRaw(c1.get(), "std::string lhs = values.back(); values.pop_back();");
    EmitExpr(c1.get(), "values.push_back(\"(\" + lhs + \" \" + f.s1 + \" \" + "
                       "rhs + \")\")");
    EmitRaw(c1.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    EmitRaw(c2.get(), "std::string sub = values.back(); values.pop_back();");
    EmitRaw(c2.get(), "if (!f.b) values.push_back(f.s1 + \"(\" + sub + \")\");");
    EmitRaw(c2.get(), "else values.push_back(\"(\" + sub + \")\" + f.s1);");
    EmitRaw(c2.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    EmitRaw(c3.get(), "std::vector<std::string> args(f.count);");
    {
      auto argLoop = IRBuilder::block();
      EmitRaw(argLoop.get(), "args[i] = values.back(); values.pop_back();");
      AddStmt(c3.get(),
              IRBuilder::for_("unsigned i = 0", IRExpr("i < f.count"), "++i",
                              std::move(argLoop)));
    }
    AddStmt(c3.get(), IRBuilder::var("std::string", "callee"));
    EmitRaw(c3.get(), "if (f.b) { callee = values.back(); values.pop_back(); }");
    EmitVar(c3.get(), "std::string", "s", "callee + \"(\"");
    {
      auto buildLoop = IRBuilder::block();
      EmitRaw(buildLoop.get(), "if (i > 0) s += \", \";");
      EmitExpr(buildLoop.get(), "s += args[i]");
      AddStmt(c3.get(),
              IRBuilder::for_("unsigned i = 0", IRExpr("i < f.count"), "++i",
                              std::move(buildLoop)));
    }
    EmitExpr(c3.get(), "s += \")\"");
    EmitExpr(c3.get(), "values.push_back(s)");
    EmitRaw(c3.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  {
    auto c4 = IRBuilder::block();
    EmitRaw(c4.get(), "std::string falseExpr = values.back(); "
                      "values.pop_back();");
    EmitRaw(c4.get(), "std::string trueExpr = values.back(); "
                      "values.pop_back();");
    EmitRaw(c4.get(), "std::string cond = values.back(); values.pop_back();");
    EmitExpr(c4.get(), "values.push_back(\"(\" + cond + \" ? \" + trueExpr + "
                       "\" : \" + falseExpr + \")\")");
    EmitRaw(c4.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"4"}, std::move(c4));
  }
  {
    auto c5 = IRBuilder::block();
    EmitRaw(c5.get(), "std::string idx = values.back(); values.pop_back();");
    EmitRaw(c5.get(), "std::string base = values.back(); values.pop_back();");
    EmitExpr(c5.get(), "values.push_back(base + \"[\" + idx + \"]\")");
    EmitRaw(c5.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"5"}, std::move(c5));
  }
  {
    auto c6 = IRBuilder::block();
    EmitRaw(c6.get(), "std::string base = values.back(); values.pop_back();");
    EmitExpr(c6.get(), "values.push_back(base + (f.b ? \"->\" : \".\") + f.s1)");
    EmitRaw(c6.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"6"}, std::move(c6));
  }
  {
    auto c7 = IRBuilder::block();
    EmitRaw(c7.get(), "std::string sub = values.back(); values.pop_back();");
    EmitExpr(c7.get(), "values.push_back(\"(\" + sub + \")\")");
    EmitRaw(c7.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"7"}, std::move(c7));
  }
  {
    auto c8 = IRBuilder::block();
    EmitRaw(c8.get(), "std::string sub = values.back(); values.pop_back();");
    EmitExpr(c8.get(), "values.push_back(sub)");
    EmitRaw(c8.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"8"}, std::move(c8));
  }
  {
    auto c9 = IRBuilder::block();
    EmitRaw(c9.get(), "std::string sub = values.back(); values.pop_back();");
    EmitExpr(c9.get(), "values.push_back(\"(\" + f.s1 + \")(\" + sub + \")\")");
    EmitRaw(c9.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"9"}, std::move(c9));
  }
  AddStmt(w.get(), std::move(sw));
  AddStmt(body.get(),
          IRBuilder::while_(IRExpr("!stack.empty()"), std::move(w)));
  AddStmt(body.get(),
          IRBuilder::ret(IRExpr("values.empty() ? \"\" : values.back()")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

int StringStructuralRecursionRule::cost() const {
  return RuleCatalog::StringStructuralRecursion.Cost;
}

const char *StringStructuralRecursionRule::name() const {
  return RuleCatalog::StringStructuralRecursion.Name;
}

} // namespace cps
