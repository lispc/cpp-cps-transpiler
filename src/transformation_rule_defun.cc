#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace cps {

using namespace clang;

namespace {

struct DefunClosureInfo {
  std::string Name;
  bool NeedsSavedArg = false;
};

// Build the IRStructData for the argument struct (all params + a default
// constructor that zero-initialises them).
IRStructData BuildArgStructData(const FunctionDecl *FD,
                                const GenContext &Ctx) {
  IRStructData data;
  data.name = Ctx.ArgType;
  std::vector<IRCtorParam> ctorParams;
  std::vector<std::pair<std::string, std::string>> ctorInit;
  std::vector<std::pair<std::string, std::string>> defaultInit;
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    std::string pName = FD->getParamDecl(i)->getNameAsString();
    std::string pType = GetParamStorageType(FD->getParamDecl(i));
    data.fields.emplace_back(pType, pName);
    ctorParams.emplace_back(pType, pName);
    ctorInit.emplace_back(pName, pName);
    defaultInit.emplace_back(pName, "0");
  }
  data.ctors.emplace_back(std::move(ctorParams), std::move(ctorInit));
  data.ctors.emplace_back(std::vector<IRCtorParam>{}, std::move(defaultInit));
  return data;
}

} // anonymous namespace

bool DefunctionalizedRule::applies(const FunctionDecl *FD,
                                   const BodyAnalysis &BA,
                                   const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  if (!BA.IsRecursive)
    return false;
  // The final recursive expression must actually contain a recursive call.
  // AnalyzeBody may mark a function recursive even when the extracted RecExpr
  // is just a constant (e.g. helper functions with recursive if-returns).
  return ContainsRecursiveCall(BA.RecExpr, Ctx.FuncName);
}

CpsResult DefunctionalizedRule::apply(const FunctionDecl *FD,
                                        const BodyAnalysis &BA,
                                        GenContext &Ctx) const {
  const Expr *RecExpr = BA.RecExpr;

  IRBuilder b;
  b.comment("=== Generated defunctionalized code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  // Arg struct.
  b.structDef(BuildArgStructData(FD, Ctx));

  std::vector<CallExpr *> holes;
  CollectHolesDeep(RecExpr, Ctx.FuncName, holes);

  if (holes.size() == 1 && holes[0] == RecExpr->IgnoreParenImpCasts()) {
    auto body = IRBuilder::block();
    IRBuilder::add(body.get(),
                   IRBuilder::var(Ctx.ArgType, "arg",
                                  IRExpr(ArgCtorDefun(
                                      std::vector<std::string>(
                                          Ctx.ParamNames.size(), "0"),
                                      Ctx))));
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      IRBuilder::add(body.get(),
                     IRBuilder::expr(IRExpr(
                         "arg." + Ctx.ParamNames[i] + " = " +
                         FD->getParamDecl(i)->getNameAsString())));
    }

    auto loopBody = IRBuilder::block();
    EmitUnpacksDefun(loopBody.get(), "arg", Ctx);
    EmitStmtsToIR(b, loopBody.get(), BA.LeadingStmts, Ctx.ASTCtx);
    for (const auto &bc : BA.BaseCases) {
      IRBuilder::add(loopBody.get(),
                     IRBuilder::if_(IRExpr(bc.CondStr),
                                    IRBuilder::ret(IRExpr(bc.ValueStr))));
    }
    EmitStmtsToIR(b, loopBody.get(), BA.MiddleStmts, Ctx.ASTCtx);
    if (const CallExpr *RecCall = dyn_cast<CallExpr>(RecExpr)) {
      for (unsigned i = 0;
           i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
        std::string pName = FD->getParamDecl(i)->getNameAsString();
        IRBuilder::add(loopBody.get(),
                       IRBuilder::expr(IRExpr(
                           "arg." + pName + " = " +
                           PrintExpr(RecCall->getArg(i), Ctx.ASTCtx))));
      }
    }
    IRBuilder::add(body.get(),
                   IRBuilder::while_(IRExpr("1"), std::move(loopBody)));
    b.function(sig, std::move(body));
    return PrintGeneratedUnit(b.unit);
  }

  std::vector<DefunClosureInfo> closures;
  for (size_t i = 0; i < holes.size(); ++i) {
    closures.push_back(
        {"K" + std::to_string(i),
         NeedsSavedArg(RecExpr, holes, i, Ctx.ParamDeclSet)});
  }

  // Continuation tag enum.
  std::string contName = Ctx.FuncName + "Cont";
  std::vector<std::string> enumerators = {"Done"};
  for (const auto &c : closures)
    enumerators.push_back(c.Name);
  b.enumDef(contName, enumerators);

  // Frame struct.
  {
    std::string frameName = Ctx.FuncName + "Frame";
    IRStructData frame;
    frame.name = frameName;
    frame.fields.emplace_back(contName, "tag");
    frame.fields.emplace_back("std::vector<" + Ctx.RetType + ">", "vals");
    frame.fields.emplace_back("bool", "has_arg");
    frame.fields.emplace_back(Ctx.ArgType, "saved_arg");
    {
      std::vector<IRCtorParam> params;
      params.emplace_back(contName, "t");
      frame.ctors.emplace_back(std::move(params),
                               std::vector<std::pair<std::string, std::string>>{
                                   {"tag", "t"}, {"has_arg", "false"}});
    }
    {
      std::vector<IRCtorParam> params;
      params.emplace_back(contName, "t");
      params.emplace_back("std::vector<" + Ctx.RetType + ">", "v");
      frame.ctors.emplace_back(std::move(params),
                               std::vector<std::pair<std::string, std::string>>{
                                   {"tag", "t"},
                                   {"vals", "std::move(v)"},
                                   {"has_arg", "false"}});
    }
    {
      std::vector<IRCtorParam> params;
      params.emplace_back(contName, "t");
      params.emplace_back("std::vector<" + Ctx.RetType + ">", "v");
      params.emplace_back(Ctx.ArgType, "a");
      frame.ctors.emplace_back(std::move(params),
                               std::vector<std::pair<std::string, std::string>>{
                                   {"tag", "t"},
                                   {"vals", "std::move(v)"},
                                   {"has_arg", "true"},
                                   {"saved_arg", "a"}});
    }
    b.structDef(std::move(frame));
  }

  auto body = IRBuilder::block();
  IRBuilder::add(body.get(),
                 IRBuilder::var("std::vector<" + Ctx.FuncName + "Frame>",
                                "__cps_k"));
  IRBuilder::add(body.get(),
                 IRBuilder::expr(IRExpr("__cps_k.emplace_back(" + contName +
                                        "::Done)")));
  {
    std::vector<std::string> argDefaults;
    for (unsigned i = 0; i < FD->getNumParams(); ++i)
      argDefaults.push_back(
          GetDefaultValueForType(FD->getParamDecl(i)->getType()));
    IRBuilder::add(body.get(),
                   IRBuilder::var(Ctx.ArgType, "arg",
                                  IRExpr(ArgCtorDefun(argDefaults, Ctx))));
  }
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    IRBuilder::add(body.get(),
                   IRBuilder::expr(IRExpr(
                       "arg." + Ctx.ParamNames[i] + " = " +
                       FD->getParamDecl(i)->getNameAsString())));
  }
  IRBuilder::add(body.get(),
                 IRBuilder::var(Ctx.RetType, "val",
                                IRExpr(GetDefaultValueForType(
                                    FD->getReturnType()))));
  IRBuilder::add(body.get(), IRBuilder::label("dispatch"));

  auto loopBody = IRBuilder::block();
  EmitUnpacksDefun(loopBody.get(), "arg", Ctx);
  EmitStmtsToIR(b, loopBody.get(), BA.LeadingStmts, Ctx.ASTCtx);

  // else branch: middle statements, push the first continuation frame,
  // re-argument, and re-dispatch.
  auto elseBlk = IRBuilder::block();
  EmitStmtsToIR(b, elseBlk.get(), BA.MiddleStmts, Ctx.ASTCtx);
  {
    std::string push = "__cps_k.emplace_back(" + contName + "::K0";
    if (closures[0].NeedsSavedArg)
      push += ", std::vector<" + Ctx.RetType + ">{}, arg";
    else
      push += ", std::vector<" + Ctx.RetType + ">{}";
    push += ")";
    IRBuilder::add(elseBlk.get(), IRBuilder::expr(IRExpr(push)));
    std::vector<std::string> newParams;
    for (unsigned a = 0;
         a < FD->getNumParams() && a < holes[0]->getNumArgs(); ++a)
      newParams.push_back(PrintExpr(holes[0]->getArg(a), Ctx.ASTCtx));
    IRBuilder::add(elseBlk.get(),
                   IRBuilder::expr(IRExpr("arg = " +
                                          ArgCtorDefun(newParams, Ctx))));
    IRBuilder::add(elseBlk.get(), IRBuilder::goto_("dispatch"));
  }
  {
    std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>> branches;
    for (const auto &bc : BA.BaseCases) {
      auto thenBlk = IRBuilder::block();
      IRBuilder::add(thenBlk.get(),
                     IRBuilder::expr(IRExpr("val = " + bc.ValueStr)));
      branches.emplace_back(bc.CondStr, std::move(thenBlk));
    }
    IRBuilder::add(loopBody.get(),
                   IRBuilder::ifChain(std::move(branches),
                                      std::move(elseBlk)));
  }

  // Continuation machine.
  auto drainBody = IRBuilder::block();
  IRBuilder::add(drainBody.get(),
                 IRBuilder::var("auto", "f", IRExpr("__cps_k.back()")));
  IRBuilder::add(drainBody.get(), IRBuilder::expr(IRExpr("__cps_k.pop_back()")));
  auto sw = IRBuilder::switch_(IRExpr("f.tag"));
  {
    auto doneBlk = IRBuilder::block();
    IRBuilder::add(doneBlk.get(), IRBuilder::ret(IRExpr("val")));
    IRBuilder::case_(sw.get(), {contName + "::Done"}, std::move(doneBlk));
  }
  for (size_t i = 0; i < closures.size(); ++i) {
    const auto &cls = closures[i];
    auto caseBlk = IRBuilder::block();
    if (i == closures.size() - 1) {
      std::unordered_map<const Expr *, std::string> repls;
      for (size_t j = 0; j < holes.size(); ++j)
        repls[holes[j]] =
            (j == i) ? "val" : "f.vals[" + std::to_string(j) + "]";
      std::string finalExpr =
          StripOuterParens(PrintExprWithReplacements(RecExpr, repls, Ctx.ASTCtx));
      auto used = ParamsUsedInCode(finalExpr, Ctx.ParamNames);
      EmitTargetedUnpacks(caseBlk.get(), "f.saved_arg", used);
      // Avoid redundant "val = val;" when the recursive call itself is the
      // entire expression.
      if (finalExpr != "val")
        IRBuilder::add(caseBlk.get(),
                       IRBuilder::expr(IRExpr("val = " + finalExpr)));
      IRBuilder::add(caseBlk.get(), IRBuilder::break_());
    } else {
      std::vector<std::string> captured;
      for (size_t j = 0; j < i; ++j)
        captured.push_back("f.vals[" + std::to_string(j) + "]");
      captured.push_back("val");
      std::string push = "__cps_k.emplace_back(" + contName + "::K" +
                         std::to_string(i + 1) + ", ";
      push += "std::vector<" + Ctx.RetType + ">{";
      for (size_t j = 0; j < captured.size(); ++j) {
        if (j > 0)
          push += ", ";
        push += captured[j];
      }
      push += "}";
      if (closures[i + 1].NeedsSavedArg)
        push += ", f.saved_arg";
      push += ")";

      std::unordered_map<const Expr *, std::string> argRepls;
      for (size_t j = 0; j <= i; ++j)
        argRepls[holes[j]] =
            (j == i) ? "val" : "f.vals[" + std::to_string(j) + "]";
      std::vector<std::string> newParams;
      for (unsigned a = 0;
           a < FD->getNumParams() && a < holes[i + 1]->getNumArgs(); ++a)
        newParams.push_back(PrintExprWithReplacements(
            holes[i + 1]->getArg(a), argRepls, Ctx.ASTCtx));
      std::string argUpdate = "arg = " + ArgCtorDefun(newParams, Ctx);

      std::string codeToCheck = push + " " + argUpdate + ";";
      auto used = ParamsUsedInCode(codeToCheck, Ctx.ParamNames);
      EmitTargetedUnpacks(caseBlk.get(), "f.saved_arg", used);

      IRBuilder::add(caseBlk.get(), IRBuilder::expr(IRExpr(push)));
      IRBuilder::add(caseBlk.get(), IRBuilder::expr(IRExpr(argUpdate)));
      IRBuilder::add(caseBlk.get(), IRBuilder::goto_("dispatch"));
    }
    IRBuilder::case_(sw.get(), {contName + "::" + cls.Name},
                     std::move(caseBlk));
  }
  IRBuilder::add(drainBody.get(), std::move(sw));

  IRBuilder::add(loopBody.get(),
                 IRBuilder::while_(IRExpr("!__cps_k.empty()"),
                                   std::move(drainBody)));
  IRBuilder::add(loopBody.get(), IRBuilder::ret(IRExpr("val")));

  IRBuilder::add(body.get(),
                 IRBuilder::while_(IRExpr("1"), std::move(loopBody)));
  b.function(sig, std::move(body));

  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
