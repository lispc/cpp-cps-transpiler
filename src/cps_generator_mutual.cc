#include "cps_generator.h"
#include "output_ir.h"
#include "stack_machine_codegen.h"
#include "transformation_rules_helpers.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace clang;

namespace cps {

namespace {

std::string CommonPrefix(const std::vector<std::string> &Names) {
  if (Names.empty())
    return "";
  std::string prefix = Names[0];
  for (size_t i = 1; i < Names.size(); ++i) {
    size_t len = 0;
    while (len < prefix.size() && len < Names[i].size() &&
           prefix[len] == Names[i][len])
      ++len;
    prefix.resize(len);
  }
  // Trim trailing underscore if present for nicer naming.
  if (!prefix.empty() && prefix.back() == '_')
    prefix.pop_back();
  return prefix;
}

// Build a Decl* -> new-name replacement map for the parameters of FD.
std::unordered_map<const ValueDecl *, std::string>
BuildParamRenameMap(const FunctionDecl *FD,
                    const std::vector<std::string> &NewNames) {
  std::unordered_map<const ValueDecl *, std::string> repls;
  for (unsigned i = 0; i < FD->getNumParams() && i < NewNames.size(); ++i) {
    std::string Old = FD->getParamDecl(i)->getNameAsString();
    if (Old != NewNames[i])
      repls[FD->getParamDecl(i)] = NewNames[i];
  }
  return repls;
}

// AST-level parameter rename for an expression.
std::string RenameParams(const Expr *E, const FunctionDecl *FD,
                         const std::vector<std::string> &NewNames,
                         const ASTContext *Ctx) {
  return PrintExprWithDeclReplacements(E, BuildParamRenameMap(FD, NewNames),
                                       Ctx);
}

// Legacy string-based rename for statement source text.  Used only when we
// must preserve a full statement (e.g. a local variable declaration) that is
// not yet represented by a single AST expression.
std::string RenameParams(const std::string &Src, const FunctionDecl *FD,
                         const std::vector<std::string> &NewNames) {
  std::string Out = Src;
  for (unsigned i = 0; i < FD->getNumParams() && i < NewNames.size(); ++i) {
    std::string Old = FD->getParamDecl(i)->getNameAsString();
    if (Old != NewNames[i])
      Out = ReplaceWholeWord(Out, Old, NewNames[i]);
  }
  return Out;
}

std::string RenameBaseCaseCond(const BaseCase &bc, const FunctionDecl *FD,
                               const std::vector<std::string> &NewNames,
                               const ASTContext *Ctx) {
  if (bc.CondExpr)
    return RenameParams(bc.CondExpr, FD, NewNames, Ctx);
  return RenameParams(bc.CondStr, FD, NewNames);
}

std::string RenameBaseCaseValue(const BaseCase &bc, const FunctionDecl *FD,
                                const std::vector<std::string> &NewNames,
                                const ASTContext *Ctx) {
  if (bc.ValueExpr)
    return RenameParams(bc.ValueExpr, FD, NewNames, Ctx);
  return RenameParams(bc.ValueStr, FD, NewNames);
}

void EmitRenamedStmtsToIR(IRBlock *blk,
                          const std::vector<const clang::Stmt *> &Stmts,
                          const clang::ASTContext *Ctx, const FunctionDecl *FD,
                          const std::vector<std::string> &NewNames) {
  for (const Stmt *S : Stmts) {
    std::string line = PrintStmt(S, Ctx);
    // Clang's printPretty does not append a semicolon when printing an Expr
    // that happens to be used as a full statement. Add it manually.
    if (isa<Expr>(S) && !line.empty() && line.back() != ';')
      line += ';';
    IRBuilder::add(blk, IRBuilder::rawStmt(RenameParams(line, FD, NewNames)));
  }
}

void CollectGroupHoles(
    const Expr *E, const std::unordered_set<std::string> &GroupNames,
    std::vector<CallExpr *> &Holes) {
  if (!E)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (GroupNames.count(Callee->getNameAsString())) {
        Holes.push_back(const_cast<CallExpr *>(CE));
        return;
      }
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child))
      CollectGroupHoles(ChildExpr, GroupNames, Holes);
  }
}

bool ContainsGroupCall(
    const Expr *E, const std::unordered_set<std::string> &GroupNames) {
  if (!E)
    return false;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (GroupNames.count(Callee->getNameAsString()))
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ContainsGroupCall(dyn_cast_or_null<Expr>(Child), GroupNames))
      return true;
  }
  return false;
}

} // anonymous namespace

// Forward declaration.
CpsResult GenerateMutualGenericStackCPS(
    const std::vector<const FunctionDecl *> &FDs,
    const std::unordered_map<std::string, BodyAnalysis> &Analyses,
    const std::string &retType, const std::vector<std::string> &paramTypes,
    const std::vector<std::string> &paramNames);

CpsResult GenerateMutualCPS(
    const std::vector<const FunctionDecl *> &FDs) {
  if (FDs.empty())
    return MakeError(CpsErrorCode::InternalError, "empty mutual recursion group");

  const ASTContext *Ctx = &FDs[0]->getASTContext();

  // Require identical signatures (same return type and parameter types).
  std::string retType = NormalizeTypeName(FDs[0]->getReturnType().getAsString());
  std::vector<std::string> paramTypes;
  for (unsigned i = 0; i < FDs[0]->getNumParams(); ++i)
    paramTypes.push_back(
        NormalizeTypeName(FDs[0]->getParamDecl(i)->getType().getAsString()));
  // Use a synthetic, positional naming scheme shared by the whole group so that
  // member functions do not need to agree on parameter names.
  std::vector<std::string> paramNames;
  for (unsigned i = 0; i < FDs[0]->getNumParams(); ++i)
    paramNames.push_back("cps_p" + std::to_string(i));
  std::string groupName = FDs[0]->getNameAsString();

  for (size_t f = 1; f < FDs.size(); ++f) {
    if (NormalizeTypeName(FDs[f]->getReturnType().getAsString()) != retType)
      return MakeError(CpsErrorCode::UnsupportedBodyShape,
                       "mutual recursion group members must have identical "
                       "return types",
                       groupName);
    if (FDs[f]->getNumParams() != paramTypes.size())
      return MakeError(CpsErrorCode::UnsupportedBodyShape,
                       "mutual recursion group members must have identical "
                       "parameter counts",
                       groupName);
    for (unsigned i = 0; i < FDs[f]->getNumParams(); ++i) {
      if (NormalizeTypeName(FDs[f]->getParamDecl(i)->getType().getAsString()) !=
          paramTypes[i])
        return MakeError(CpsErrorCode::UnsupportedBodyShape,
                         "mutual recursion group members must have identical "
                         "parameter types",
                         groupName);
    }
  }

  // Analyze each function body.
  std::unordered_map<std::string, BodyAnalysis> Analyses;
  for (const FunctionDecl *FD : FDs) {
    BodyAnalysis BA;
    bool isVoid = FD->getReturnType()->isVoidType();
    if (!AnalyzeBody(FD->getBody(), BA, Ctx, FD->getNameAsString(), isVoid))
      return MakeError(CpsErrorCode::UnsupportedBodyShape,
                       "function body not in supported shape", groupName);
    Analyses[FD->getNameAsString()] = BA;
  }

  // Determine whether all mutual calls are tail calls. If not, fall back to
  // a generic-stack dispatcher.
  bool allTailCalls = true;
  for (const FunctionDecl *FD : FDs) {
    const BodyAnalysis &BA = Analyses[FD->getNameAsString()];
    const Expr *RecExpr = BA.RecExpr;
    const CallExpr *CE = dyn_cast<CallExpr>(RecExpr);
    if (!CE)
      allTailCalls = false;
    else if (!IsInTailPosition(CE, FD->getBody(), FD->getNameAsString()))
      allTailCalls = false;
    if (!allTailCalls)
      break;
  }

  if (!allTailCalls)
    return GenerateMutualGenericStackCPS(FDs, Analyses, retType, paramTypes,
                                         paramNames);

  IRBuilder b;
  b.comment("=== Generated mutual-recursion code ===");

  // Enum tag.
  std::string enumName = FDs[0]->getNameAsString() + "MutualTag";
  std::vector<std::string> memberNames;
  for (const FunctionDecl *FD : FDs)
    memberNames.push_back(FD->getNameAsString());
  b.enumDef(enumName, memberNames);

  // Dispatcher name: use common prefix if available, otherwise first function.
  std::string prefix = CommonPrefix(memberNames);
  std::string dispatcherName = prefix.empty() ? memberNames[0] : prefix;
  dispatcherName += "_dispatch";

  // Dispatcher signature.
  std::string sig = retType + " " + dispatcherName +
                    "(" + enumName + " tag";
  for (size_t i = 0; i < paramNames.size(); ++i)
    sig += ", " + paramTypes[i] + " " + paramNames[i];
  sig += ")";

  auto dispatcherBody = IRBuilder::block();
  auto loopBody = IRBuilder::block();
  auto sw = IRBuilder::switch_(IRExpr("tag"));
  for (const FunctionDecl *FD : FDs) {
    const BodyAnalysis &BA = Analyses[FD->getNameAsString()];
    auto caseBody = IRBuilder::block();
    for (const auto &bc : BA.BaseCases) {
      IRBuilder::add(caseBody.get(),
                     IRBuilder::if_(
                         IRExpr(RenameBaseCaseCond(bc, FD, paramNames, Ctx)),
                         IRBuilder::ret(IRExpr(RenameBaseCaseValue(
                             bc, FD, paramNames, Ctx)))));
    }
    const CallExpr *CE = dyn_cast<CallExpr>(BA.RecExpr);
    std::string nextTag = CE->getDirectCallee()->getNameAsString();
    IRBuilder::add(caseBody.get(),
                   IRBuilder::expr(IRExpr("tag = " + enumName +
                                          "::" + nextTag)));
    for (unsigned i = 0;
         i < FD->getNumParams() && i < CE->getNumArgs(); ++i) {
      IRBuilder::add(caseBody.get(),
                     IRBuilder::var("auto", "next_" + paramNames[i],
                                    IRExpr(RenameParams(CE->getArg(i), FD,
                                                        paramNames, Ctx))));
    }
    for (unsigned i = 0;
         i < FD->getNumParams() && i < CE->getNumArgs(); ++i) {
      IRBuilder::add(caseBody.get(),
                     IRBuilder::expr(IRExpr(paramNames[i] + " = next_" +
                                            paramNames[i])));
    }
    IRBuilder::add(caseBody.get(), IRBuilder::break_());
    IRBuilder::case_(sw.get(), {enumName + "::" + FD->getNameAsString()},
                     std::move(caseBody));
  }
  IRBuilder::add(loopBody.get(), std::move(sw));
  IRBuilder::add(dispatcherBody.get(),
                 IRBuilder::while_(IRExpr("1"), std::move(loopBody)));
  b.function(sig, std::move(dispatcherBody));

  // Wrapper functions.
  for (const FunctionDecl *FD : FDs) {
    std::string wrapperSig = retType + " " + FD->getNameAsString() + "(";
    for (size_t i = 0; i < paramNames.size(); ++i) {
      if (i > 0) wrapperSig += ", ";
      wrapperSig += paramTypes[i] + " " + paramNames[i];
    }
    wrapperSig += ")";
    std::string call = dispatcherName + "(" + enumName + "::" +
                       FD->getNameAsString();
    for (const auto &p : paramNames)
      call += ", " + p;
    call += ")";
    auto wrapperBody = IRBuilder::block();
    IRBuilder::add(wrapperBody.get(), IRBuilder::ret(IRExpr(call)));
    b.function(wrapperSig, std::move(wrapperBody));
  }

  return PrintGeneratedUnit(b.unit);
}

CpsResult GenerateMutualGenericStackCPS(
    const std::vector<const FunctionDecl *> &FDs,
    const std::unordered_map<std::string, BodyAnalysis> &Analyses,
    const std::string &retType, const std::vector<std::string> &paramTypes,
    const std::vector<std::string> &paramNames) {
  const ASTContext *Ctx = &FDs[0]->getASTContext();
  std::string groupName = FDs[0]->getNameAsString();

  std::unordered_set<std::string> groupNames;
  for (const FunctionDecl *FD : FDs)
    groupNames.insert(FD->getNameAsString());

  // Precompute holes per function and combined expressions.
  std::unordered_map<std::string, std::vector<CallExpr *>> HolesByFunc;
  std::unordered_map<std::string, std::string> CombinedByFunc;
  for (const FunctionDecl *FD : FDs) {
    const std::string &name = FD->getNameAsString();
    const BodyAnalysis &BA = Analyses.at(name);
    std::vector<CallExpr *> holes;
    CollectGroupHoles(BA.RecExpr, groupNames, holes);
    if (holes.empty())
      return MakeError(CpsErrorCode::NoApplicableRule,
                       "mutual generic-stack rule found no recursive calls",
                       groupName);
    // Reject nested group calls inside hole arguments.
    for (CallExpr *CE : holes) {
      for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
        if (ContainsGroupCall(CE->getArg(i), groupNames))
          return MakeError(CpsErrorCode::UnsupportedBodyShape,
                           "nested mutual recursive calls are not supported",
                           groupName);
      }
    }
    HolesByFunc[name] = holes;
    std::unordered_map<const Expr *, std::string> repls;
    for (size_t i = 0; i < holes.size(); ++i)
      repls[holes[i]] = "v" + std::to_string(i);
    CombinedByFunc[name] = StripOuterParens(PrintExprWithReplacements(
        BA.RecExpr, repls, BuildParamRenameMap(FD, paramNames), Ctx));
  }

  // Identify tail-call members: their recursive expression is exactly one
  // direct call to another group member. These can be dispatched without a
  // combine marker.
  std::unordered_map<std::string, bool> isTailByFunc;
  for (const FunctionDecl *FD : FDs) {
    const std::string &name = FD->getNameAsString();
    const auto &holes = HolesByFunc[name];
    isTailByFunc[name] =
        (holes.size() == 1 && holes[0] == Analyses.at(name).RecExpr);
  }

  // Dispatcher name.
  std::vector<std::string> names;
  for (const FunctionDecl *FD : FDs)
    names.push_back(FD->getNameAsString());
  std::string prefix = CommonPrefix(names);
  std::string dispatcherName = prefix.empty() ? names[0] : prefix;
  dispatcherName += "_dispatch";

  IRBuilder b;
  // Use the dispatcher name as the base for frame/entry types so multiple
  // mutual groups in the same translation unit cannot clash.
  StackMachineCodegen smg(dispatcherName, retType);
  smg.emitBanner(b, "mutual-recursion code (generic stack)", groupName);
  smg.emitIncludes(b);

  // Function tag enum.
  std::string enumName = FDs[0]->getNameAsString() + "MutualTag";
  b.enumDef(enumName, names);

  smg.addTagField(enumName);
  for (size_t i = 0; i < paramNames.size(); ++i)
    smg.addPlainField(paramTypes[i], paramNames[i]);
  smg.emitFrameStruct(b);
  smg.emitStackEntryStruct(b);

  // Dispatcher signature.
  std::string sig = retType + " " + dispatcherName + "(" + enumName + " tag";
  for (size_t i = 0; i < paramNames.size(); ++i)
    sig += ", " + paramTypes[i] + " " + paramNames[i];
  sig += ")";

  auto buildFrameArgs = [&](const std::string &tagExpr,
                            const std::vector<std::string> &argValues) {
    std::string s = tagExpr;
    for (const auto &a : argValues) {
      if (!s.empty())
        s += ", ";
      s += a;
    }
    return s;
  };

  auto body = IRBuilder::block();
  smg.emitStackDecl(body.get());
  smg.emitValuesDecl(body.get());
  IRBuilder::add(body.get(),
                 IRBuilder::expr(IRExpr(
                     smg.stackName() + ".emplace_back(" + smg.frameName() +
                     "(" + buildFrameArgs("tag", paramNames) + "))")));

  smg.emitLoop(
      body.get(),
      [&](IRBlock *iw) {
        // Marker branch: combine the child values of the finished frame.
        IRBuilder::add(iw, IRBuilder::var(enumName, "ftag",
                                          IRExpr(smg.curName() + ".tag")));
        std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>>
            branches;
        for (const FunctionDecl *FD : FDs) {
          const std::string &name = FD->getNameAsString();
          auto blk = IRBuilder::block();
          size_t hcount = HolesByFunc.at(name).size();
          for (size_t j = 0; j < hcount; ++j) {
            IRBuilder::add(blk.get(),
                           IRBuilder::var(retType, "v" + std::to_string(j),
                                          IRExpr(smg.valuesName() +
                                                 ".back()")));
            IRBuilder::add(blk.get(),
                           IRBuilder::expr(IRExpr(smg.valuesName() +
                                                  ".pop_back()")));
          }
          IRBuilder::add(blk.get(),
                         IRBuilder::expr(IRExpr(smg.valuesName() +
                                                ".push_back(" +
                                                CombinedByFunc.at(name) +
                                                ")")));
          branches.emplace_back("ftag == " + enumName + "::" + name,
                                std::move(blk));
        }
        IRBuilder::add(iw, IRBuilder::ifChain(std::move(branches)));
      },
      [&](IRBlock *iw) {
        // Frame branch: dispatch on the function tag.
        IRBuilder::add(iw, IRBuilder::var(enumName, "ftag",
                                          IRExpr(smg.curName() + ".tag")));
        std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>>
            branches;
        for (const FunctionDecl *FD : FDs) {
          const std::string &name = FD->getNameAsString();
          const BodyAnalysis &BA = Analyses.at(name);
          auto blk = IRBuilder::block();
          EmitRenamedStmtsToIR(blk.get(), BA.LeadingStmts, Ctx, FD,
                               paramNames);

          // else block: middle statements + pushes.
          auto elseBlk = IRBuilder::block();
          EmitRenamedStmtsToIR(elseBlk.get(), BA.MiddleStmts, Ctx, FD,
                               paramNames);
          const std::vector<CallExpr *> &holes = HolesByFunc.at(name);
          if (isTailByFunc[name]) {
            // Tail-call member: directly push the callee frame, no marker.
            const CallExpr *CE = holes[0];
            std::vector<std::string> args;
            for (unsigned a = 0;
                 a < FD->getNumParams() && a < CE->getNumArgs(); ++a)
              args.push_back(RenameParams(CE->getArg(a), FD, paramNames, Ctx));
            IRBuilder::add(
                elseBlk.get(),
                IRBuilder::expr(IRExpr(
                    smg.stackName() + ".emplace_back(" + smg.frameName() +
                    "(" +
                    buildFrameArgs(enumName + "::" +
                                       CE->getDirectCallee()
                                           ->getNameAsString(),
                                   args) +
                    "))")));
          } else {
            IRBuilder::add(elseBlk.get(),
                           IRBuilder::expr(IRExpr(
                               smg.stackName() + ".emplace_back(" +
                               smg.entryName() + "(" +
                               std::to_string(holes.size()) + ", " +
                               smg.curName() + "))")));
            for (size_t hi = 0; hi < holes.size(); ++hi) {
              std::vector<std::string> args;
              for (unsigned a = 0;
                   a < FD->getNumParams() && a < holes[hi]->getNumArgs(); ++a)
                args.push_back(
                    RenameParams(holes[hi]->getArg(a), FD, paramNames, Ctx));
              IRBuilder::add(
                  elseBlk.get(),
                  IRBuilder::expr(IRExpr(
                      smg.stackName() + ".emplace_back(" + smg.frameName() +
                      "(" +
                      buildFrameArgs(enumName + "::" +
                                         holes[hi]
                                             ->getDirectCallee()
                                             ->getNameAsString(),
                                     args) +
                      "))")));
            }
          }

          std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>>
              bcBranches;
          for (const auto &bc : BA.BaseCases) {
            auto thenBlk = IRBuilder::block();
            IRBuilder::add(thenBlk.get(),
                           IRBuilder::expr(IRExpr(
                               smg.valuesName() + ".push_back(" +
                               RenameBaseCaseValue(bc, FD, paramNames, Ctx) +
                               ")")));
            bcBranches.emplace_back(
                RenameBaseCaseCond(bc, FD, paramNames, Ctx),
                std::move(thenBlk));
          }
          IRBuilder::add(blk.get(),
                         IRBuilder::ifChain(std::move(bcBranches),
                                      std::move(elseBlk)));

          branches.emplace_back("ftag == " + enumName + "::" + name,
                                std::move(blk));
        }
        IRBuilder::add(iw, IRBuilder::ifChain(std::move(branches)));
      });

  IRBuilder::add(body.get(),
                 IRBuilder::ret(IRExpr(smg.valuesName() + ".back()")));
  b.function(sig, std::move(body));

  // Wrapper functions.
  for (const FunctionDecl *FD : FDs) {
    std::string wrapperSig = retType + " " + FD->getNameAsString() + "(";
    for (size_t i = 0; i < paramNames.size(); ++i) {
      if (i > 0) wrapperSig += ", ";
      wrapperSig += paramTypes[i] + " " + paramNames[i];
    }
    wrapperSig += ")";
    std::string call = dispatcherName + "(" + enumName + "::" +
                       FD->getNameAsString();
    for (const auto &p : paramNames)
      call += ", " + p;
    call += ")";
    auto wrapperBody = IRBuilder::block();
    IRBuilder::add(wrapperBody.get(), IRBuilder::ret(IRExpr(call)));
    b.function(wrapperSig, std::move(wrapperBody));
  }

  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
