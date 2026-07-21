#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cps {

using namespace clang;

namespace {

// Per-parameter classification shared by applies() and apply().
struct DimAnalysis {
  // isIndex[d] is true when parameter d is an integer index dimension;
  // otherwise the parameter is passed through unchanged in every hole.
  std::vector<bool> isIndex;
  // maxOff[d]: maximum constant subtracted from parameter d across all holes
  // (only meaningful for index dimensions).
  std::vector<int> maxOff;
  // offsets[h][d]: constant subtracted from parameter d in hole h
  // (0 for pass-through positions).
  std::vector<std::vector<int>> offsets;
};

bool IsParamRef(const Expr *E, const ParmVarDecl *P) {
  E = E->IgnoreParenImpCasts();
  const auto *DRE = dyn_cast<DeclRefExpr>(E);
  return DRE && DRE->getDecl()->getCanonicalDecl() == P->getCanonicalDecl();
}

// Match `P - <positive integer literal>` with decl-based comparison.
bool MatchParamMinusConst(const Expr *E, const ParmVarDecl *P, int &OutOffset) {
  E = E->IgnoreParenImpCasts();
  const auto *BO = dyn_cast<BinaryOperator>(E);
  if (!BO || BO->getOpcode() != BO_Sub)
    return false;
  if (!IsParamRef(BO->getLHS(), P))
    return false;
  const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
  const auto *IL = dyn_cast<IntegerLiteral>(RHS);
  if (!IL)
    return false;
  int64_t v = IL->getValue().getSExtValue();
  if (v <= 0 || v > INT32_MAX)
    return false;
  OutOffset = static_cast<int>(v);
  return true;
}

// Classify every parameter position as index or pass-through and collect the
// per-hole offsets. Returns false when the hole arguments do not fit the
// supported shape (see rule comment).
bool AnalyzeDims(const std::vector<CallExpr *> &holes, const GenContext &Ctx,
                 DimAnalysis &Out) {
  size_t nParams = Ctx.Params.size();
  Out.isIndex.assign(nParams, false);
  Out.maxOff.assign(nParams, 0);
  Out.offsets.clear();

  // Pass 1: validate hole shapes and record offsets.
  for (const CallExpr *hole : holes) {
    if (hole->getNumArgs() != nParams)
      return false;
    std::vector<int> offs(nParams, 0);
    int positiveOffsets = 0;
    for (unsigned a = 0; a < nParams; ++a) {
      const ParmVarDecl *P = Ctx.Params[a];
      const Expr *arg = hole->getArg(a);
      if (IsParamRef(arg, P)) {
        offs[a] = 0; // pass-through or zero offset; classified in pass 2
      } else {
        if (!P->getType()->isIntegralOrEnumerationType())
          return false;
        int off = 0;
        if (!MatchParamMinusConst(arg, P, off))
          return false;
        offs[a] = off;
        ++positiveOffsets;
      }
    }
    // Every hole must make progress in at least one dimension.
    if (positiveOffsets == 0)
      return false;
    Out.offsets.push_back(std::move(offs));
  }

  // Pass 2: a position is an index dimension iff some hole subtracts a
  // constant there.
  for (const auto &offs : Out.offsets) {
    for (size_t a = 0; a < nParams; ++a) {
      if (offs[a] > 0) {
        Out.isIndex[a] = true;
        Out.maxOff[a] = std::max(Out.maxOff[a], offs[a]);
      }
    }
  }
  return true;
}

} // anonymous namespace

bool MultiDimMemoRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                               const GenContext &Ctx) const {
  if (Ctx.RetType == "void" || !BA.RecExpr)
    return false;
  // Single-parameter recurrences are MemoizationRule's job.
  if (Ctx.Params.size() < 2)
    return false;
  if (!BA.MiddleStmts.empty())
    return false;
  if (BA.BaseCases.empty())
    return false;
  // Allows the min/max family used by LCS / edit-distance style recurrences.
  if (!IsPureExprIgnoringRecursiveCalls(BA.RecExpr, Ctx.FuncName))
    return false;

  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);
  if (holes.empty())
    return false;

  DimAnalysis dims;
  if (!AnalyzeDims(holes, Ctx, dims))
    return false;

  // Base cases must cover the boundary slab 0..maxOff_d-1 of every index
  // dimension, independent of the other coordinates. Base cases that cannot
  // be statically evaluated for a dimension are simply skipped; at least one
  // must be provably true.
  for (size_t d = 0; d < Ctx.Params.size(); ++d) {
    if (!dims.isIndex[d])
      continue;
    for (int v = 0; v < dims.maxOff[d]; ++v) {
      bool covered = false;
      for (const auto &bc : BA.BaseCases) {
        if (!bc.CondExpr)
          return false; // switch-derived base cases not supported here
        if (EvalConditionForParam(bc.CondExpr, Ctx.ParamNames[d], v) ==
            EvalResult::True) {
          covered = true;
          break;
        }
      }
      if (!covered)
        return false;
    }
  }

  return true;
}

CpsResult MultiDimMemoRule::apply(const FunctionDecl *FD,
                                  const BodyAnalysis &BA,
                                  GenContext &Ctx) const {
  std::vector<CallExpr *> holes;
  CollectHoles(BA.RecExpr, Ctx.FuncName, holes);

  DimAnalysis dims;
  AnalyzeDims(holes, Ctx, dims);

  size_t nParams = Ctx.Params.size();

  // Loop variable names for the index dimensions (in parameter order).
  std::vector<std::string> loopVar(nParams);
  std::vector<size_t> indexDims;
  for (size_t d = 0; d < nParams; ++d) {
    if (dims.isIndex[d]) {
      loopVar[d] = "__cps_i" + std::to_string(indexDims.size());
      indexDims.push_back(d);
    }
  }

  // Map every index parameter to its loop variable.
  std::unordered_map<const ValueDecl *, std::string> declRepls;
  for (size_t d : indexDims)
    declRepls[Ctx.Params[d]] = loopVar[d];

  // dp[__cps_i0 - off0][__cps_i1] ... index string builder. When offsets is
  // null, plain loop variables are used; when useParams is set, the original
  // parameter names are used instead (for the final return).
  auto indexString = [&](const std::vector<int> *offsets,
                         bool useParams) -> std::string {
    std::string s;
    for (size_t d : indexDims) {
      std::string base = useParams ? Ctx.ParamNames[d] : loopVar[d];
      int off = offsets ? (*offsets)[d] : 0;
      s += "[";
      s += base;
      if (off > 0)
        s += " - " + std::to_string(off);
      s += "]";
    }
    return s;
  };

  // Replace each hole with its dp lookup.
  std::unordered_map<const Expr *, std::string> holeRepls;
  for (size_t h = 0; h < holes.size(); ++h)
    holeRepls[holes[h]] = "dp" + indexString(&dims.offsets[h], false);

  std::string combinedExpr = StripOuterParens(PrintExprWithReplacements(
      BA.RecExpr, holeRepls, declRepls, Ctx.ASTCtx));

  // Print a base-case condition/value with index params mapped to loop vars.
  BaseCaseRename loopVarRename;
  loopVarRename.DeclRepls = declRepls;
  for (size_t d : indexDims)
    loopVarRename.StringRepls.emplace_back(Ctx.ParamNames[d], loopVar[d]);
  loopVarRename.StripParens = true;
  loopVarRename.UseDeclPrinter = true;

  auto printWithLoopVars = [&](const BaseCase &bc, bool cond) -> std::string {
    return cond ? PrintBaseCaseCond(bc, Ctx.ASTCtx, loopVarRename)
                : PrintBaseCaseValue(bc, Ctx.ASTCtx, loopVarRename);
  };

  IRBuilder b;
  b.comment("=== Generated multi-dim memoized code for function: " +
            Ctx.FuncName + " ===");
  b.include("vector");
  if (ContainsCallToOneOf(BA.RecExpr, {"min", "max"}))
    b.include("algorithm");

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);
  auto body = IRBuilder::block();

  EmitStmtsToIR(b, body.get(), BA.LeadingStmts, Ctx.ASTCtx);

  // Nested dp table declaration:
  //   std::vector<std::vector<T>> dp(n0 + 1, std::vector<T>(n1 + 1));
  {
    std::string dpType = Ctx.RetType;
    for (size_t k = 0; k < indexDims.size(); ++k)
      dpType = "std::vector<" + dpType + ">";
    std::vector<std::string> elemType(indexDims.size() + 1);
    elemType[indexDims.size()] = Ctx.RetType;
    for (size_t k = indexDims.size(); k-- > 0;)
      elemType[k] = "std::vector<" + elemType[k + 1] + ">";
    std::string decl;
    for (size_t k = 0; k < indexDims.size(); ++k) {
      decl += elemType[k] + "(" + Ctx.ParamNames[indexDims[k]] + " + 1";
      if (k + 1 < indexDims.size())
        decl += ", ";
    }
    for (size_t k = 1; k < indexDims.size(); ++k)
      decl += ")";
    decl += ");";
    IRBuilder::add(body.get(), IRBuilder::rawStmt(dpType + " dp = " + decl));
  }

  // Cell body: base-case chain, else the recurrence.
  auto cellBody = IRBuilder::block();
  {
    std::string cell = "dp" + indexString(nullptr, false);
    std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>> branches;
    for (const auto &bc : BA.BaseCases) {
      auto thenBlk = IRBuilder::block();
      IRBuilder::add(thenBlk.get(),
                     IRBuilder::expr(IRExpr(cell + " = " +
                                            printWithLoopVars(bc, false))));
      branches.emplace_back(printWithLoopVars(bc, true),
                            std::move(thenBlk));
    }
    auto elseBlk = IRBuilder::block();
    IRBuilder::add(elseBlk.get(),
                   IRBuilder::expr(IRExpr(cell + " = " + combinedExpr)));
    IRBuilder::add(cellBody.get(),
                   IRBuilder::ifChain(std::move(branches),
                                      std::move(elseBlk)));
  }

  // Wrap the cell body in one ascending loop per index dimension, innermost
  // last.
  std::unique_ptr<IRStmt> nest = std::move(cellBody);
  for (size_t k = indexDims.size(); k-- > 0;) {
    size_t d = indexDims[k];
    auto loopBody = IRBuilder::block();
    IRBuilder::add(loopBody.get(), std::move(nest));
    nest = IRBuilder::for_("int " + loopVar[d] + " = 0",
                           IRExpr(loopVar[d] + " <= " + Ctx.ParamNames[d]),
                           "++" + loopVar[d], std::move(loopBody));
  }
  IRBuilder::add(body.get(), std::move(nest));

  IRBuilder::add(body.get(),
                 IRBuilder::ret(IRExpr("dp" + indexString(nullptr, true))));

  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
