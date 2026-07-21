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

// Build a local `struct __cps_Frame { ... };` definition from (type, name) fields.
std::unique_ptr<IRLocalStruct>
FrameStruct(std::vector<std::pair<std::string, std::string>> fields) {
  IRStructData data;
  data.name = "__cps_Frame";
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
  IRBuilder::add(body.get(), FrameStruct({{"const Stmt *", "S"},
                                   {"int", "state"},
                                   {"bool", "saved"}}));
  IRBuilder::add(body.get(), IRBuilder::var("std::vector<__cps_Frame>", "__cps_stack"));
  IRBuilder::addExpr(body.get(), "__cps_stack.push_back({" + sName + ", 0, false})");
  IRBuilder::addVar(body.get(), Ctx.RetType, "__cps_ret", "false");

  auto w = IRBuilder::block();
  IRBuilder::addVar(w.get(), "__cps_Frame &", "f", "__cps_stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    IRBuilder::addVar(c0.get(), "const Stmt *", sName, "f.S");
    IRBuilder::addRaw(c0.get(),
            "if (!" + sName + ") { __cps_ret = false; __cps_stack.pop_back(); break; }");
    {
      auto rsBlk = IRBuilder::block();
      IRBuilder::addVar(rsBlk.get(), "const Expr *", "E", "RS->getRetValue()");
      {
        auto ceBlk = IRBuilder::block();
        {
          auto calleeBlk = IRBuilder::block();
          IRBuilder::addExpr(calleeBlk.get(), "__cps_ret = Callee->getNameAsString() == " +
                                        targetName + "->getNameAsString()");
          IRBuilder::addExpr(calleeBlk.get(), "__cps_stack.pop_back()");
          IRBuilder::add(calleeBlk.get(), IRBuilder::break_());
          IRBuilder::add(ceBlk.get(),
                  IRBuilder::if_(IRExpr("const FunctionDecl *Callee = "
                                        "CE->getDirectCallee()"),
                                 std::move(calleeBlk)));
        }
        IRBuilder::add(rsBlk.get(),
                IRBuilder::if_(
                    IRExpr("const CallExpr *CE = dyn_cast<CallExpr>(E)"),
                    std::move(ceBlk)));
      }
      IRBuilder::addRaw(rsBlk.get(), "__cps_ret = false; __cps_stack.pop_back(); break;");
      IRBuilder::add(c0.get(),
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
            IRBuilder::addRaw(opBlk.get(), "f.state = 1; __cps_stack.push_back({IS->getThen(), "
                                 "0, false}); break;");
            IRBuilder::add(boBlk.get(),
                    IRBuilder::if_(IRExpr("BO->getOpcode() == BO_LAnd || "
                                          "BO->getOpcode() == BO_LOr"),
                                   std::move(opBlk)));
          }
          IRBuilder::add(condBlk.get(),
                  IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                        "dyn_cast<BinaryOperator>(Cond)"),
                                 std::move(boBlk)));
        }
        IRBuilder::add(isBlk.get(),
                IRBuilder::if_(IRExpr("const Expr *Cond = IS->getCond()"),
                               std::move(condBlk)));
      }
      IRBuilder::addRaw(isBlk.get(), "f.state = 2; __cps_stack.push_back({IS->getThen(), 0, "
                           "false}); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(
                  IRExpr("const IfStmt *IS = dyn_cast<IfStmt>(" + sName + ")"),
                  std::move(isBlk)));
    }
    {
      auto csBlk = IRBuilder::block();
      IRBuilder::addRaw(csBlk.get(), "if (CS->body_empty()) { __cps_ret = false; "
                           "__cps_stack.pop_back(); break; }");
      IRBuilder::addRaw(csBlk.get(), "f.state = 5; __cps_stack.push_back({CS->body_back(), 0, "
                           "false}); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const CompoundStmt *CS = "
                                    "dyn_cast<CompoundStmt>(" +
                                    sName + ")"),
                             std::move(csBlk)));
    }
    IRBuilder::addRaw(c0.get(), "__cps_ret = false; __cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    IRBuilder::addVar(c1.get(), "const IfStmt *", "IS", "dyn_cast<IfStmt>(f.S)");
    auto thenBlk = IRBuilder::block();
    IRBuilder::addRaw(thenBlk.get(),
            "f.state = 4; __cps_stack.push_back({IS->getElse(), 0, false});");
    auto elseBlk = IRBuilder::block();
    IRBuilder::addRaw(elseBlk.get(), "__cps_ret = false; __cps_stack.pop_back();");
    IRBuilder::add(c1.get(), IRBuilder::if_(IRExpr("__cps_ret"), std::move(thenBlk),
                                     std::move(elseBlk)));
    IRBuilder::add(c1.get(), IRBuilder::break_());
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    IRBuilder::addVar(c2.get(), "const IfStmt *", "IS", "dyn_cast<IfStmt>(f.S)");
    IRBuilder::addRaw(c2.get(), "if (!__cps_ret) { __cps_stack.pop_back(); break; }");
    IRBuilder::addRaw(c2.get(), "f.saved = __cps_ret; f.state = 3; "
                      "__cps_stack.push_back({IS->getElse(), 0, false}); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    IRBuilder::addRaw(c3.get(), "__cps_ret = f.saved && __cps_ret; __cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  {
    auto c4 = IRBuilder::block();
    IRBuilder::addRaw(c4.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"4"}, std::move(c4));
  }
  {
    auto c5 = IRBuilder::block();
    IRBuilder::addRaw(c5.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"5"}, std::move(c5));
  }
  IRBuilder::add(w.get(), std::move(sw));
  IRBuilder::add(body.get(),
          IRBuilder::while_(IRExpr("!__cps_stack.empty()"), std::move(w)));
  IRBuilder::add(body.get(), IRBuilder::ret(IRExpr("__cps_ret")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder::add(body.get(), FrameStruct({{"const Expr *", "E"},
                                   {"const Stmt *", "S"},
                                   {"int", "state"}}));
  IRBuilder::add(body.get(), IRBuilder::var("std::vector<__cps_Frame>", "__cps_stack"));
  IRBuilder::addExpr(body.get(), "__cps_stack.push_back({" + eName + ", " + sName + ", 0})");
  IRBuilder::addVar(body.get(), Ctx.RetType, "__cps_ret", "false");

  auto w = IRBuilder::block();
  IRBuilder::addVar(w.get(), "__cps_Frame &", "f", "__cps_stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    IRBuilder::addVar(c0.get(), "const Expr *", eName, "f.E");
    IRBuilder::addVar(c0.get(), "const Stmt *", sName, "f.S");
    IRBuilder::addRaw(c0.get(), "if (!" + eName + " || !" + sName +
                          ") { __cps_ret = false; __cps_stack.pop_back(); break; }");
    {
      auto rsBlk = IRBuilder::block();
      IRBuilder::addExpr(rsBlk.get(), "__cps_ret = RS->getRetValue() == " + eName);
      IRBuilder::addRaw(rsBlk.get(), "__cps_stack.pop_back(); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const ReturnStmt *RS = "
                                    "dyn_cast<ReturnStmt>(" +
                                    sName + ")"),
                             std::move(rsBlk)));
    }
    {
      auto esBlk = IRBuilder::block();
      IRBuilder::addExpr(esBlk.get(), "__cps_ret = ExprS == " + eName);
      IRBuilder::addRaw(esBlk.get(), "__cps_stack.pop_back(); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(
                  IRExpr("const Expr *ExprS = dyn_cast<Expr>(" + sName + ")"),
                  std::move(esBlk)));
    }
    {
      auto ifBlk = IRBuilder::block();
      IRBuilder::addRaw(ifBlk.get(), "f.state = 1; __cps_stack.push_back({" + eName +
                               ", IfS->getThen(), 0}); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(
                  IRExpr("const IfStmt *IfS = dyn_cast<IfStmt>(" + sName + ")"),
                  std::move(ifBlk)));
    }
    {
      auto csBlk = IRBuilder::block();
      IRBuilder::addRaw(csBlk.get(), "if (CS->body_empty()) { __cps_ret = false; "
                           "__cps_stack.pop_back(); break; }");
      IRBuilder::addVar(csBlk.get(), "const Stmt *", "Last", "nullptr");
      IRBuilder::add(csBlk.get(),
              IRBuilder::for_("const Stmt *Child : CS->body()", IRExpr(""), "",
                              IRBuilder::expr(IRExpr("Last = Child"))));
      IRBuilder::addRaw(csBlk.get(), "f.state = 2; __cps_stack.push_back({" + eName +
                               ", Last, 0}); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const CompoundStmt *CS = "
                                    "dyn_cast<CompoundStmt>(" +
                                    sName + ")"),
                             std::move(csBlk)));
    }
    IRBuilder::addRaw(c0.get(), "__cps_ret = false; __cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    IRBuilder::addRaw(c1.get(), "if (__cps_ret) { __cps_stack.pop_back(); break; }");
    IRBuilder::addVar(c1.get(), "const IfStmt *", "IfS", "dyn_cast<IfStmt>(f.S)");
    IRBuilder::addRaw(c1.get(),
            "f.state = 2; __cps_stack.push_back({f.E, IfS->getElse(), 0}); break;");
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    IRBuilder::addRaw(c2.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  IRBuilder::add(w.get(), std::move(sw));
  IRBuilder::add(body.get(),
          IRBuilder::while_(IRExpr("!__cps_stack.empty()"), std::move(w)));
  IRBuilder::add(body.get(), IRBuilder::ret(IRExpr("__cps_ret")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder::add(body.get(), FrameStruct({{"const Expr *", "E"},
                                   {"int", "state"},
                                   {"unsigned", "count"}}));
  IRBuilder::add(body.get(), IRBuilder::var("std::vector<__cps_Frame>", "__cps_stack"));
  IRBuilder::add(body.get(), IRBuilder::var("std::vector<bool>", "__cps_values"));
  IRBuilder::addExpr(body.get(), "__cps_stack.push_back({" + eName + ", 0, 0})");

  auto w = IRBuilder::block();
  IRBuilder::addVar(w.get(), "__cps_Frame &", "f", "__cps_stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    IRBuilder::addVar(c0.get(), "const Expr *", eName, "f.E");
    IRBuilder::addRaw(c0.get(), "if (!" + eName +
                          ") { __cps_values.push_back(true); __cps_stack.pop_back(); "
                          "break; }");
    IRBuilder::addExpr(c0.get(), eName + " = " + eName + "->IgnoreParenImpCasts()");
    {
      auto ceBlk = IRBuilder::block();
      {
        auto calleeBlk = IRBuilder::block();
        {
          auto matchBlk = IRBuilder::block();
          IRBuilder::addRaw(matchBlk.get(),
                  "__cps_values.push_back(true); __cps_stack.pop_back(); break;");
          IRBuilder::add(calleeBlk.get(),
                  IRBuilder::if_(
                      IRExpr("Callee->getNameAsString() == " + funcName),
                      std::move(matchBlk)));
        }
        {
          auto pureBlk = IRBuilder::block();
          IRBuilder::addVar(pureBlk.get(), "unsigned", "n", "CE->getNumArgs()");
          IRBuilder::addExpr(pureBlk.get(), "f.count = n");
          IRBuilder::addRaw(pureBlk.get(), "if (n == 0) { __cps_values.push_back(true); "
                                 "__cps_stack.pop_back(); break; }");
          IRBuilder::addExpr(pureBlk.get(), "f.state = 1");
          IRBuilder::add(pureBlk.get(),
                  IRBuilder::for_("int i = static_cast<int>(n) - 1",
                                  IRExpr("i >= 0"), "--i",
                                  IRBuilder::expr(IRExpr(
                                      "__cps_stack.push_back({CE->getArg(i), 0, 0})"))));
          IRBuilder::add(pureBlk.get(), IRBuilder::break_());
          IRBuilder::add(calleeBlk.get(),
                  IRBuilder::if_(IRExpr("IsKnownPureFunction("
                                        "Callee->getNameAsString())"),
                                 std::move(pureBlk)));
        }
        IRBuilder::add(ceBlk.get(),
                IRBuilder::if_(IRExpr("const FunctionDecl *Callee = "
                                      "CE->getDirectCallee()"),
                               std::move(calleeBlk)));
      }
      IRBuilder::addRaw(ceBlk.get(), "__cps_values.push_back(false); __cps_stack.pop_back(); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const CallExpr *CE = dyn_cast<CallExpr>(" +
                                    eName + ")"),
                             std::move(ceBlk)));
    }
    {
      auto boBlk = IRBuilder::block();
      {
        auto assignBlk = IRBuilder::block();
        IRBuilder::addRaw(assignBlk.get(),
                "__cps_values.push_back(false); __cps_stack.pop_back(); break;");
        IRBuilder::add(boBlk.get(),
                IRBuilder::if_(IRExpr("BO->isAssignmentOp() || "
                                      "BO->getOpcode() == BO_Comma"),
                               std::move(assignBlk)));
      }
      IRBuilder::addExpr(boBlk.get(), "f.state = 2");
      IRBuilder::addExpr(boBlk.get(), "__cps_stack.push_back({BO->getRHS(), 0, 0})");
      IRBuilder::addExpr(boBlk.get(), "__cps_stack.push_back({BO->getLHS(), 0, 0})");
      IRBuilder::add(boBlk.get(), IRBuilder::break_());
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                    "dyn_cast<BinaryOperator>(" +
                                    eName + ")"),
                             std::move(boBlk)));
    }
    {
      auto uoBlk = IRBuilder::block();
      {
        auto incBlk = IRBuilder::block();
        IRBuilder::addRaw(incBlk.get(),
                "__cps_values.push_back(false); __cps_stack.pop_back(); break;");
        IRBuilder::add(uoBlk.get(),
                IRBuilder::if_(IRExpr("UO->isIncrementDecrementOp()"),
                               std::move(incBlk)));
      }
      IRBuilder::addExpr(uoBlk.get(), "f.state = 3");
      IRBuilder::addExpr(uoBlk.get(), "__cps_stack.push_back({UO->getSubExpr(), 0, 0})");
      IRBuilder::add(uoBlk.get(), IRBuilder::break_());
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const UnaryOperator *UO = "
                                    "dyn_cast<UnaryOperator>(" +
                                    eName + ")"),
                             std::move(uoBlk)));
    }
    IRBuilder::add(c0.get(),
            IRBuilder::var("std::vector<const Expr *>", "__cps_children"));
    {
      auto childLoopBlk = IRBuilder::block();
      IRBuilder::add(childLoopBlk.get(),
              IRBuilder::if_(
                  IRExpr("const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)"),
                  IRBuilder::expr(IRExpr("__cps_children.push_back(ChildExpr)"))));
      IRBuilder::add(c0.get(),
              IRBuilder::for_("const Stmt *Child : " + eName + "->children()",
                              IRExpr(""), "", std::move(childLoopBlk)));
    }
    IRBuilder::addExpr(c0.get(),
             "f.count = static_cast<unsigned>(__cps_children.size())");
    IRBuilder::addRaw(c0.get(), "if (f.count == 0) { __cps_values.push_back(true); "
                      "__cps_stack.pop_back(); break; }");
    IRBuilder::addExpr(c0.get(), "f.state = 4");
    IRBuilder::add(c0.get(),
            IRBuilder::for_("auto it = __cps_children.rbegin()",
                            IRExpr("it != __cps_children.rend()"), "++it",
                            IRBuilder::expr(IRExpr("__cps_stack.push_back({*it, 0, 0})"))));
    IRBuilder::add(c0.get(), IRBuilder::break_());
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  for (const char *lbl : {"1", "4"}) {
    auto agg = IRBuilder::block();
    IRBuilder::addVar(agg.get(), "bool", "res", "true");
    {
      auto loopBlk = IRBuilder::block();
      IRBuilder::addRaw(loopBlk.get(), "bool v = __cps_values.back(); __cps_values.pop_back();");
      IRBuilder::addExpr(loopBlk.get(), "res = res && v");
      IRBuilder::add(agg.get(),
              IRBuilder::for_("unsigned i = 0", IRExpr("i < f.count"), "++i",
                              std::move(loopBlk)));
    }
    IRBuilder::addExpr(agg.get(), "__cps_values.push_back(res)");
    IRBuilder::addRaw(agg.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {lbl}, std::move(agg));
  }
  {
    auto c2 = IRBuilder::block();
    IRBuilder::addRaw(c2.get(), "bool rhs = __cps_values.back(); __cps_values.pop_back();");
    IRBuilder::addRaw(c2.get(), "bool lhs = __cps_values.back(); __cps_values.pop_back();");
    IRBuilder::addExpr(c2.get(), "__cps_values.push_back(lhs && rhs)");
    IRBuilder::addRaw(c2.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    IRBuilder::addRaw(c3.get(), "bool v = __cps_values.back(); __cps_values.pop_back();");
    IRBuilder::addExpr(c3.get(), "__cps_values.push_back(v)");
    IRBuilder::addRaw(c3.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  IRBuilder::add(w.get(), std::move(sw));
  IRBuilder::add(body.get(),
          IRBuilder::while_(IRExpr("!__cps_stack.empty()"), std::move(w)));
  IRBuilder::add(body.get(),
          IRBuilder::ret(IRExpr("__cps_values.empty() ? true : __cps_values.back()")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder::add(body.get(), FrameStruct({{"const Stmt *", "S"}}));
  IRBuilder::add(body.get(), IRBuilder::var("std::vector<__cps_Frame>", "__cps_stack"));
  IRBuilder::addExpr(body.get(), "__cps_stack.push_back({" + sName + "})");

  auto w = IRBuilder::block();
  IRBuilder::addRaw(w.get(), "__cps_Frame f = __cps_stack.back(); __cps_stack.pop_back();");
  IRBuilder::add(w.get(), IRBuilder::if_(IRExpr("isa<ReturnStmt>(f.S)"),
                                  IRBuilder::ret(IRExpr("true"))));
  IRBuilder::add(w.get(), IRBuilder::if_(IRExpr("isa<IfStmt>(f.S)"),
                                  IRBuilder::ret(IRExpr("true"))));
  IRBuilder::add(w.get(), IRBuilder::if_(IRExpr("isa<SwitchStmt>(f.S)"),
                                  IRBuilder::ret(IRExpr("true"))));
  {
    auto csBlk = IRBuilder::block();
    IRBuilder::add(csBlk.get(),
            IRBuilder::if_(
                IRExpr("CS->size() == 1"),
                IRBuilder::expr(
                    IRExpr("__cps_stack.push_back({CS->body_begin()[0]})")),
                IRBuilder::ret(IRExpr("false"))));
    auto elseBlk = IRBuilder::block();
    IRBuilder::add(elseBlk.get(), IRBuilder::ret(IRExpr("false")));
    IRBuilder::add(w.get(),
            IRBuilder::if_(IRExpr("const CompoundStmt *CS = "
                                  "dyn_cast<CompoundStmt>(f.S)"),
                           std::move(csBlk), std::move(elseBlk)));
  }
  IRBuilder::add(body.get(),
          IRBuilder::while_(IRExpr("!__cps_stack.empty()"), std::move(w)));
  IRBuilder::add(body.get(), IRBuilder::ret(IRExpr("false")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
    IRBuilder::add(csBlk.get(), IRBuilder::if_(IRExpr("CS->body_empty()"),
                                        IRBuilder::ret(IRExpr("nullptr"))));
    IRBuilder::addVar(csBlk.get(), "const Stmt *", "Last", "nullptr");
    IRBuilder::add(csBlk.get(),
            IRBuilder::for_("const Stmt *B : CS->body()", IRExpr(""), "",
                            IRBuilder::expr(IRExpr("Last = B"))));
    IRBuilder::addExpr(csBlk.get(), sName + " = Last");
    IRBuilder::add(csBlk.get(), IRBuilder::continue_());
    IRBuilder::add(w.get(),
            IRBuilder::if_(IRExpr("const CompoundStmt *CS = "
                                  "dyn_cast<CompoundStmt>(" +
                                  sName + ")"),
                           std::move(csBlk)));
  }
  {
    auto ifBlk = IRBuilder::block();
    IRBuilder::add(ifBlk.get(), IRBuilder::if_(IRExpr("IfS->getElse()"),
                                        IRBuilder::ret(IRExpr(sName))));
    IRBuilder::addExpr(ifBlk.get(), sName + " = IfS->getThen()");
    IRBuilder::add(ifBlk.get(), IRBuilder::continue_());
    IRBuilder::add(w.get(),
            IRBuilder::if_(
                IRExpr("const IfStmt *IfS = dyn_cast<IfStmt>(" + sName + ")"),
                std::move(ifBlk)));
  }
  IRBuilder::add(w.get(), IRBuilder::ret(IRExpr(sName)));
  IRBuilder::add(body.get(), IRBuilder::while_(IRExpr("true"), std::move(w)));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder::add(body.get(), FrameStruct({{"const Stmt *", "S"}}));
  IRBuilder::add(body.get(), IRBuilder::var("std::vector<__cps_Frame>", "__cps_stack"));
  IRBuilder::addExpr(body.get(), "__cps_stack.push_back({" + sName + "})");

  auto w = IRBuilder::block();
  IRBuilder::addRaw(w.get(), "__cps_Frame f = __cps_stack.back(); __cps_stack.pop_back();");
  IRBuilder::add(w.get(),
          IRBuilder::if_(IRExpr("!f.S"), IRBuilder::continue_()));
  {
    auto ifBlk = IRBuilder::block();
    IRBuilder::addVar(ifBlk.get(), "const Expr *", "BaseExpr",
            "ExtractReturnExpr(IfS->getThen())");
    IRBuilder::add(ifBlk.get(),
            IRBuilder::if_(
                IRExpr("BaseExpr"),
                IRBuilder::expr(IRExpr(
                    baName +
                    ".BaseCases.push_back(MakeBaseCase(IfS->getCond(), "
                    "BaseExpr, " +
                    ctxName + "))"))));
    IRBuilder::add(ifBlk.get(),
            IRBuilder::if_(IRExpr("const Stmt *Else = IfS->getElse()"),
                           IRBuilder::expr(IRExpr("__cps_stack.push_back({Else})"))));
    IRBuilder::add(ifBlk.get(), IRBuilder::continue_());
    IRBuilder::add(w.get(),
            IRBuilder::if_(
                IRExpr("const IfStmt *IfS = dyn_cast<IfStmt>(f.S)"),
                std::move(ifBlk)));
  }
  {
    auto rsBlk = IRBuilder::block();
    IRBuilder::addExpr(rsBlk.get(), baName + ".RecExpr = RS->getRetValue()");
    IRBuilder::addExpr(rsBlk.get(), baName + ".IsRecursive = true");
    IRBuilder::add(w.get(),
            IRBuilder::if_(IRExpr("const ReturnStmt *RS = "
                                  "dyn_cast<ReturnStmt>(f.S)"),
                           std::move(rsBlk)));
  }
  IRBuilder::add(body.get(),
          IRBuilder::while_(IRExpr("!__cps_stack.empty()"), std::move(w)));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder::add(body.get(), FrameStruct({{"const Expr *", "E"},
                                   {"int", "state"},
                                   {"EvalResult", "saved"}}));
  IRBuilder::add(body.get(), IRBuilder::var("std::vector<__cps_Frame>", "__cps_stack"));
  IRBuilder::addExpr(body.get(),
           "__cps_stack.push_back({" + eName + ", 0, EvalResult::Unknown})");
  IRBuilder::addVar(body.get(), Ctx.RetType, "__cps_ret", "EvalResult::Unknown");

  auto w = IRBuilder::block();
  IRBuilder::addVar(w.get(), "__cps_Frame &", "f", "__cps_stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    IRBuilder::addVar(c0.get(), "const Expr *", eName, "f.E");
    IRBuilder::addExpr(c0.get(), eName + " = " + eName + "->IgnoreParenImpCasts()");
    {
      auto boBlk = IRBuilder::block();
      {
        auto landBlk = IRBuilder::block();
        IRBuilder::addRaw(landBlk.get(),
                "f.state = 1; __cps_stack.push_back({BO->getLHS(), 0, "
                "EvalResult::Unknown}); break;");
        IRBuilder::add(boBlk.get(),
                IRBuilder::if_(IRExpr("BO->getOpcode() == BO_LAnd"),
                               std::move(landBlk)));
      }
      {
        auto lorBlk = IRBuilder::block();
        IRBuilder::addRaw(lorBlk.get(),
                "f.state = 2; __cps_stack.push_back({BO->getLHS(), 0, "
                "EvalResult::Unknown}); break;");
        IRBuilder::add(boBlk.get(),
                IRBuilder::if_(IRExpr("BO->getOpcode() == BO_LOr"),
                               std::move(lorBlk)));
      }
      IRBuilder::addRaw(boBlk.get(), "int lhsVal = 0, rhsVal = 0;");
      IRBuilder::addVar(boBlk.get(), "bool", "lhsKnown",
              "ExtractParamOrLiteral(BO->getLHS(), " + pName + ", " + vName +
                  ", lhsVal)");
      IRBuilder::addVar(boBlk.get(), "bool", "rhsKnown",
              "ExtractParamOrLiteral(BO->getRHS(), " + pName + ", " + vName +
                  ", rhsVal)");
      IRBuilder::addRaw(boBlk.get(), "if (!lhsKnown || !rhsKnown) { __cps_ret = "
                           "EvalResult::Unknown; __cps_stack.pop_back(); break; }");
      auto opSw = IRBuilder::switch_(IRExpr("BO->getOpcode()"));
      const std::pair<const char *, const char *> opCases[] = {
          {"BO_EQ", "=="}, {"BO_NE", "!="}, {"BO_LT", "<"},
          {"BO_GT", ">"},  {"BO_LE", "<="}, {"BO_GE", ">="},
      };
      for (const auto &oc : opCases) {
        auto opBlk = IRBuilder::block();
        IRBuilder::addExpr(opBlk.get(), std::string("__cps_ret = (lhsVal ") + oc.second +
                                  " rhsVal) ? EvalResult::True : "
                                  "EvalResult::False");
        IRBuilder::add(opBlk.get(), IRBuilder::break_());
        IRBuilder::case_(opSw.get(), {oc.first}, std::move(opBlk));
      }
      {
        auto defBlk = IRBuilder::block();
        IRBuilder::addExpr(defBlk.get(), "__cps_ret = EvalResult::Unknown");
        IRBuilder::add(defBlk.get(), IRBuilder::break_());
        IRBuilder::case_(opSw.get(), {}, std::move(defBlk));
      }
      IRBuilder::add(boBlk.get(), std::move(opSw));
      IRBuilder::addRaw(boBlk.get(), "__cps_stack.pop_back(); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                    "dyn_cast<BinaryOperator>(" +
                                    eName + ")"),
                             std::move(boBlk)));
    }
    {
      auto uoBlk = IRBuilder::block();
      {
        auto lnotBlk = IRBuilder::block();
        IRBuilder::addRaw(lnotBlk.get(),
                "f.state = 3; __cps_stack.push_back({UO->getSubExpr(), 0, "
                "EvalResult::Unknown}); break;");
        IRBuilder::add(uoBlk.get(),
                IRBuilder::if_(IRExpr("UO->getOpcode() == UO_LNot"),
                               std::move(lnotBlk)));
      }
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const UnaryOperator *UO = "
                                    "dyn_cast<UnaryOperator>(" +
                                    eName + ")"),
                             std::move(uoBlk)));
    }
    IRBuilder::addRaw(c0.get(),
            "__cps_ret = EvalResult::Unknown; __cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    IRBuilder::addRaw(c1.get(),
            "if (__cps_ret == EvalResult::False) { __cps_stack.pop_back(); break; }");
    IRBuilder::addRaw(c1.get(), "f.saved = __cps_ret; f.state = 4;");
    IRBuilder::addVar(c1.get(), "const BinaryOperator *", "BO",
            "dyn_cast<BinaryOperator>(f.E)");
    IRBuilder::addRaw(c1.get(),
            "__cps_stack.push_back({BO->getRHS(), 0, EvalResult::Unknown}); break;");
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    IRBuilder::addRaw(c2.get(),
            "if (__cps_ret == EvalResult::True) { __cps_stack.pop_back(); break; }");
    IRBuilder::addRaw(c2.get(), "f.saved = __cps_ret; f.state = 5;");
    IRBuilder::addVar(c2.get(), "const BinaryOperator *", "BO",
            "dyn_cast<BinaryOperator>(f.E)");
    IRBuilder::addRaw(c2.get(),
            "__cps_stack.push_back({BO->getRHS(), 0, EvalResult::Unknown}); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    IRBuilder::add(c3.get(),
            IRBuilder::if_(
                IRExpr("__cps_ret == EvalResult::True"),
                IRBuilder::expr(IRExpr("__cps_ret = EvalResult::False")),
                IRBuilder::if_(
                    IRExpr("__cps_ret == EvalResult::False"),
                    IRBuilder::expr(IRExpr("__cps_ret = EvalResult::True")),
                    IRBuilder::expr(IRExpr("__cps_ret = EvalResult::Unknown")))));
    IRBuilder::addRaw(c3.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  {
    auto c4 = IRBuilder::block();
    IRBuilder::add(c4.get(),
            IRBuilder::if_(
                IRExpr("f.saved == EvalResult::False || "
                       "__cps_ret == EvalResult::False"),
                IRBuilder::expr(IRExpr("__cps_ret = EvalResult::False")),
                IRBuilder::if_(
                    IRExpr("f.saved == EvalResult::Unknown || "
                           "__cps_ret == EvalResult::Unknown"),
                    IRBuilder::expr(IRExpr("__cps_ret = EvalResult::Unknown")),
                    IRBuilder::expr(IRExpr("__cps_ret = EvalResult::True")))));
    IRBuilder::addRaw(c4.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"4"}, std::move(c4));
  }
  {
    auto c5 = IRBuilder::block();
    IRBuilder::add(c5.get(),
            IRBuilder::if_(
                IRExpr("f.saved == EvalResult::True || "
                       "__cps_ret == EvalResult::True"),
                IRBuilder::expr(IRExpr("__cps_ret = EvalResult::True")),
                IRBuilder::if_(
                    IRExpr("f.saved == EvalResult::Unknown || "
                           "__cps_ret == EvalResult::Unknown"),
                    IRBuilder::expr(IRExpr("__cps_ret = EvalResult::Unknown")),
                    IRBuilder::expr(IRExpr("__cps_ret = EvalResult::False")))));
    IRBuilder::addRaw(c5.get(), "__cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"5"}, std::move(c5));
  }
  IRBuilder::add(w.get(), std::move(sw));
  IRBuilder::add(body.get(),
          IRBuilder::while_(IRExpr("!__cps_stack.empty()"), std::move(w)));
  IRBuilder::add(body.get(), IRBuilder::ret(IRExpr("__cps_ret")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
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
  IRBuilder::add(body.get(), FrameStruct({{"const Expr *", "E"},
                                   {"int", "state"},
                                   {"std::size_t", "terms_start"},
                                   {"std::size_t", "rhs_start"},
                                   {"int", "saved_max"}}));
  IRBuilder::add(body.get(), IRBuilder::var("std::vector<__cps_Frame>", "__cps_stack"));
  IRBuilder::addExpr(body.get(), "__cps_stack.push_back({" + eName + ", 0, " + termsName +
                           ".size(), 0, " + maxName + "})");
  IRBuilder::addVar(body.get(), Ctx.RetType, "__cps_ret", "false");

  auto w = IRBuilder::block();
  IRBuilder::addVar(w.get(), "__cps_Frame &", "f", "__cps_stack.back()");
  auto sw = IRBuilder::switch_(IRExpr("f.state"));
  {
    auto c0 = IRBuilder::block();
    IRBuilder::addVar(c0.get(), "const Expr *", eName, "f.E");
    IRBuilder::addExpr(c0.get(), eName + " = " + eName + "->IgnoreParenImpCasts()");
    {
      auto ceBlk = IRBuilder::block();
      {
        auto calleeBlk = IRBuilder::block();
        {
          auto matchBlk = IRBuilder::block();
          IRBuilder::addRaw(matchBlk.get(), "if (CE->getNumArgs() != 1) { __cps_ret = false; "
                                  "__cps_stack.pop_back(); break; }");
          IRBuilder::addVar(matchBlk.get(), "const Expr *", "Arg",
                  "CE->getArg(0)->IgnoreParenImpCasts()");
          IRBuilder::addVar(matchBlk.get(), "const BinaryOperator *", "BO",
                  "dyn_cast<BinaryOperator>(Arg)");
          IRBuilder::addRaw(matchBlk.get(), "if (!BO || BO->getOpcode() != BO_Sub) { "
                                  "__cps_ret = false; __cps_stack.pop_back(); break; }");
          IRBuilder::addVar(matchBlk.get(), "const Expr *", "LHS",
                  "BO->getLHS()->IgnoreParenImpCasts()");
          IRBuilder::addVar(matchBlk.get(), "const Expr *", "RHS",
                  "BO->getRHS()->IgnoreParenImpCasts()");
          IRBuilder::addVar(matchBlk.get(), "const DeclRefExpr *", "DRE",
                  "dyn_cast<DeclRefExpr>(LHS)");
          IRBuilder::addRaw(matchBlk.get(),
                  "if (!DRE || DRE->getDecl()->getNameAsString() != " + pName +
                      ") { __cps_ret = false; __cps_stack.pop_back(); break; }");
          IRBuilder::addVar(matchBlk.get(), "const IntegerLiteral *", "IL",
                  "dyn_cast<IntegerLiteral>(RHS)");
          IRBuilder::addRaw(matchBlk.get(),
                  "if (!IL) { __cps_ret = false; __cps_stack.pop_back(); break; }");
          IRBuilder::addVar(matchBlk.get(), "int", "c",
                  "static_cast<int>(IL->getValue().getSExtValue())");
          IRBuilder::addRaw(matchBlk.get(),
                  "if (c <= 0) { __cps_ret = false; __cps_stack.pop_back(); break; }");
          IRBuilder::addExpr(matchBlk.get(), termsName + ".push_back({c, 1, "
                                               "const_cast<CallExpr *>(CE)})");
          IRBuilder::addExpr(matchBlk.get(),
                   maxName + " = std::max(" + maxName + ", c)");
          IRBuilder::addRaw(matchBlk.get(), "__cps_ret = true; __cps_stack.pop_back(); break;");
          IRBuilder::add(calleeBlk.get(),
                  IRBuilder::if_(
                      IRExpr("Callee->getNameAsString() == " + funcName),
                      std::move(matchBlk)));
        }
        IRBuilder::add(ceBlk.get(),
                IRBuilder::if_(IRExpr("const FunctionDecl *Callee = "
                                      "CE->getDirectCallee()"),
                               std::move(calleeBlk)));
      }
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const CallExpr *CE = dyn_cast<CallExpr>(" +
                                    eName + ")"),
                             std::move(ceBlk)));
    }
    {
      auto uoBlk = IRBuilder::block();
      {
        auto minusBlk = IRBuilder::block();
        IRBuilder::addRaw(minusBlk.get(), "f.terms_start = " + termsName +
                                    ".size(); f.saved_max = " + maxName + ";");
        IRBuilder::addRaw(minusBlk.get(), "f.state = 1; __cps_stack.push_back({UO->getSubExpr()"
                                ", 0, 0, 0, 0}); break;");
        IRBuilder::add(uoBlk.get(),
                IRBuilder::if_(IRExpr("UO->getOpcode() == UO_Minus"),
                               std::move(minusBlk)));
      }
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const UnaryOperator *UO = "
                                    "dyn_cast<UnaryOperator>(" +
                                    eName + ")"),
                             std::move(uoBlk)));
    }
    {
      auto boBlk = IRBuilder::block();
      IRBuilder::addRaw(boBlk.get(), "if (BO->getOpcode() != BO_Add && "
                           "BO->getOpcode() != BO_Sub) { __cps_ret = false; "
                           "__cps_stack.pop_back(); break; }");
      IRBuilder::addRaw(boBlk.get(), "f.terms_start = " + termsName +
                               ".size(); f.saved_max = " + maxName + ";");
      IRBuilder::addRaw(boBlk.get(), "f.state = 2; __cps_stack.push_back({BO->getLHS(), 0, 0, "
                           "0, 0}); break;");
      IRBuilder::add(c0.get(),
              IRBuilder::if_(IRExpr("const BinaryOperator *BO = "
                                    "dyn_cast<BinaryOperator>(" +
                                    eName + ")"),
                             std::move(boBlk)));
    }
    IRBuilder::addRaw(c0.get(), "__cps_ret = false; __cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"0"}, std::move(c0));
  }
  {
    auto c1 = IRBuilder::block();
    IRBuilder::addRaw(c1.get(), "if (!__cps_ret) { __cps_stack.pop_back(); break; }");
    IRBuilder::add(c1.get(),
            IRBuilder::for_("std::size_t i = f.terms_start",
                            IRExpr("i < " + termsName + ".size()"), "++i",
                            IRBuilder::expr(IRExpr(termsName +
                                                   "[i].Sign = -" + termsName +
                                                   "[i].Sign"))));
    IRBuilder::addExpr(c1.get(), maxName + " = std::max(f.saved_max, " + maxName + ")");
    IRBuilder::addRaw(c1.get(), "__cps_ret = true; __cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"1"}, std::move(c1));
  }
  {
    auto c2 = IRBuilder::block();
    IRBuilder::addRaw(c2.get(), "if (!__cps_ret) { __cps_stack.pop_back(); break; }");
    IRBuilder::addExpr(c2.get(), "f.rhs_start = " + termsName + ".size()");
    IRBuilder::addVar(c2.get(), "const BinaryOperator *", "BO",
            "dyn_cast<BinaryOperator>(f.E)");
    IRBuilder::addRaw(c2.get(), "f.state = 3; __cps_stack.push_back({BO->getRHS(), 0, 0, 0, "
                      "0}); break;");
    IRBuilder::case_(sw.get(), {"2"}, std::move(c2));
  }
  {
    auto c3 = IRBuilder::block();
    IRBuilder::addRaw(c3.get(), "if (!__cps_ret) { __cps_stack.pop_back(); break; }");
    IRBuilder::addVar(c3.get(), "const BinaryOperator *", "BO",
            "dyn_cast<BinaryOperator>(f.E)");
    {
      auto subBlk = IRBuilder::block();
      IRBuilder::add(subBlk.get(),
              IRBuilder::for_("std::size_t i = f.rhs_start",
                              IRExpr("i < " + termsName + ".size()"), "++i",
                              IRBuilder::expr(IRExpr(termsName +
                                                     "[i].Sign = -" +
                                                     termsName + "[i].Sign"))));
      IRBuilder::add(c3.get(),
              IRBuilder::if_(IRExpr("BO->getOpcode() == BO_Sub"),
                             std::move(subBlk)));
    }
    IRBuilder::addExpr(c3.get(),
             maxName + " = std::max({f.saved_max, " + maxName + "})");
    IRBuilder::addRaw(c3.get(), "__cps_ret = true; __cps_stack.pop_back(); break;");
    IRBuilder::case_(sw.get(), {"3"}, std::move(c3));
  }
  IRBuilder::add(w.get(), std::move(sw));
  IRBuilder::add(body.get(),
          IRBuilder::while_(IRExpr("!__cps_stack.empty()"), std::move(w)));
  IRBuilder::add(body.get(), IRBuilder::ret(IRExpr("__cps_ret")));
  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
