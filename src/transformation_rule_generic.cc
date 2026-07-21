#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "stack_machine_codegen.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace cps {

using namespace clang;

bool GenericStackRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                               const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
  if (holes.empty())
    return false;
  // GenericStackRule cannot handle recursive calls inside a hole's arguments
  // (nested recursion); leave those to DefunctionalizedRule.
  for (CallExpr *CE : holes) {
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (ContainsRecursiveCall(CE->getArg(i), Ctx.FuncName))
        return false;
    }
  }
  return true;
}

CpsResult GenericStackRule::apply(const FunctionDecl *FD,
                                    const BodyAnalysis &BA,
                                    GenContext &Ctx) const {
  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);

  bool needsAlgorithm = false;
  std::string combinedExpr;
  if (const CallExpr *CE = dyn_cast<CallExpr>(BA.RecExpr)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      std::string name = Callee->getNameAsString();
      if ((name == "min" || name == "max") && CE->getNumArgs() == 2) {
        needsAlgorithm = true;
        std::unordered_map<const Expr *, std::string> repls;
        for (size_t i = 0; i < holes.size(); ++i)
          repls[holes[i]] = "v" + std::to_string(i);
        std::string a0 =
            StripOuterParens(PrintExprWithReplacements(CE->getArg(0), repls,
                                                        Ctx.ASTCtx));
        std::string a1 =
            StripOuterParens(PrintExprWithReplacements(CE->getArg(1), repls,
                                                        Ctx.ASTCtx));
        combinedExpr = "std::" + name + "(" + a0 + ", " + a1 + ")";
      }
    }
  }
  if (combinedExpr.empty()) {
    std::unordered_map<const Expr *, std::string> repls;
    for (size_t i = 0; i < holes.size(); ++i)
      repls[holes[i]] = "v" + std::to_string(i);
    combinedExpr = StripOuterParens(
        PrintExprWithReplacements(BA.RecExpr, repls, Ctx.ASTCtx));
  }

  // Capture local variables declared in leading/middle statements that are
  // referenced by the combine expression. Leading-statement locals are
  // initialized once at function entry; middle-statement locals are computed
  // inside each frame branch and captured when pushing the combine marker.
  std::vector<const VarDecl *> allLocals;
  CollectLocalVarDecls(BA.LeadingStmts, allLocals);
  CollectLocalVarDecls(BA.MiddleStmts, allLocals);
  std::vector<const VarDecl *> leadingCaptured;
  std::vector<const VarDecl *> middleCaptured;
  for (const VarDecl *VD : allLocals) {
    if (ExprContainsDeclRefOutsideHoles(BA.RecExpr, VD, holes)) {
      if (IsLocalFromStmts(VD, BA.LeadingStmts))
        leadingCaptured.push_back(VD);
      else
        middleCaptured.push_back(VD);
    }
  }
  std::vector<const VarDecl *> capturedLocals = leadingCaptured;
  capturedLocals.insert(capturedLocals.end(), middleCaptured.begin(),
                        middleCaptured.end());

  IRBuilder b;
  StackMachineCodegen smg(Ctx.FuncName, Ctx.RetType);
  if (needsAlgorithm)
    smg.addInclude("algorithm");

  smg.emitBanner(b, "generic-stack", Ctx.FuncName);
  smg.emitIncludes(b);

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  for (unsigned i = 0; i < FD->getNumParams(); ++i)
    smg.addParam(FD->getParamDecl(i));
  for (const VarDecl *VD : capturedLocals)
    smg.addLocal(VD);

  smg.emitFrameStruct(b);
  smg.emitStackEntryStruct(b);

  auto buildFrameArgs = [&](const std::vector<std::string> &paramValues,
                            const std::vector<const VarDecl *> &localValues) {
    std::string s;
    for (const auto &pv : paramValues) {
      if (!s.empty())
        s += ", ";
      s += pv;
    }
    for (const VarDecl *VD : capturedLocals) {
      if (!s.empty())
        s += ", ";
      if (std::find(localValues.begin(), localValues.end(), VD) !=
          localValues.end())
        s += VD->getNameAsString();
      else
        s += GetDefaultValueForType(VD->getType());
    }
    return s;
  };

  std::vector<std::string> paramNames;
  for (unsigned i = 0; i < FD->getNumParams(); ++i)
    paramNames.push_back(FD->getParamDecl(i)->getNameAsString());

  auto body = IRBuilder::block();
  smg.emitStackDecl(body.get());
  smg.emitValuesDecl(body.get());

  // Leading statements are executed once with the original parameters and
  // initialise any captured locals that are shared by all frames.
  EmitStmtsToIR(b, body.get(), BA.LeadingStmts, Ctx.ASTCtx);

  IRBuilder::add(body.get(),
                 IRBuilder::expr(IRExpr(
                     smg.stackName() + ".emplace_back(" + smg.frameName() +
                     "(" + buildFrameArgs(paramNames, leadingCaptured) +
                     "))")));

  smg.emitLoop(
      body.get(),
      [&](IRBlock *iw) {
        // Marker branch: pop the child values and push the combined result.
        for (size_t i = 0; i < holes.size(); ++i) {
          IRBuilder::add(iw, IRBuilder::var(Ctx.RetType,
                                            "v" + std::to_string(i),
                                            IRExpr(smg.valuesName() +
                                                   ".back()")));
          IRBuilder::add(iw,
                         IRBuilder::expr(IRExpr(smg.valuesName() +
                                                ".pop_back()")));
        }
        IRBuilder::add(iw, IRBuilder::expr(IRExpr(smg.valuesName() +
                                                  ".push_back(" +
                                                  combinedExpr + ")")));
      },
      [&](IRBlock *iw) {
        // Frame branch: base-case chain, else push marker + child frames.
        auto elseBlk = IRBuilder::block();
        EmitStmtsToIR(b, elseBlk.get(), BA.MiddleStmts, Ctx.ASTCtx);
        IRBuilder::add(elseBlk.get(),
                       IRBuilder::expr(IRExpr(
                           smg.stackName() + ".emplace_back(" +
                           smg.entryName() + "(" +
                           std::to_string(holes.size()) + ", " +
                           smg.frameName() + "(" +
                           buildFrameArgs(paramNames, capturedLocals) +
                           ")))")));
        for (size_t i = 0; i < holes.size(); ++i) {
          std::vector<std::string> childParams;
          for (unsigned a = 0;
               a < FD->getNumParams() && a < holes[i]->getNumArgs(); ++a)
            childParams.push_back(PrintExpr(holes[i]->getArg(a), Ctx.ASTCtx));
          IRBuilder::add(elseBlk.get(),
                         IRBuilder::expr(IRExpr(
                             smg.stackName() + ".emplace_back(" +
                             smg.frameName() + "(" +
                             buildFrameArgs(childParams, {}) + "))")));
        }

        std::unique_ptr<IRStmt> chain = std::move(elseBlk);
        for (size_t bi = BA.BaseCases.size(); bi-- > 0;) {
          const auto &bc = BA.BaseCases[bi];
          auto thenBlk = IRBuilder::block();
          IRBuilder::add(thenBlk.get(),
                         IRBuilder::expr(IRExpr(smg.valuesName() +
                                                ".push_back(" + bc.ValueStr +
                                                ")")));
          chain = IRBuilder::if_(IRExpr(bc.CondStr), std::move(thenBlk),
                                 std::move(chain));
        }
        IRBuilder::add(iw, std::move(chain));
      });

  IRBuilder::add(body.get(),
                 IRBuilder::ret(IRExpr(smg.valuesName() + ".back()")));
  b.function(sig, std::move(body));

  return PrintGeneratedUnit(b.unit);
}

int GenericStackRule::cost() const { return RuleCatalog::GenericStack.Cost; }

const char *GenericStackRule::name() const {
  return RuleCatalog::GenericStack.Name;
}

} // namespace cps
