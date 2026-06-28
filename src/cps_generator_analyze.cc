#include "cps_generator.h"
#include "transformation_rules_helpers.h"
#include "clang/AST/Stmt.h"
#include <string>
#include <vector>

using namespace clang;

namespace cps {

namespace {

// Unwrap a single-statement CompoundStmt, or return the last statement of a
// multi-statement CompoundStmt.  This lets base-case detection find the
// trailing return inside compound if-branches such as those in the helper
// functions IsInTailPosition, EvalConditionForParam, and ParseLinearTerms.
const Stmt *UnwrapTrailingStmt(const Stmt *S) {
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->body_empty())
      return nullptr;
    const Stmt *Last = nullptr;
    for (const Stmt *B : CS->body())
      Last = B;
    return UnwrapTrailingStmt(Last);
  }
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    // An if-without-else is just a guarded block; the trailing statement of
    // its then-branch is what matters for base-case detection.
    if (IfS->getElse())
      return S;
    return UnwrapTrailingStmt(IfS->getThen());
  }
  return S;
}

const Expr *ExtractReturnExpr(const Stmt *S) {
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(UnwrapTrailingStmt(S)))
    return RS->getRetValue();
  return nullptr;
}

bool IsVoidReturn(const Stmt *S) {
  return isa<ReturnStmt>(UnwrapTrailingStmt(S));
}

// True if the statement is (or ends with) a switch statement.
// Used to accept helper-function base cases such as EvalConditionForParam,
// where the value differs per case and is not represented by a single Expr.
bool EndsWithSwitch(const Stmt *S) {
  return isa<SwitchStmt>(UnwrapTrailingStmt(S));
}

BaseCase MakeBaseCase(const Expr *Cond, const Expr *Value,
                      const ASTContext *Ctx) {
  BaseCase bc;
  bc.CondExpr = Cond;
  bc.ValueExpr = Value;
  bc.CondStr = Cond ? StripOuterParens(PrintExpr(Cond, Ctx)) : "";
  bc.ValueStr = Value ? StripOuterParens(PrintExpr(Value, Ctx)) : "";
  return bc;
}

void FlattenIfElse(const Stmt *S, BodyAnalysis &BA, const ASTContext *Ctx) {
  if (!S)
    return;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    const Expr *BaseExpr = ExtractReturnExpr(IfS->getThen());
    if (BaseExpr)
      BA.BaseCases.push_back(MakeBaseCase(IfS->getCond(), BaseExpr, Ctx));
    if (const Stmt *Else = IfS->getElse())
      FlattenIfElse(Else, BA, Ctx);
    return;
  }
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
    BA.RecExpr = RS->getRetValue();
    BA.IsRecursive = true;
  }
}

// Collect all case values from a possibly nested chain of CaseStmts
// (e.g., "case 0: case 1: return ...").
std::vector<const Expr *> CollectCaseValues(const CaseStmt *Case) {
  std::vector<const Expr *> values;
  const Stmt *Sub = Case;
  while (const CaseStmt *CS = dyn_cast<CaseStmt>(Sub)) {
    values.push_back(CS->getLHS());
    if (CS->getRHS())
      values.push_back(CS->getRHS());
    Sub = CS->getSubStmt();
  }
  return values;
}

// Get the actual body after stripping nested CaseStmts.
const Stmt *GetCaseBody(const CaseStmt *Case) {
  const Stmt *Sub = Case;
  while (const CaseStmt *CS = dyn_cast<CaseStmt>(Sub))
    Sub = CS->getSubStmt();
  return Sub;
}

bool ExtractSwitchCases(const SwitchStmt *SS, BodyAnalysis &BA,
                        const ASTContext *Ctx) {
  if (!SS)
    return false;
  const Expr *Cond = SS->getCond();
  if (!Cond)
    return false;
  std::string condStr = PrintExpr(Cond, Ctx);

  const CompoundStmt *CS = dyn_cast<CompoundStmt>(SS->getBody());
  if (!CS)
    return false;

  const Expr *pendingValue = nullptr;
  std::string pendingValueStr;
  std::vector<const Expr *> pendingCases;

  for (const Stmt *Sub : CS->body()) {
    if (const CaseStmt *Case = dyn_cast<CaseStmt>(Sub)) {
      std::vector<const Expr *> caseVals = CollectCaseValues(Case);
      const Expr *ret = ExtractReturnExpr(GetCaseBody(Case));
      for (const Expr *cv : caseVals)
        pendingCases.push_back(cv);
      if (ret) {
        pendingValue = ret;
        pendingValueStr = PrintExpr(ret, Ctx);
      }
      if (pendingValue) {
        for (const Expr *cv : pendingCases) {
          BaseCase bc;
          bc.ValueExpr = pendingValue;
          bc.CondStr = "(" + condStr + " == " + PrintExpr(cv, Ctx) + ")";
          bc.ValueStr = pendingValueStr;
          BA.BaseCases.push_back(bc);
        }
        pendingCases.clear();
      }
    } else if (const DefaultStmt *Def = dyn_cast<DefaultStmt>(Sub)) {
      const Expr *ret = ExtractReturnExpr(Def->getSubStmt());
      if (!ret)
        return false;
      BA.RecExpr = ret;
      BA.IsRecursive = true;
    }
  }
  return BA.IsRecursive;
}

bool IsReturnOrIfReturnOrSwitch(const Stmt *S) {
  if (isa<ReturnStmt>(S))
    return true;
  if (isa<IfStmt>(S))
    return true;
  if (isa<SwitchStmt>(S))
    return true;
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->size() == 1)
      return IsReturnOrIfReturnOrSwitch(CS->body_begin()[0]);
  }
  return false;
}

} // anonymous namespace

bool AnalyzeBody(const Stmt *Body, BodyAnalysis &BA,
                 const ASTContext *Ctx,
                 const std::string &FuncName,
                 bool IsVoid) {
  BA = BodyAnalysis();
  const CompoundStmt *CS = dyn_cast<CompoundStmt>(Body);
  if (!CS)
    return false;

  // Tree-traversal recursion: loop over children with a recursive call inside
  // an if-return. Existing return-expression rules don't handle this shape;
  // TreeTraversalRule will pick it up via BA.IsRecursive.
  const Stmt *Loop = nullptr;
  const IfStmt *RecIf = nullptr;
  CallExpr *RecCall = nullptr;
  if (IsTreeTraversalShape(CS, FuncName, Loop, RecIf, RecCall, IsVoid)) {
    BA.IsRecursive = true;
    return true;
  }

  size_t idx = 0;
  while (idx < CS->size()) {
    const Stmt *S = CS->body_begin()[idx];
    if (IsReturnOrIfReturnOrSwitch(S))
      break;
    BA.LeadingStmts.push_back(S);
    ++idx;
  }

  while (idx < CS->size()) {
    const Stmt *S = CS->body_begin()[idx];
    if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
      const Expr *BaseExpr = ExtractReturnExpr(IfS->getThen());
      // A void base case "if (cond) return;" has no value expression and is
      // valid for void functions; non-void functions require a return value.
      // We also allow branches that end in a switch of returns (e.g.
      // EvalConditionForParam), treating them as base cases with no single
      // value expression.
      if (!BaseExpr && !IsVoidReturn(IfS->getThen()) &&
          !EndsWithSwitch(IfS->getThen()))
        return false;
      BA.BaseCases.push_back(MakeBaseCase(IfS->getCond(), BaseExpr, Ctx));
      if (const Stmt *Else = IfS->getElse()) {
        FlattenIfElse(Else, BA, Ctx);
        ++idx;
        break;
      }
      ++idx;
      continue;
    }
    if (const SwitchStmt *SS = dyn_cast<SwitchStmt>(S)) {
      if (!ExtractSwitchCases(SS, BA, Ctx))
        return false;
      ++idx;
      break;
    }
    if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
      const Expr *Ret = RS->getRetValue();
      // Normalize "return cond ? base : rec;" into a base case + recursive expr.
      bool splitTernary = false;
      if (Ret) {
        if (const ConditionalOperator *CO =
                dyn_cast<ConditionalOperator>(Ret->IgnoreParenImpCasts())) {
          const Expr *TrueE = CO->getTrueExpr()->IgnoreParenImpCasts();
          const Expr *FalseE = CO->getFalseExpr()->IgnoreParenImpCasts();
          bool trueRec = ContainsRecursiveCall(TrueE, FuncName);
          bool falseRec = ContainsRecursiveCall(FalseE, FuncName);
          if (trueRec && !falseRec) {
            BA.BaseCases.push_back(
                MakeBaseCase(CO->getCond(), FalseE, Ctx));
            BA.RecExpr = TrueE;
            BA.IsRecursive = true;
            splitTernary = true;
          } else if (!trueRec && falseRec) {
            BA.BaseCases.push_back(
                MakeBaseCase(CO->getCond(), TrueE, Ctx));
            BA.RecExpr = FalseE;
            BA.IsRecursive = true;
            splitTernary = true;
          }
        }
      }
      if (!splitTernary) {
        BA.RecExpr = Ret;
        BA.IsRecursive = true;
      }
      ++idx;
      break;
    }
    // For void tail-recursive functions, the recursive "return" may be an
    // expression statement containing a direct recursive call.
    if (IsVoid) {
      if (const Expr *E = dyn_cast<Expr>(S)) {
        E = E->IgnoreParenImpCasts();
        if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
          if (const FunctionDecl *Callee = CE->getDirectCallee()) {
            if (Callee->getNameAsString() == FuncName) {
              BA.RecExpr = E;
              BA.IsRecursive = true;
              ++idx;
              break;
            }
          }
        }
      }
    }
    BA.MiddleStmts.push_back(S);
    ++idx;
  }

  if (idx != CS->size())
    return false;

  return !BA.BaseCases.empty() && BA.IsRecursive;
}

} // namespace cps
