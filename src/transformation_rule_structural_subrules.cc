#include "transformation_rules.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
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

// Small local conveniences over IRBuilder for the leaf statements these
// rules emit.

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

// Build a local `struct Frame { ... };` definition from (type, name) fields.
std::unique_ptr<IRLocalStruct>
FrameStruct(std::vector<std::pair<std::string, std::string>> fields) {
  IRStructData data;
  data.name = "Frame";
  for (auto &f : fields)
    data.fields.emplace_back(std::move(f.first), std::move(f.second));
  return IRBuilder::localStruct(std::move(data));
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string sName = FD->getParamDecl(0)->getNameAsString();
  std::string targetName = FD->getParamDecl(1)->getNameAsString();

  auto body = IRBuilder::block();
  AddStmt(body.get(), FrameStruct({{"const Stmt *", "S"},
                                   {"int", "state"},
                                   {"bool", "saved"}}));
  AddStmt(body.get(), IRBuilder::var("std::vector<Frame>", "stack"));
  EmitExpr(body.get(), "stack.push_back({" + sName + ", 0, false})");
  EmitVar(body.get(), Ctx.RetType, "ret", "false");

  auto w = IRBuilder::block();
  EmitVar(w.get(), "Frame &", "f", "stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    EmitVar(c0.get(), "const Stmt *", sName, "f.S");
    EmitRaw(c0.get(),
            "if (!" + sName + ") { ret = false; stack.pop_back(); break; }");
    {
      auto rsBlk = IRBuilder::block();
      EmitVar(rsBlk.get(), "const Expr *", "E", "RS->getRetValue()");
      {
        auto ceBlk = IRBuilder::block();
        {
          auto calleeBlk = IRBuilder::block();
          EmitExpr(calleeBlk.get(), "ret = Callee->getNameAsString() == " +
                                        targetName + "->getNameAsString()");
          EmitExpr(calleeBlk.get(), "stack.pop_back()");
          AddStmt(calleeBlk.get(), IRBuilder::break_());
          AddStmt(ceBlk.get(),
                  IRBuilder::if_(IRExpr("const FunctionDecl *Callee = "
                                        "CE->getDirectCallee()"),
                                 std::move(calleeBlk)));
        }
        AddStmt(rsBlk.get(),
                IRBuilder::if_(
                    IRExpr("const CallExpr *CE = dyn_cast<CallExpr>(E)"),
                    std::move(ceBlk)));
      }
      EmitRaw(rsBlk.get(), "ret = false; stack.pop_back(); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const ReturnStmt *RS = "
                                    "dyn_cast<ReturnStmt>(" +
                                    sName + ")"),
                             std::move(rsBlk)));
    }
    {
      auto isBlk = IRBuilder::block();
      {
        auto condBlk = IRBuilder::block();
        {
          auto boBlk = IRBuilder::block();
          {
            auto opBlk = IRBuilder::block();
            EmitRaw(opBlk.get(), "f.state = 1; stack.push_back({IS->getThen(), "
                                 "0, false}); break;");
            AddStmt(boBlk.get(),
                    IRBuilder::if_(IRExpr("BO->getOpcode() == BO_LAnd || "
                                          "BO->getOpcode() == BO_LOr"),
                                   std::move(opBlk)));
          }
          AddStmt(condBlk.get(),
                  IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                        "dyn_cast<BinaryOperator>(Cond)"),
                                 std::move(boBlk)));
        }
        AddStmt(isBlk.get(),
                IRBuilder::if_(IRExpr("const Expr *Cond = IS->getCond()"),
                               std::move(condBlk)));
      }
      EmitRaw(isBlk.get(), "f.state = 2; stack.push_back({IS->getThen(), 0, "
                           "false}); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(
                  IRExpr("const IfStmt *IS = dyn_cast<IfStmt>(" + sName + ")"),
                  std::move(isBlk)));
    }
    {
      auto csBlk = IRBuilder::block();
      EmitRaw(csBlk.get(), "if (CS->body_empty()) { ret = false; "
                           "stack.pop_back(); break; }");
      EmitRaw(csBlk.get(), "f.state = 5; stack.push_back({CS->body_back(), 0, "
                           "false}); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const CompoundStmt *CS = "
                                    "dyn_cast<CompoundStmt>(" +
                                    sName + ")"),
                             std::move(csBlk)));
    }
    EmitRaw(c0.get(), "ret = false; stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    EmitVar(c1.get(), "const IfStmt *", "IS", "dyn_cast<IfStmt>(f.S)");
    auto thenBlk = IRBuilder::block();
    EmitRaw(thenBlk.get(),
            "f.state = 4; stack.push_back({IS->getElse(), 0, false});");
    auto elseBlk = IRBuilder::block();
    EmitRaw(elseBlk.get(), "ret = false; stack.pop_back();");
    AddStmt(c1.get(), IRBuilder::if_(IRExpr("ret"), std::move(thenBlk),
                                     std::move(elseBlk)));
    AddStmt(c1.get(), IRBuilder::break_());
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    EmitVar(c2.get(), "const IfStmt *", "IS", "dyn_cast<IfStmt>(f.S)");
    EmitRaw(c2.get(), "if (!ret) { stack.pop_back(); break; }");
    EmitRaw(c2.get(), "f.saved = ret; f.state = 3; "
                      "stack.push_back({IS->getElse(), 0, false}); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    EmitRaw(c3.get(), "ret = f.saved && ret; stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  {
    auto c4 = IRBuilder::block();
    EmitRaw(c4.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"4"}, std::move(c4));
  }
  {
    auto c5 = IRBuilder::block();
    EmitRaw(c5.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"5"}, std::move(c5));
  }
  AddStmt(w.get(), std::move(sw));
  AddStmt(body.get(),
          IRBuilder::while_(IRExpr("!stack.empty()"), std::move(w)));
  AddStmt(body.get(), IRBuilder::ret(IRExpr("ret")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string sName = FD->getParamDecl(1)->getNameAsString();

  auto body = IRBuilder::block();
  AddStmt(body.get(), FrameStruct({{"const Expr *", "E"},
                                   {"const Stmt *", "S"},
                                   {"int", "state"}}));
  AddStmt(body.get(), IRBuilder::var("std::vector<Frame>", "stack"));
  EmitExpr(body.get(), "stack.push_back({" + eName + ", " + sName + ", 0})");
  EmitVar(body.get(), Ctx.RetType, "ret", "false");

  auto w = IRBuilder::block();
  EmitVar(w.get(), "Frame &", "f", "stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    EmitVar(c0.get(), "const Expr *", eName, "f.E");
    EmitVar(c0.get(), "const Stmt *", sName, "f.S");
    EmitRaw(c0.get(), "if (!" + eName + " || !" + sName +
                          ") { ret = false; stack.pop_back(); break; }");
    {
      auto rsBlk = IRBuilder::block();
      EmitExpr(rsBlk.get(), "ret = RS->getRetValue() == " + eName);
      EmitRaw(rsBlk.get(), "stack.pop_back(); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const ReturnStmt *RS = "
                                    "dyn_cast<ReturnStmt>(" +
                                    sName + ")"),
                             std::move(rsBlk)));
    }
    {
      auto esBlk = IRBuilder::block();
      EmitExpr(esBlk.get(), "ret = ExprS == " + eName);
      EmitRaw(esBlk.get(), "stack.pop_back(); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(
                  IRExpr("const Expr *ExprS = dyn_cast<Expr>(" + sName + ")"),
                  std::move(esBlk)));
    }
    {
      auto ifBlk = IRBuilder::block();
      EmitRaw(ifBlk.get(), "f.state = 1; stack.push_back({" + eName +
                               ", IfS->getThen(), 0}); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(
                  IRExpr("const IfStmt *IfS = dyn_cast<IfStmt>(" + sName + ")"),
                  std::move(ifBlk)));
    }
    {
      auto csBlk = IRBuilder::block();
      EmitRaw(csBlk.get(), "if (CS->body_empty()) { ret = false; "
                           "stack.pop_back(); break; }");
      EmitVar(csBlk.get(), "const Stmt *", "Last", "nullptr");
      AddStmt(csBlk.get(),
              IRBuilder::for_("const Stmt *Child : CS->body()", IRExpr(""), "",
                              IRBuilder::expr(IRExpr("Last = Child"))));
      EmitRaw(csBlk.get(), "f.state = 2; stack.push_back({" + eName +
                               ", Last, 0}); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const CompoundStmt *CS = "
                                    "dyn_cast<CompoundStmt>(" +
                                    sName + ")"),
                             std::move(csBlk)));
    }
    EmitRaw(c0.get(), "ret = false; stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    EmitRaw(c1.get(), "if (ret) { stack.pop_back(); break; }");
    EmitVar(c1.get(), "const IfStmt *", "IfS", "dyn_cast<IfStmt>(f.S)");
    EmitRaw(c1.get(),
            "f.state = 2; stack.push_back({f.E, IfS->getElse(), 0}); break;");
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    EmitRaw(c2.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  AddStmt(w.get(), std::move(sw));
  AddStmt(body.get(),
          IRBuilder::while_(IRExpr("!stack.empty()"), std::move(w)));
  AddStmt(body.get(), IRBuilder::ret(IRExpr("ret")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string funcName = FD->getParamDecl(1)->getNameAsString();

  auto body = IRBuilder::block();
  AddStmt(body.get(), FrameStruct({{"const Expr *", "E"},
                                   {"int", "state"},
                                   {"unsigned", "count"}}));
  AddStmt(body.get(), IRBuilder::var("std::vector<Frame>", "stack"));
  AddStmt(body.get(), IRBuilder::var("std::vector<bool>", "values"));
  EmitExpr(body.get(), "stack.push_back({" + eName + ", 0, 0})");

  auto w = IRBuilder::block();
  EmitVar(w.get(), "Frame &", "f", "stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    EmitVar(c0.get(), "const Expr *", eName, "f.E");
    EmitRaw(c0.get(), "if (!" + eName +
                          ") { values.push_back(true); stack.pop_back(); "
                          "break; }");
    EmitExpr(c0.get(), eName + " = " + eName + "->IgnoreParenImpCasts()");
    {
      auto ceBlk = IRBuilder::block();
      {
        auto calleeBlk = IRBuilder::block();
        {
          auto matchBlk = IRBuilder::block();
          EmitRaw(matchBlk.get(),
                  "values.push_back(true); stack.pop_back(); break;");
          AddStmt(calleeBlk.get(),
                  IRBuilder::if_(
                      IRExpr("Callee->getNameAsString() == " + funcName),
                      std::move(matchBlk)));
        }
        {
          auto pureBlk = IRBuilder::block();
          EmitVar(pureBlk.get(), "unsigned", "n", "CE->getNumArgs()");
          EmitExpr(pureBlk.get(), "f.count = n");
          EmitRaw(pureBlk.get(), "if (n == 0) { values.push_back(true); "
                                 "stack.pop_back(); break; }");
          EmitExpr(pureBlk.get(), "f.state = 1");
          AddStmt(pureBlk.get(),
                  IRBuilder::for_("int i = static_cast<int>(n) - 1",
                                  IRExpr("i >= 0"), "--i",
                                  IRBuilder::expr(IRExpr(
                                      "stack.push_back({CE->getArg(i), 0, 0})"))));
          AddStmt(pureBlk.get(), IRBuilder::break_());
          AddStmt(calleeBlk.get(),
                  IRBuilder::if_(IRExpr("IsKnownPureFunction("
                                        "Callee->getNameAsString())"),
                                 std::move(pureBlk)));
        }
        AddStmt(ceBlk.get(),
                IRBuilder::if_(IRExpr("const FunctionDecl *Callee = "
                                      "CE->getDirectCallee()"),
                               std::move(calleeBlk)));
      }
      EmitRaw(ceBlk.get(), "values.push_back(false); stack.pop_back(); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const CallExpr *CE = dyn_cast<CallExpr>(" +
                                    eName + ")"),
                             std::move(ceBlk)));
    }
    {
      auto boBlk = IRBuilder::block();
      {
        auto assignBlk = IRBuilder::block();
        EmitRaw(assignBlk.get(),
                "values.push_back(false); stack.pop_back(); break;");
        AddStmt(boBlk.get(),
                IRBuilder::if_(IRExpr("BO->isAssignmentOp() || "
                                      "BO->getOpcode() == BO_Comma"),
                               std::move(assignBlk)));
      }
      EmitExpr(boBlk.get(), "f.state = 2");
      EmitExpr(boBlk.get(), "stack.push_back({BO->getRHS(), 0, 0})");
      EmitExpr(boBlk.get(), "stack.push_back({BO->getLHS(), 0, 0})");
      AddStmt(boBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                    "dyn_cast<BinaryOperator>(" +
                                    eName + ")"),
                             std::move(boBlk)));
    }
    {
      auto uoBlk = IRBuilder::block();
      {
        auto incBlk = IRBuilder::block();
        EmitRaw(incBlk.get(),
                "values.push_back(false); stack.pop_back(); break;");
        AddStmt(uoBlk.get(),
                IRBuilder::if_(IRExpr("UO->isIncrementDecrementOp()"),
                               std::move(incBlk)));
      }
      EmitExpr(uoBlk.get(), "f.state = 3");
      EmitExpr(uoBlk.get(), "stack.push_back({UO->getSubExpr(), 0, 0})");
      AddStmt(uoBlk.get(), IRBuilder::break_());
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const UnaryOperator *UO = "
                                    "dyn_cast<UnaryOperator>(" +
                                    eName + ")"),
                             std::move(uoBlk)));
    }
    AddStmt(c0.get(),
            IRBuilder::var("std::vector<const Expr *>", "__cps_children"));
    {
      auto childLoopBlk = IRBuilder::block();
      AddStmt(childLoopBlk.get(),
              IRBuilder::if_(
                  IRExpr("const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)"),
                  IRBuilder::expr(IRExpr("__cps_children.push_back(ChildExpr)"))));
      AddStmt(c0.get(),
              IRBuilder::for_("const Stmt *Child : " + eName + "->children()",
                              IRExpr(""), "", std::move(childLoopBlk)));
    }
    EmitExpr(c0.get(),
             "f.count = static_cast<unsigned>(__cps_children.size())");
    EmitRaw(c0.get(), "if (f.count == 0) { values.push_back(true); "
                      "stack.pop_back(); break; }");
    EmitExpr(c0.get(), "f.state = 4");
    AddStmt(c0.get(),
            IRBuilder::for_("auto it = __cps_children.rbegin()",
                            IRExpr("it != __cps_children.rend()"), "++it",
                            IRBuilder::expr(IRExpr("stack.push_back({*it, 0, 0})"))));
    AddStmt(c0.get(), IRBuilder::break_());
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  for (const char *lbl : {"1", "4"}) {
    auto agg = IRBuilder::block();
    EmitVar(agg.get(), "bool", "res", "true");
    {
      auto loopBlk = IRBuilder::block();
      EmitRaw(loopBlk.get(), "bool v = values.back(); values.pop_back();");
      EmitExpr(loopBlk.get(), "res = res && v");
      AddStmt(agg.get(),
              IRBuilder::for_("unsigned i = 0", IRExpr("i < f.count"), "++i",
                              std::move(loopBlk)));
    }
    EmitExpr(agg.get(), "values.push_back(res)");
    EmitRaw(agg.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {lbl}, std::move(agg));
  }
  {
    auto c2 = IRBuilder::block();
    EmitRaw(c2.get(), "bool rhs = values.back(); values.pop_back();");
    EmitRaw(c2.get(), "bool lhs = values.back(); values.pop_back();");
    EmitExpr(c2.get(), "values.push_back(lhs && rhs)");
    EmitRaw(c2.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    EmitRaw(c3.get(), "bool v = values.back(); values.pop_back();");
    EmitExpr(c3.get(), "values.push_back(v)");
    EmitRaw(c3.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  AddStmt(w.get(), std::move(sw));
  AddStmt(body.get(),
          IRBuilder::while_(IRExpr("!stack.empty()"), std::move(w)));
  AddStmt(body.get(),
          IRBuilder::ret(IRExpr("values.empty() ? true : values.back()")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string sName = FD->getParamDecl(0)->getNameAsString();

  auto body = IRBuilder::block();
  AddStmt(body.get(), FrameStruct({{"const Stmt *", "S"}}));
  AddStmt(body.get(), IRBuilder::var("std::vector<Frame>", "stack"));
  EmitExpr(body.get(), "stack.push_back({" + sName + "})");

  auto w = IRBuilder::block();
  EmitRaw(w.get(), "Frame f = stack.back(); stack.pop_back();");
  AddStmt(w.get(), IRBuilder::if_(IRExpr("isa<ReturnStmt>(f.S)"),
                                  IRBuilder::ret(IRExpr("true"))));
  AddStmt(w.get(), IRBuilder::if_(IRExpr("isa<IfStmt>(f.S)"),
                                  IRBuilder::ret(IRExpr("true"))));
  AddStmt(w.get(), IRBuilder::if_(IRExpr("isa<SwitchStmt>(f.S)"),
                                  IRBuilder::ret(IRExpr("true"))));
  {
    auto csBlk = IRBuilder::block();
    AddStmt(csBlk.get(),
            IRBuilder::if_(
                IRExpr("CS->size() == 1"),
                IRBuilder::expr(
                    IRExpr("stack.push_back({CS->body_begin()[0]})")),
                IRBuilder::ret(IRExpr("false"))));
    auto elseBlk = IRBuilder::block();
    AddStmt(elseBlk.get(), IRBuilder::ret(IRExpr("false")));
    AddStmt(w.get(),
            IRBuilder::if_(IRExpr("const CompoundStmt *CS = "
                                  "dyn_cast<CompoundStmt>(f.S)"),
                           std::move(csBlk), std::move(elseBlk)));
  }
  AddStmt(body.get(),
          IRBuilder::while_(IRExpr("!stack.empty()"), std::move(w)));
  AddStmt(body.get(), IRBuilder::ret(IRExpr("false")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string sName = FD->getParamDecl(0)->getNameAsString();

  auto body = IRBuilder::block();
  auto w = IRBuilder::block();
  {
    auto csBlk = IRBuilder::block();
    AddStmt(csBlk.get(), IRBuilder::if_(IRExpr("CS->body_empty()"),
                                        IRBuilder::ret(IRExpr("nullptr"))));
    EmitVar(csBlk.get(), "const Stmt *", "Last", "nullptr");
    AddStmt(csBlk.get(),
            IRBuilder::for_("const Stmt *B : CS->body()", IRExpr(""), "",
                            IRBuilder::expr(IRExpr("Last = B"))));
    EmitExpr(csBlk.get(), sName + " = Last");
    AddStmt(csBlk.get(), IRBuilder::continue_());
    AddStmt(w.get(),
            IRBuilder::if_(IRExpr("const CompoundStmt *CS = "
                                  "dyn_cast<CompoundStmt>(" +
                                  sName + ")"),
                           std::move(csBlk)));
  }
  {
    auto ifBlk = IRBuilder::block();
    AddStmt(ifBlk.get(), IRBuilder::if_(IRExpr("IfS->getElse()"),
                                        IRBuilder::ret(IRExpr(sName))));
    EmitExpr(ifBlk.get(), sName + " = IfS->getThen()");
    AddStmt(ifBlk.get(), IRBuilder::continue_());
    AddStmt(w.get(),
            IRBuilder::if_(
                IRExpr("const IfStmt *IfS = dyn_cast<IfStmt>(" + sName + ")"),
                std::move(ifBlk)));
  }
  AddStmt(w.get(), IRBuilder::ret(IRExpr(sName)));
  AddStmt(body.get(), IRBuilder::while_(IRExpr("true"), std::move(w)));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string sName = FD->getParamDecl(0)->getNameAsString();
  std::string baName = FD->getParamDecl(1)->getNameAsString();
  std::string ctxName = FD->getParamDecl(2)->getNameAsString();

  auto body = IRBuilder::block();
  AddStmt(body.get(), FrameStruct({{"const Stmt *", "S"}}));
  AddStmt(body.get(), IRBuilder::var("std::vector<Frame>", "stack"));
  EmitExpr(body.get(), "stack.push_back({" + sName + "})");

  auto w = IRBuilder::block();
  EmitRaw(w.get(), "Frame f = stack.back(); stack.pop_back();");
  AddStmt(w.get(),
          IRBuilder::if_(IRExpr("!f.S"), IRBuilder::continue_()));
  {
    auto ifBlk = IRBuilder::block();
    EmitVar(ifBlk.get(), "const Expr *", "BaseExpr",
            "ExtractReturnExpr(IfS->getThen())");
    AddStmt(ifBlk.get(),
            IRBuilder::if_(
                IRExpr("BaseExpr"),
                IRBuilder::expr(IRExpr(
                    baName +
                    ".BaseCases.push_back(MakeBaseCase(IfS->getCond(), "
                    "BaseExpr, " +
                    ctxName + "))"))));
    AddStmt(ifBlk.get(),
            IRBuilder::if_(IRExpr("const Stmt *Else = IfS->getElse()"),
                           IRBuilder::expr(IRExpr("stack.push_back({Else})"))));
    AddStmt(ifBlk.get(), IRBuilder::continue_());
    AddStmt(w.get(),
            IRBuilder::if_(
                IRExpr("const IfStmt *IfS = dyn_cast<IfStmt>(f.S)"),
                std::move(ifBlk)));
  }
  {
    auto rsBlk = IRBuilder::block();
    EmitExpr(rsBlk.get(), baName + ".RecExpr = RS->getRetValue()");
    EmitExpr(rsBlk.get(), baName + ".IsRecursive = true");
    AddStmt(w.get(),
            IRBuilder::if_(IRExpr("const ReturnStmt *RS = "
                                  "dyn_cast<ReturnStmt>(f.S)"),
                           std::move(rsBlk)));
  }
  AddStmt(body.get(),
          IRBuilder::while_(IRExpr("!stack.empty()"), std::move(w)));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string pName = FD->getParamDecl(1)->getNameAsString();
  std::string vName = FD->getParamDecl(2)->getNameAsString();

  auto body = IRBuilder::block();
  AddStmt(body.get(), FrameStruct({{"const Expr *", "E"},
                                   {"int", "state"},
                                   {"EvalResult", "saved"}}));
  AddStmt(body.get(), IRBuilder::var("std::vector<Frame>", "stack"));
  EmitExpr(body.get(),
           "stack.push_back({" + eName + ", 0, EvalResult::Unknown})");
  EmitVar(body.get(), Ctx.RetType, "ret", "EvalResult::Unknown");

  auto w = IRBuilder::block();
  EmitVar(w.get(), "Frame &", "f", "stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    EmitVar(c0.get(), "const Expr *", eName, "f.E");
    EmitExpr(c0.get(), eName + " = " + eName + "->IgnoreParenImpCasts()");
    {
      auto boBlk = IRBuilder::block();
      {
        auto landBlk = IRBuilder::block();
        EmitRaw(landBlk.get(),
                "f.state = 1; stack.push_back({BO->getLHS(), 0, "
                "EvalResult::Unknown}); break;");
        AddStmt(boBlk.get(),
                IRBuilder::if_(IRExpr("BO->getOpcode() == BO_LAnd"),
                               std::move(landBlk)));
      }
      {
        auto lorBlk = IRBuilder::block();
        EmitRaw(lorBlk.get(),
                "f.state = 2; stack.push_back({BO->getLHS(), 0, "
                "EvalResult::Unknown}); break;");
        AddStmt(boBlk.get(),
                IRBuilder::if_(IRExpr("BO->getOpcode() == BO_LOr"),
                               std::move(lorBlk)));
      }
      EmitRaw(boBlk.get(), "int lhsVal = 0, rhsVal = 0;");
      EmitVar(boBlk.get(), "bool", "lhsKnown",
              "ExtractParamOrLiteral(BO->getLHS(), " + pName + ", " + vName +
                  ", lhsVal)");
      EmitVar(boBlk.get(), "bool", "rhsKnown",
              "ExtractParamOrLiteral(BO->getRHS(), " + pName + ", " + vName +
                  ", rhsVal)");
      EmitRaw(boBlk.get(), "if (!lhsKnown || !rhsKnown) { ret = "
                           "EvalResult::Unknown; stack.pop_back(); break; }");
      auto opSw = IRBuilder::switch_(IRExpr("BO->getOpcode()"));
      const std::pair<const char *, const char *> opCases[] = {
          {"BO_EQ", "=="}, {"BO_NE", "!="}, {"BO_LT", "<"},
          {"BO_GT", ">"},  {"BO_LE", "<="}, {"BO_GE", ">="},
      };
      for (const auto &oc : opCases) {
        auto opBlk = IRBuilder::block();
        EmitExpr(opBlk.get(), std::string("ret = (lhsVal ") + oc.second +
                                  " rhsVal) ? EvalResult::True : "
                                  "EvalResult::False");
        AddStmt(opBlk.get(), IRBuilder::break_());
        IRBuilder::case_(opSw.get(), {oc.first}, std::move(opBlk));
      }
      {
        auto defBlk = IRBuilder::block();
        EmitExpr(defBlk.get(), "ret = EvalResult::Unknown");
        AddStmt(defBlk.get(), IRBuilder::break_());
        IRBuilder::case_(opSw.get(), {}, std::move(defBlk));
      }
      AddStmt(boBlk.get(), std::move(opSw));
      EmitRaw(boBlk.get(), "stack.pop_back(); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                    "dyn_cast<BinaryOperator>(" +
                                    eName + ")"),
                             std::move(boBlk)));
    }
    {
      auto uoBlk = IRBuilder::block();
      {
        auto lnotBlk = IRBuilder::block();
        EmitRaw(lnotBlk.get(),
                "f.state = 3; stack.push_back({UO->getSubExpr(), 0, "
                "EvalResult::Unknown}); break;");
        AddStmt(uoBlk.get(),
                IRBuilder::if_(IRExpr("UO->getOpcode() == UO_LNot"),
                               std::move(lnotBlk)));
      }
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const UnaryOperator *UO = "
                                    "dyn_cast<UnaryOperator>(" +
                                    eName + ")"),
                             std::move(uoBlk)));
    }
    EmitRaw(c0.get(),
            "ret = EvalResult::Unknown; stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    EmitRaw(c1.get(),
            "if (ret == EvalResult::False) { stack.pop_back(); break; }");
    EmitRaw(c1.get(), "f.saved = ret; f.state = 4;");
    EmitVar(c1.get(), "const BinaryOperator *", "BO",
            "dyn_cast<BinaryOperator>(f.E)");
    EmitRaw(c1.get(),
            "stack.push_back({BO->getRHS(), 0, EvalResult::Unknown}); break;");
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    EmitRaw(c2.get(),
            "if (ret == EvalResult::True) { stack.pop_back(); break; }");
    EmitRaw(c2.get(), "f.saved = ret; f.state = 5;");
    EmitVar(c2.get(), "const BinaryOperator *", "BO",
            "dyn_cast<BinaryOperator>(f.E)");
    EmitRaw(c2.get(),
            "stack.push_back({BO->getRHS(), 0, EvalResult::Unknown}); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    AddStmt(c3.get(),
            IRBuilder::if_(
                IRExpr("ret == EvalResult::True"),
                IRBuilder::expr(IRExpr("ret = EvalResult::False")),
                IRBuilder::if_(
                    IRExpr("ret == EvalResult::False"),
                    IRBuilder::expr(IRExpr("ret = EvalResult::True")),
                    IRBuilder::expr(IRExpr("ret = EvalResult::Unknown")))));
    EmitRaw(c3.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  {
    auto c4 = IRBuilder::block();
    AddStmt(c4.get(),
            IRBuilder::if_(
                IRExpr("f.saved == EvalResult::False || "
                       "ret == EvalResult::False"),
                IRBuilder::expr(IRExpr("ret = EvalResult::False")),
                IRBuilder::if_(
                    IRExpr("f.saved == EvalResult::Unknown || "
                           "ret == EvalResult::Unknown"),
                    IRBuilder::expr(IRExpr("ret = EvalResult::Unknown")),
                    IRBuilder::expr(IRExpr("ret = EvalResult::True")))));
    EmitRaw(c4.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"4"}, std::move(c4));
  }
  {
    auto c5 = IRBuilder::block();
    AddStmt(c5.get(),
            IRBuilder::if_(
                IRExpr("f.saved == EvalResult::True || "
                       "ret == EvalResult::True"),
                IRBuilder::expr(IRExpr("ret = EvalResult::True")),
                IRBuilder::if_(
                    IRExpr("f.saved == EvalResult::Unknown || "
                           "ret == EvalResult::Unknown"),
                    IRBuilder::expr(IRExpr("ret = EvalResult::Unknown")),
                    IRBuilder::expr(IRExpr("ret = EvalResult::False")))));
    EmitRaw(c5.get(), "stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"5"}, std::move(c5));
  }
  AddStmt(w.get(), std::move(sw));
  AddStmt(body.get(),
          IRBuilder::while_(IRExpr("!stack.empty()"), std::move(w)));
  AddStmt(body.get(), IRBuilder::ret(IRExpr("ret")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder b;
  b.comment("=== Generated structural-recursion code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");
  b.include("algorithm");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  std::string eName = FD->getParamDecl(0)->getNameAsString();
  std::string funcName = FD->getParamDecl(1)->getNameAsString();
  std::string pName = FD->getParamDecl(2)->getNameAsString();
  std::string termsName = FD->getParamDecl(3)->getNameAsString();
  std::string maxName = FD->getParamDecl(4)->getNameAsString();

  auto body = IRBuilder::block();
  AddStmt(body.get(), FrameStruct({{"const Expr *", "E"},
                                   {"int", "state"},
                                   {"std::size_t", "terms_start"},
                                   {"std::size_t", "rhs_start"},
                                   {"int", "saved_max"}}));
  AddStmt(body.get(), IRBuilder::var("std::vector<Frame>", "stack"));
  EmitExpr(body.get(), "stack.push_back({" + eName + ", 0, " + termsName +
                           ".size(), 0, " + maxName + "})");
  EmitVar(body.get(), Ctx.RetType, "ret", "false");

  auto w = IRBuilder::block();
  EmitVar(w.get(), "Frame &", "f", "stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    EmitVar(c0.get(), "const Expr *", eName, "f.E");
    EmitExpr(c0.get(), eName + " = " + eName + "->IgnoreParenImpCasts()");
    {
      auto ceBlk = IRBuilder::block();
      {
        auto calleeBlk = IRBuilder::block();
        {
          auto matchBlk = IRBuilder::block();
          EmitRaw(matchBlk.get(), "if (CE->getNumArgs() != 1) { ret = false; "
                                  "stack.pop_back(); break; }");
          EmitVar(matchBlk.get(), "const Expr *", "Arg",
                  "CE->getArg(0)->IgnoreParenImpCasts()");
          EmitVar(matchBlk.get(), "const BinaryOperator *", "BO",
                  "dyn_cast<BinaryOperator>(Arg)");
          EmitRaw(matchBlk.get(), "if (!BO || BO->getOpcode() != BO_Sub) { "
                                  "ret = false; stack.pop_back(); break; }");
          EmitVar(matchBlk.get(), "const Expr *", "LHS",
                  "BO->getLHS()->IgnoreParenImpCasts()");
          EmitVar(matchBlk.get(), "const Expr *", "RHS",
                  "BO->getRHS()->IgnoreParenImpCasts()");
          EmitVar(matchBlk.get(), "const DeclRefExpr *", "DRE",
                  "dyn_cast<DeclRefExpr>(LHS)");
          EmitRaw(matchBlk.get(),
                  "if (!DRE || DRE->getDecl()->getNameAsString() != " + pName +
                      ") { ret = false; stack.pop_back(); break; }");
          EmitVar(matchBlk.get(), "const IntegerLiteral *", "IL",
                  "dyn_cast<IntegerLiteral>(RHS)");
          EmitRaw(matchBlk.get(),
                  "if (!IL) { ret = false; stack.pop_back(); break; }");
          EmitVar(matchBlk.get(), "int", "c",
                  "static_cast<int>(IL->getValue().getSExtValue())");
          EmitRaw(matchBlk.get(),
                  "if (c <= 0) { ret = false; stack.pop_back(); break; }");
          EmitExpr(matchBlk.get(), termsName + ".push_back({c, 1, "
                                               "const_cast<CallExpr *>(CE)})");
          EmitExpr(matchBlk.get(),
                   maxName + " = std::max(" + maxName + ", c)");
          EmitRaw(matchBlk.get(), "ret = true; stack.pop_back(); break;");
          AddStmt(calleeBlk.get(),
                  IRBuilder::if_(
                      IRExpr("Callee->getNameAsString() == " + funcName),
                      std::move(matchBlk)));
        }
        AddStmt(ceBlk.get(),
                IRBuilder::if_(IRExpr("const FunctionDecl *Callee = "
                                      "CE->getDirectCallee()"),
                               std::move(calleeBlk)));
      }
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const CallExpr *CE = dyn_cast<CallExpr>(" +
                                    eName + ")"),
                             std::move(ceBlk)));
    }
    {
      auto uoBlk = IRBuilder::block();
      {
        auto minusBlk = IRBuilder::block();
        EmitRaw(minusBlk.get(), "f.terms_start = " + termsName +
                                    ".size(); f.saved_max = " + maxName + ";");
        EmitRaw(minusBlk.get(), "f.state = 1; stack.push_back({UO->getSubExpr()"
                                ", 0, 0, 0, 0}); break;");
        AddStmt(uoBlk.get(),
                IRBuilder::if_(IRExpr("UO->getOpcode() == UO_Minus"),
                               std::move(minusBlk)));
      }
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const UnaryOperator *UO = "
                                    "dyn_cast<UnaryOperator>(" +
                                    eName + ")"),
                             std::move(uoBlk)));
    }
    {
      auto boBlk = IRBuilder::block();
      EmitRaw(boBlk.get(), "if (BO->getOpcode() != BO_Add && "
                           "BO->getOpcode() != BO_Sub) { ret = false; "
                           "stack.pop_back(); break; }");
      EmitRaw(boBlk.get(), "f.terms_start = " + termsName +
                               ".size(); f.saved_max = " + maxName + ";");
      EmitRaw(boBlk.get(), "f.state = 2; stack.push_back({BO->getLHS(), 0, 0, "
                           "0, 0}); break;");
      AddStmt(c0.get(),
              IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                    "dyn_cast<BinaryOperator>(" +
                                    eName + ")"),
                             std::move(boBlk)));
    }
    EmitRaw(c0.get(), "ret = false; stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    EmitRaw(c1.get(), "if (!ret) { stack.pop_back(); break; }");
    AddStmt(c1.get(),
            IRBuilder::for_("std::size_t i = f.terms_start",
                            IRExpr("i < " + termsName + ".size()"), "++i",
                            IRBuilder::expr(IRExpr(termsName +
                                                   "[i].Sign = -" + termsName +
                                                   "[i].Sign"))));
    EmitExpr(c1.get(), maxName + " = std::max(f.saved_max, " + maxName + ")");
    EmitRaw(c1.get(), "ret = true; stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    EmitRaw(c2.get(), "if (!ret) { stack.pop_back(); break; }");
    EmitExpr(c2.get(), "f.rhs_start = " + termsName + ".size()");
    EmitVar(c2.get(), "const BinaryOperator *", "BO",
            "dyn_cast<BinaryOperator>(f.E)");
    EmitRaw(c2.get(), "f.state = 3; stack.push_back({BO->getRHS(), 0, 0, 0, "
                      "0}); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    EmitRaw(c3.get(), "if (!ret) { stack.pop_back(); break; }");
    EmitVar(c3.get(), "const BinaryOperator *", "BO",
            "dyn_cast<BinaryOperator>(f.E)");
    {
      auto subBlk = IRBuilder::block();
      AddStmt(subBlk.get(),
              IRBuilder::for_("std::size_t i = f.rhs_start",
                              IRExpr("i < " + termsName + ".size()"), "++i",
                              IRBuilder::expr(IRExpr(termsName +
                                                     "[i].Sign = -" +
                                                     termsName + "[i].Sign"))));
      AddStmt(c3.get(),
              IRBuilder::if_(IRExpr("BO->getOpcode() == BO_Sub"),
                             std::move(subBlk)));
    }
    EmitExpr(c3.get(),
             maxName + " = std::max({f.saved_max, " + maxName + "})");
    EmitRaw(c3.get(), "ret = true; stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  AddStmt(w.get(), std::move(sw));
  AddStmt(body.get(),
          IRBuilder::while_(IRExpr("!stack.empty()"), std::move(w)));
  AddStmt(body.get(), IRBuilder::ret(IRExpr("ret")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

int ParseLinearTermsRule::cost() const {
  return RuleCatalog::ParseLinearTerms.Cost;
}

const char *ParseLinearTermsRule::name() const {
  return RuleCatalog::ParseLinearTerms.Name;
}

} // namespace cps
