#include "transformation_rules_helpers.h"
#include "transformation_rule.h"
#include "code_emitter.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace cps {

using namespace clang;

// ============================================================
// Tail-position detection
// ============================================================

bool IsInTailPosition(const Expr *E, const Stmt *S,
                      const std::string &FuncName) {
  if (!E || !S)
    return false;
  if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S))
    return RS->getRetValue() == E;
  // For void tail-recursive functions, the recursive call may appear as the
  // final expression statement (not wrapped in a return).
  if (const Expr *ExprS = dyn_cast<Expr>(S))
    return ExprS == E;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S))
    return IsInTailPosition(E, IfS->getThen(), FuncName) ||
           IsInTailPosition(E, IfS->getElse(), FuncName);
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->body_empty())
      return false;
    const Stmt *Last = nullptr;
    for (const Stmt *Child : CS->body())
      Last = Child;
    return IsInTailPosition(E, Last, FuncName);
  }
  return false;
}

// ============================================================
// Local variable collection
// ============================================================

void CollectLocalVarDecls(const Stmt *S,
                          std::vector<const VarDecl *> &Out) {
  if (!S)
    return;
  if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D))
        Out.push_back(VD);
    }
  }
}

void CollectLocalVarDecls(const std::vector<const Stmt *> &Stmts,
                          std::vector<const VarDecl *> &Out) {
  for (const Stmt *S : Stmts)
    CollectLocalVarDecls(S, Out);
}

bool IsLocalFromStmts(const VarDecl *VD,
                      const std::vector<const Stmt *> &Stmts) {
  for (const Stmt *S : Stmts) {
    if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : DS->decls()) {
        if (D == VD)
          return true;
      }
    }
  }
  return false;
}

namespace {

bool IsIdentifierBoundary(char c) {
  return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
}

bool ContainsCall(const Stmt *Root, const CallExpr *Target) {
  if (!Root)
    return false;
  if (Root == Target)
    return true;
  for (const Stmt *Child : Root->children()) {
    if (ContainsCall(Child, Target))
      return true;
  }
  return false;
}

const Stmt *FindDirectChildStmtContainingCall(const Stmt *Root,
                                              const CallExpr *Target) {
  if (!Root)
    return nullptr;
  for (const Stmt *Child : Root->children()) {
    if (!Child)
      continue;
    if (Child == Target || ContainsCall(Child, Target))
      return Child;
  }
  return nullptr;
}

} // anonymous namespace

bool IdentifierUsedInCode(const std::string &Code,
                          const std::string &Name) {
  return ContainsWholeWord(Code, Name);
}

bool ContainsWholeWord(const std::string &Code, const std::string &Word) {
  size_t pos = 0;
  while ((pos = Code.find(Word, pos)) != std::string::npos) {
    bool leftOK = (pos == 0) || IsIdentifierBoundary(Code[pos - 1]);
    size_t end = pos + Word.length();
    bool rightOK = (end == Code.size()) || IsIdentifierBoundary(Code[end]);
    if (leftOK && rightOK)
      return true;
    ++pos;
  }
  return false;
}

std::string ReplaceWholeWord(const std::string &S, const std::string &Old,
                             const std::string &New) {
  std::string result = S;
  size_t pos = 0;
  while ((pos = result.find(Old, pos)) != std::string::npos) {
    bool leftOK = (pos == 0) || IsIdentifierBoundary(result[pos - 1]);
    size_t end = pos + Old.length();
    bool rightOK = (end == result.size()) || IsIdentifierBoundary(result[end]);
    if (leftOK && rightOK) {
      result.replace(pos, Old.length(), New);
      pos += New.length();
    } else {
      ++pos;
    }
  }
  return result;
}

// ============================================================
// Recursive call detection
// ============================================================

bool IsDirectRecursiveCall(const Expr *E, const std::string &FuncName) {
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee())
      return Callee->getNameAsString() == FuncName;
  }
  return false;
}

void CollectHoles(const Expr *E, const std::string &FuncName,
                  std::vector<CallExpr *> &Holes) {
  if (!E)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName) {
        Holes.push_back(const_cast<CallExpr *>(CE));
        return;
      }
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      CollectHoles(ChildExpr, FuncName, Holes);
    }
  }
}

void CollectHolesDeep(const Expr *E, const std::string &FuncName,
                      std::vector<CallExpr *> &Holes) {
  if (!E)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName) {
        // Post-order: descend into arguments first, then record this call.
        for (unsigned i = 0; i < CE->getNumArgs(); ++i)
          CollectHolesDeep(CE->getArg(i), FuncName, Holes);
        Holes.push_back(const_cast<CallExpr *>(CE));
        return;
      }
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      CollectHolesDeep(ChildExpr, FuncName, Holes);
    }
  }
}

bool ContainsRecursiveCall(const Expr *E, const std::string &FuncName) {
  if (!E)
    return false;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName)
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      if (ContainsRecursiveCall(ChildExpr, FuncName))
        return true;
    }
  }
  return false;
}

void CollectRecursiveCallsInStmt(const Stmt *S, const std::string &FuncName,
                                 std::vector<CallExpr *> &Calls) {
  if (!S)
    return;
  if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName)
        Calls.push_back(const_cast<CallExpr *>(CE));
    }
  }
  for (const Stmt *Child : S->children())
    CollectRecursiveCallsInStmt(Child, FuncName, Calls);
}

const IfStmt *FindRecursiveCallReturnIf(const Stmt *S,
                                        const std::string &FuncName,
                                        CallExpr *&OutCall) {
  if (!S)
    return nullptr;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    const Expr *Cond = IfS->getCond()->IgnoreParenImpCasts();
    if (const CallExpr *CE = dyn_cast<CallExpr>(Cond)) {
      if (const FunctionDecl *Callee = CE->getDirectCallee()) {
        if (Callee->getNameAsString() == FuncName) {
          if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(IfS->getThen())) {
            (void)RS;
            OutCall = const_cast<CallExpr *>(CE);
            return IfS;
          }
        }
      }
    }
  }
  for (const Stmt *Child : S->children()) {
    if (const IfStmt *Found = FindRecursiveCallReturnIf(Child, FuncName, OutCall))
      return Found;
  }
  return nullptr;
}

bool IsLoopStmt(const Stmt *S) {
  return isa<ForStmt>(S) || isa<CXXForRangeStmt>(S);
}

const Stmt *GetLoopBody(const Stmt *S) {
  if (const ForStmt *FS = dyn_cast<ForStmt>(S))
    return FS->getBody();
  if (const CXXForRangeStmt *FRS = dyn_cast<CXXForRangeStmt>(S))
    return FRS->getBody();
  return nullptr;
}

bool IsTreeTraversalShape(const CompoundStmt *CS, const std::string &FuncName,
                          const Stmt *&OutLoop, const IfStmt *&OutRecIf,
                          CallExpr *&OutRecCall, bool IsVoid) {
  OutLoop = nullptr;
  OutRecIf = nullptr;
  OutRecCall = nullptr;
  if (!CS || CS->body_empty())
    return false;

  // Find the single loop in the body.
  for (const Stmt *S : CS->body()) {
    if (IsLoopStmt(S)) {
      if (OutLoop)
        return false; // more than one loop
      OutLoop = S;
    }
  }
  if (!OutLoop)
    return false;

  // Everything before the loop must be recursion-free.
  bool beforeLoop = true;
  bool seenReturnAfterLoop = false;
  for (const Stmt *S : CS->body()) {
    if (S == OutLoop) {
      beforeLoop = false;
      continue;
    }
    if (beforeLoop) {
      std::vector<CallExpr *> callsInStmt;
      CollectRecursiveCallsInStmt(S, FuncName, callsInStmt);
      if (!callsInStmt.empty())
        return false;
    } else {
      // After the loop we allow at most one final return. For void functions
      // any recursion-free tail statements are also allowed.
      if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
        if (seenReturnAfterLoop)
          return false;
        seenReturnAfterLoop = true;
      } else if (!IsVoid) {
        return false;
      }
    }
  }

  const Stmt *LoopBody = GetLoopBody(OutLoop);
  if (!LoopBody)
    return false;

  // Exactly one recursive call inside the loop.
  std::vector<CallExpr *> calls;
  CollectRecursiveCallsInStmt(LoopBody, FuncName, calls);
  if (calls.size() != 1)
    return false;

  // Case 1: recursive call is the condition of an if-return.
  OutRecIf = FindRecursiveCallReturnIf(LoopBody, FuncName, OutRecCall);
  if (OutRecIf && OutRecCall)
    return true;

  // Case 2: a single recursive call appears somewhere inside the loop body.
  // We will replace the statement that encloses it with an explicit stack push.
  // This covers expression-statement recursion as well as recursion guarded by
  // an if condition, including condition-variable initializers.
  OutRecCall = calls[0];
  return true;
}

// ============================================================
// Argument shape checks
// ============================================================

bool ContainsNonRecursiveCall(const Expr *E, const std::string &FuncName) {
  if (!E)
    return false;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() != FuncName)
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ContainsNonRecursiveCall(dyn_cast_or_null<Expr>(Child), FuncName))
      return true;
  }
  return false;
}

bool IsParamMinusConst(const Expr *E, const std::string &ParamName,
                       int &OutConst) {
  E = E->IgnoreParenImpCasts();
  const BinaryOperator *BO = dyn_cast<BinaryOperator>(E);
  if (!BO || BO->getOpcode() != BO_Sub)
    return false;
  const DeclRefExpr *LHS =
      dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts());
  if (!LHS || LHS->getDecl()->getNameAsString() != ParamName)
    return false;
  const IntegerLiteral *RHS =
      dyn_cast<IntegerLiteral>(BO->getRHS()->IgnoreParenImpCasts());
  if (!RHS)
    return false;
  int c = static_cast<int>(RHS->getValue().getSExtValue());
  if (c <= 0)
    return false;
  OutConst = c;
  return true;
}

// ============================================================
// Parameter usage
// ============================================================

bool ExprUsesParams(const Expr *E,
                    const std::unordered_set<std::string> &ParamNames) {
  if (!E)
    return false;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const ValueDecl *VD = DRE->getDecl()) {
      if (ParamNames.count(VD->getNameAsString()))
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ExprUsesParams(dyn_cast_or_null<Expr>(Child), ParamNames))
      return true;
  }
  return false;
}

// ============================================================
// Purity analysis
// ============================================================

namespace {

bool IsPureExprImpl(const Expr *E) {
  if (!E)
    return true;

  E = E->IgnoreParenImpCasts();

  // Calls: conservative, except whitelisted pure functions.
  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    std::string name;
    if (const FunctionDecl *Callee = CE->getDirectCallee())
      name = Callee->getNameAsString();
    if (IsKnownPureFunction(name)) {
      for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
        if (!IsPureExprImpl(CE->getArg(i)))
          return false;
      }
      return true;
    }
    return false;
  }

  // Assignments and compound assignments are side effects.
  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->isAssignmentOp())
      return false;
    return IsPureExprImpl(BO->getLHS()) && IsPureExprImpl(BO->getRHS());
  }

  // Increment/decrement and address-of are side effects.
  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->isIncrementDecrementOp())
      return false;
    return IsPureExprImpl(UO->getSubExpr());
  }

  // Comma operator evaluates both and discards left: left may have side effects.
  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_Comma)
      return false;
  }

  // Everything else is pure if its children are pure.
  for (const Stmt *Child : E->children()) {
    if (!IsPureExprImpl(dyn_cast_or_null<Expr>(Child)))
      return false;
  }
  return true;
}

bool IsPureExprIgnoringRecursiveCallsImpl(const Expr *E,
                                          const std::string &FuncName) {
  if (!E)
    return true;

  E = E->IgnoreParenImpCasts();

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName)
        return true;
      if (IsKnownPureFunction(Callee->getNameAsString())) {
        for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
          if (!IsPureExprIgnoringRecursiveCallsImpl(CE->getArg(i), FuncName))
            return false;
        }
        return true;
      }
    }
    return false;
  }

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->isAssignmentOp())
      return false;
    if (BO->getOpcode() == BO_Comma)
      return false;
    return IsPureExprIgnoringRecursiveCallsImpl(BO->getLHS(), FuncName) &&
           IsPureExprIgnoringRecursiveCallsImpl(BO->getRHS(), FuncName);
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->isIncrementDecrementOp())
      return false;
    return IsPureExprIgnoringRecursiveCallsImpl(UO->getSubExpr(), FuncName);
  }

  for (const Stmt *Child : E->children()) {
    if (!IsPureExprIgnoringRecursiveCallsImpl(dyn_cast_or_null<Expr>(Child),
                                              FuncName))
      return false;
  }
  return true;
}

} // anonymous namespace

bool IsKnownPureFunction(const std::string &Name) {
  return Name == "min" || Name == "max" || Name == "std::min" ||
         Name == "std::max";
}

bool IsPureExpr(const Expr *E) { return IsPureExprImpl(E); }

bool IsPureExprIgnoringRecursiveCalls(const Expr *E,
                                      const std::string &FuncName) {
  return IsPureExprIgnoringRecursiveCallsImpl(E, FuncName);
}

// ============================================================
// Condition evaluation for base-case coverage checks
// ============================================================

bool ExtractParamOrLiteral(const Expr *E, const std::string &ParamName,
                           int ParamValue, int &Out) {
  E = E->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->getDecl()->getNameAsString() == ParamName) {
      Out = ParamValue;
      return true;
    }
  }
  if (const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(E)) {
    Out = static_cast<int>(IL->getValue().getSExtValue());
    return true;
  }
  return false;
}

EvalResult EvalConditionForParam(const Expr *E, const std::string &ParamName,
                                 int ParamValue) {
  E = E->IgnoreParenImpCasts();

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_LAnd) {
      auto L = EvalConditionForParam(BO->getLHS(), ParamName, ParamValue);
      auto R = EvalConditionForParam(BO->getRHS(), ParamName, ParamValue);
      if (L == EvalResult::False || R == EvalResult::False)
        return EvalResult::False;
      if (L == EvalResult::Unknown || R == EvalResult::Unknown)
        return EvalResult::Unknown;
      return EvalResult::True;
    }
    if (BO->getOpcode() == BO_LOr) {
      auto L = EvalConditionForParam(BO->getLHS(), ParamName, ParamValue);
      auto R = EvalConditionForParam(BO->getRHS(), ParamName, ParamValue);
      if (L == EvalResult::True || R == EvalResult::True)
        return EvalResult::True;
      if (L == EvalResult::Unknown || R == EvalResult::Unknown)
        return EvalResult::Unknown;
      return EvalResult::False;
    }

    int lhsVal = 0, rhsVal = 0;
    bool lhsKnown =
        ExtractParamOrLiteral(BO->getLHS(), ParamName, ParamValue, lhsVal);
    bool rhsKnown =
        ExtractParamOrLiteral(BO->getRHS(), ParamName, ParamValue, rhsVal);
    if (!lhsKnown || !rhsKnown)
      return EvalResult::Unknown;

    switch (BO->getOpcode()) {
    case BO_EQ:
      return lhsVal == rhsVal ? EvalResult::True : EvalResult::False;
    case BO_NE:
      return lhsVal != rhsVal ? EvalResult::True : EvalResult::False;
    case BO_LT:
      return lhsVal < rhsVal ? EvalResult::True : EvalResult::False;
    case BO_GT:
      return lhsVal > rhsVal ? EvalResult::True : EvalResult::False;
    case BO_LE:
      return lhsVal <= rhsVal ? EvalResult::True : EvalResult::False;
    case BO_GE:
      return lhsVal >= rhsVal ? EvalResult::True : EvalResult::False;
    default:
      return EvalResult::Unknown;
    }
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_LNot) {
      auto R = EvalConditionForParam(UO->getSubExpr(), ParamName, ParamValue);
      if (R == EvalResult::True)
        return EvalResult::False;
      if (R == EvalResult::False)
        return EvalResult::True;
      return EvalResult::Unknown;
    }
  }

  return EvalResult::Unknown;
}

// ============================================================
// Linear term parsing for TuplingRule / MemoizationRule
// ============================================================

bool ParseLinearTerms(const Expr *E, const std::string &FuncName,
                      const std::string &ParamName,
                      std::vector<LinearTerm> &Terms, int &MaxOrder) {
  E = E->IgnoreParenImpCasts();

  if (const CallExpr *CE = dyn_cast<CallExpr>(E)) {
    if (const FunctionDecl *Callee = CE->getDirectCallee()) {
      if (Callee->getNameAsString() == FuncName) {
        if (CE->getNumArgs() != 1)
          return false;
        const Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
        const BinaryOperator *BO = dyn_cast<BinaryOperator>(Arg);
        if (!BO || BO->getOpcode() != BO_Sub)
          return false;
        const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
        const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
        const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(LHS);
        if (!DRE || DRE->getDecl()->getNameAsString() != ParamName)
          return false;
        const IntegerLiteral *IL = dyn_cast<IntegerLiteral>(RHS);
        if (!IL)
          return false;
        int c = static_cast<int>(IL->getValue().getSExtValue());
        if (c <= 0)
          return false;
        Terms.push_back({c, 1, const_cast<CallExpr *>(CE)});
        MaxOrder = std::max(MaxOrder, c);
        return true;
      }
    }
  }

  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_Minus) {
      std::vector<LinearTerm> SubTerms;
      int SubMax = 0;
      if (!ParseLinearTerms(UO->getSubExpr(), FuncName, ParamName, SubTerms,
                            SubMax))
        return false;
      for (auto &t : SubTerms)
        t.Sign = -t.Sign;
      Terms.insert(Terms.end(), SubTerms.begin(), SubTerms.end());
      MaxOrder = std::max(MaxOrder, SubMax);
      return true;
    }
  }

  if (const BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() != BO_Add && BO->getOpcode() != BO_Sub)
      return false;
    std::vector<LinearTerm> LeftTerms, RightTerms;
    int LeftMax = 0, RightMax = 0;
    if (!ParseLinearTerms(BO->getLHS(), FuncName, ParamName, LeftTerms, LeftMax))
      return false;
    if (!ParseLinearTerms(BO->getRHS(), FuncName, ParamName, RightTerms,
                          RightMax))
      return false;
    if (BO->getOpcode() == BO_Sub) {
      for (auto &t : RightTerms)
        t.Sign = -t.Sign;
    }
    Terms.insert(Terms.end(), LeftTerms.begin(), LeftTerms.end());
    Terms.insert(Terms.end(), RightTerms.begin(), RightTerms.end());
    MaxOrder = std::max({MaxOrder, LeftMax, RightMax});
    return true;
  }

  return false;
}

// ============================================================
// Defunctionalized-rule helpers
// ============================================================

std::vector<std::string> ParamsUsedInCode(
    const std::string &Code,
    const std::vector<std::string> &ParamNames) {
  std::vector<std::string> used;
  for (const auto &p : ParamNames) {
    if (ContainsWholeWord(Code, p))
      used.push_back(p);
  }
  return used;
}

void EmitTargetedUnpacks(CodeEmitter &e, const std::string &ArgName,
                         const std::vector<std::string> &Params) {
  for (const auto &p : Params)
    e.line("auto " + p + " = " + ArgName + "." + p + ";");
}

} // namespace cps
