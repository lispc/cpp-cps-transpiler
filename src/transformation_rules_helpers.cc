#include "transformation_rules_helpers.h"
#include "transformation_rule.h"
#include "output_ir.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
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

void CollectDirectRecursiveCalls(const Stmt *Root,
                                 const std::string &FuncName,
                                 std::vector<const CallExpr *> &Out) {
  if (!Root)
    return;
  std::vector<const Stmt *> Stack;
  Stack.push_back(Root);
  while (!Stack.empty()) {
    const Stmt *S = Stack.back();
    Stack.pop_back();
    if (!S)
      continue;
    if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
      if (IsDirectRecursiveCall(CE, FuncName)) {
        Out.push_back(CE);
        continue;
      }
    }
    for (const Stmt *Child : S->children())
      Stack.push_back(Child);
  }
}

bool HasForbiddenLoop(const Stmt *Root, const std::string &FuncName,
                      const ASTContext *Ctx) {
  (void)Ctx;
  if (!Root)
    return false;
  std::vector<const Stmt *> Stack;
  Stack.push_back(Root);
  while (!Stack.empty()) {
    const Stmt *S = Stack.back();
    Stack.pop_back();
    if (!S)
      continue;
    if (isa<ForStmt>(S) || isa<WhileStmt>(S) || isa<DoStmt>(S)) {
      std::vector<const CallExpr *> Calls;
      CollectDirectRecursiveCalls(S, FuncName, Calls);
      if (Calls.empty())
        continue;
      if (const ForStmt *FS = dyn_cast<ForStmt>(S)) {
        bool argIteration = false;
        for (const CallExpr *CE : Calls) {
          for (unsigned i = 0; i < CE->getNumArgs() && !argIteration; ++i) {
            if (IsCallTo(CE->getArg(i), "getArg")) {
              argIteration = true;
              break;
            }
          }
        }
        if (argIteration)
          continue;
      }
      return true;
    }
    for (const Stmt *Child : S->children())
      Stack.push_back(Child);
  }
  return false;
}

bool AllDirectRecursiveCallsNonNested(const Stmt *Root,
                                      const std::string &FuncName) {
  std::vector<const CallExpr *> Calls;
  CollectDirectRecursiveCalls(Root, FuncName, Calls);
  for (const CallExpr *CE : Calls) {
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (ContainsRecursiveCall(CE->getArg(i), FuncName))
        return false;
    }
  }
  return true;
}

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

std::string GetDefaultValueForType(const QualType &QT) {
  if (QT->isPointerType() || QT->isReferenceType())
    return "nullptr";
  if (QT->isBooleanType())
    return "false";
  if (QT->isIntegerType())
    return "0";
  if (QT->isFloatingType())
    return "0.0";
  // Class types and other value types: use value-initialization.
  return NormalizeTypeName(QT.getAsString()) + "{}";
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

// Walk an IfStmt chain, unwrapping single-statement CompoundStmt wrappers,
// and return the then-statement of the deepest IfStmt.
const IfStmt *GetInnermostIfStmt(const IfStmt *IfS) {
  while (true) {
    const Stmt *Then = IfS->getThen();
    if (const IfStmt *Next = dyn_cast<IfStmt>(Then)) {
      IfS = Next;
      continue;
    }
    if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(Then)) {
      if (CS->size() == 1) {
        if (const IfStmt *Next = dyn_cast<IfStmt>(*CS->body_begin())) {
          IfS = Next;
          continue;
        }
      }
    }
    break;
  }
  return IfS;
}

const Stmt *GetInnermostThen(const IfStmt *IfS) {
  return GetInnermostIfStmt(IfS)->getThen();
}

std::string GetSourceText(const Stmt *S, const ASTContext *Ctx) {
  if (!S)
    return "";
  SourceRange Range = S->getSourceRange();
  const SourceManager &SM = Ctx->getSourceManager();
  const LangOptions &LO = Ctx->getLangOpts();
  return Lexer::getSourceText(CharSourceRange::getTokenRange(Range), SM, LO)
      .str();
}

std::string GetSourceText(const Decl *D, const ASTContext *Ctx) {
  if (!D)
    return "";
  SourceRange Range = D->getSourceRange();
  const SourceManager &SM = Ctx->getSourceManager();
  const LangOptions &LO = Ctx->getLangOpts();
  return Lexer::getSourceText(CharSourceRange::getTokenRange(Range), SM, LO)
      .str();
}

namespace {

// If LoopBody is an if-return whose condition is a single recursive call
// (boolean OR) or a logical-not of a single recursive call (boolean AND),
// return the IfStmt and set OutIsAnd / OutRecCall.  The then-branch must be a
// return of the matching boolean literal (true for OR, false for AND).
const IfStmt *FindBooleanAllAnyIf(const Stmt *LoopBody,
                                  const std::string &FuncName,
                                  bool &OutIsAnd,
                                  CallExpr *&OutRecCall) {
  const IfStmt *IfS = dyn_cast<IfStmt>(LoopBody);
  if (!IfS) {
    if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(LoopBody)) {
      if (CS->size() == 1)
        IfS = dyn_cast<IfStmt>(*CS->body_begin());
    }
  }
  if (!IfS)
    return nullptr;

  const ReturnStmt *RS = dyn_cast<ReturnStmt>(IfS->getThen());
  if (!RS)
    return nullptr;
  const Expr *Ret = RS->getRetValue();
  if (!Ret)
    return nullptr;
  const CXXBoolLiteralExpr *BLE =
      dyn_cast<CXXBoolLiteralExpr>(Ret->IgnoreParenImpCasts());
  if (!BLE)
    return nullptr;

  const Expr *Cond = IfS->getCond()->IgnoreParenImpCasts();

  // AND: if (!rec(...)) return false;
  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(Cond)) {
    if (UO->getOpcode() == UO_LNot) {
      const Expr *Sub = UO->getSubExpr()->IgnoreParenImpCasts();
      if (const CallExpr *CE = dyn_cast<CallExpr>(Sub)) {
        if (IsDirectRecursiveCall(CE, FuncName)) {
          if (!BLE->getValue()) {
            OutIsAnd = true;
            OutRecCall = const_cast<CallExpr *>(CE);
            return IfS;
          }
        }
      }
    }
  }

  // OR: if (rec(...)) return true;
  if (const CallExpr *CE = dyn_cast<CallExpr>(Cond)) {
    if (IsDirectRecursiveCall(CE, FuncName)) {
      if (BLE->getValue()) {
        OutIsAnd = false;
        OutRecCall = const_cast<CallExpr *>(CE);
        return IfS;
      }
    }
  }

  return nullptr;
}

// Find a direct recursive call anywhere inside E.
const CallExpr *FindDirectRecursiveCallInExpr(const Expr *E,
                                              const std::string &FuncName) {
  if (!E)
    return nullptr;
  if (const CallExpr *CE = dyn_cast<CallExpr>(E))
    if (IsDirectRecursiveCall(CE, FuncName))
      return CE;
  for (const Stmt *Child : E->children()) {
    if (const Expr *ChildE = dyn_cast_or_null<Expr>(Child))
      if (const CallExpr *Found = FindDirectRecursiveCallInExpr(ChildE, FuncName))
        return Found;
  }
  return nullptr;
}

// If LoopBody is an if-return whose condition (or condition-variable
// initializer) contains a direct recursive call and whose then-branch returns
// a value, this is a find-first search.  OutRecCall is set to the recursive
// call and OutReturnExpr to the returned expression.
const IfStmt *FindFindFirstIf(const Stmt *LoopBody,
                              const std::string &FuncName,
                              CallExpr *&OutRecCall,
                              const Expr *&OutReturnExpr) {
  const IfStmt *IfS = dyn_cast<IfStmt>(LoopBody);
  if (!IfS) {
    if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(LoopBody)) {
      if (CS->size() == 1)
        IfS = dyn_cast<IfStmt>(*CS->body_begin());
    }
  }
  if (!IfS)
    return nullptr;

  const ReturnStmt *RS = dyn_cast<ReturnStmt>(IfS->getThen());
  if (!RS)
    return nullptr;
  const Expr *Ret = RS->getRetValue();
  if (!Ret)
    return nullptr;

  // if (T x = rec(...)) return x;  or  if (T x = ...) return x;
  if (const VarDecl *VD = IfS->getConditionVariable()) {
    if (const Expr *Init = VD->getInit()) {
      if (const CallExpr *CE = FindDirectRecursiveCallInExpr(Init, FuncName)) {
        OutRecCall = const_cast<CallExpr *>(CE);
        OutReturnExpr = Ret;
        return IfS;
      }
    }
  }

  // if (rec(...)) return <value>;
  const Expr *Cond = IfS->getCond()->IgnoreParenImpCasts();
  if (const CallExpr *CE = dyn_cast<CallExpr>(Cond)) {
    if (IsDirectRecursiveCall(CE, FuncName)) {
      OutRecCall = const_cast<CallExpr *>(CE);
      OutReturnExpr = Ret;
      return IfS;
    }
  }

  return nullptr;
}

} // anonymous namespace

bool IsTreeTraversalShape(const CompoundStmt *CS, const std::string &FuncName,
                          const Stmt *&OutLoop, const IfStmt *&OutRecIf,
                          CallExpr *&OutRecCall, bool IsVoid,
                          const IfStmt **OutPostLoopIf,
                          const Stmt **OutPostLoopAction,
                          bool *OutIsBoolAllAny,
                          bool *OutIsAnd,
                          bool *OutIsFindFirst,
                          const Expr **OutFindFirstReturnExpr) {
  OutLoop = nullptr;
  OutRecIf = nullptr;
  OutRecCall = nullptr;
  if (OutPostLoopIf)
    *OutPostLoopIf = nullptr;
  if (OutPostLoopAction)
    *OutPostLoopAction = nullptr;
  if (OutIsBoolAllAny)
    *OutIsBoolAllAny = false;
  if (OutIsAnd)
    *OutIsAnd = false;
  if (OutIsFindFirst)
    *OutIsFindFirst = false;
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

  // Everything before the loop must be recursion-free, except for a single
  // leading IfStmt that performs a post-order action after recursing on
  // arguments (e.g., the original CollectHolesDeep).
  const IfStmt *PostLoopIf = nullptr;
  const Stmt *PostLoopAction = nullptr;
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
      if (!callsInStmt.empty()) {
        // We only allow one recursive leading IfStmt.
        if (PostLoopIf)
          return false;
        const IfStmt *IfS = dyn_cast<IfStmt>(S);
        if (!IfS)
          return false;

        const Stmt *InnerThen = GetInnermostThen(IfS);
        const CompoundStmt *InnerCS = dyn_cast<CompoundStmt>(InnerThen);
        if (!InnerCS || InnerCS->body_empty())
          return false;

        // All recursion in this leading statement must be inside the innermost
        // then-block.
        std::vector<CallExpr *> callsInInner;
        CollectRecursiveCallsInStmt(InnerCS, FuncName, callsInInner);
        if (callsInInner.empty() ||
            callsInInner.size() != callsInStmt.size())
          return false;

        // The innermost then-block must end with a return, preceded by a
        // recursion-free statement that becomes the post-loop action.
        const Stmt *Last = nullptr;
        for (const Stmt *B : InnerCS->body())
          Last = B;
        if (!isa<ReturnStmt>(Last))
          return false;

        const Stmt *PreRet = nullptr;
        size_t idx = 0;
        for (const Stmt *B : InnerCS->body()) {
          if (idx + 1 == InnerCS->size())
            break;
          PreRet = B;
          ++idx;
        }
        if (!PreRet)
          return false;
        std::vector<CallExpr *> callsInPreRet;
        CollectRecursiveCallsInStmt(PreRet, FuncName, callsInPreRet);
        if (!callsInPreRet.empty())
          return false;

        PostLoopIf = IfS;
        PostLoopAction = PreRet;
      }
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

  if (OutPostLoopIf)
    *OutPostLoopIf = PostLoopIf;
  if (OutPostLoopAction)
    *OutPostLoopAction = PostLoopAction;

  const Stmt *LoopBody = GetLoopBody(OutLoop);
  if (!LoopBody)
    return false;

  // Exactly one recursive call inside the loop.
  std::vector<CallExpr *> calls;
  CollectRecursiveCallsInStmt(LoopBody, FuncName, calls);
  if (calls.size() != 1)
    return false;

  // Boolean all/any: the loop body is an if-return over a range-based for loop.
  // AND: if (!rec(...)) return false;   OR: if (rec(...)) return true;
  // Only supported for CXXForRangeStmt loops because we need to iterate the
  // range in reverse on the explicit stack.
  bool boolAllAny = false;
  bool isAnd = false;
  CallExpr *boolRecCall = nullptr;
  if (const IfStmt *BoolIf =
          FindBooleanAllAnyIf(LoopBody, FuncName, isAnd, boolRecCall)) {
    if (isa<CXXForRangeStmt>(OutLoop)) {
      OutRecIf = BoolIf;
      OutRecCall = boolRecCall;
      if (OutIsBoolAllAny)
        *OutIsBoolAllAny = true;
      if (OutIsAnd)
        *OutIsAnd = isAnd;
      return true;
    }
  }

  // Find-first search over a range-based for loop: the loop body is
  // "if (rec(...)) return <value>;" (possibly using a condition variable).
  // The final return after the loop provides the default / not-found value.
  if (seenReturnAfterLoop && isa<CXXForRangeStmt>(OutLoop)) {
    const Expr *findFirstRet = nullptr;
    CallExpr *findFirstCall = nullptr;
    if (const IfStmt *FFIf =
            FindFindFirstIf(LoopBody, FuncName, findFirstCall, findFirstRet)) {
      OutRecIf = FFIf;
      OutRecCall = findFirstCall;
      if (OutIsFindFirst)
        *OutIsFindFirst = true;
      if (OutFindFirstReturnExpr)
        *OutFindFirstReturnExpr = findFirstRet;
      return true;
    }
  }

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

bool ExprUsesParams(
    const Expr *E,
    const std::unordered_set<const ValueDecl *> &ParamDecls) {
  if (!E)
    return false;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const ValueDecl *VD = DRE->getDecl()) {
      if (ParamDecls.count(VD))
        return true;
    }
  }
  for (const Stmt *Child : E->children()) {
    if (ExprUsesParams(dyn_cast_or_null<Expr>(Child), ParamDecls))
      return true;
  }
  return false;
}

bool ExprContainsDeclRef(const Expr *E, const ValueDecl *VD) {
  if (!E)
    return false;
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->getDecl() == VD)
      return true;
  }
  for (const Stmt *Child : E->children()) {
    if (ExprContainsDeclRef(dyn_cast_or_null<Expr>(Child), VD))
      return true;
  }
  return false;
}

bool ExprContainsDeclRefOutsideHoles(
    const Expr *E, const ValueDecl *VD,
    const std::vector<CallExpr *> &Holes) {
  if (!E)
    return false;
  // If this node is a hole, do not descend: its arguments will be replaced.
  for (CallExpr *Hole : Holes) {
    if (E == Hole)
      return false;
  }
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->getDecl() == VD)
      return true;
  }
  for (const Stmt *Child : E->children()) {
    if (ExprContainsDeclRefOutsideHoles(dyn_cast_or_null<Expr>(Child), VD,
                                        Holes))
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

void EmitTargetedUnpacks(IRBlock *blk, const std::string &ArgName,
                         const std::vector<std::string> &Params) {
  for (const auto &p : Params)
    IRBuilder::add(blk, IRBuilder::var("auto", p,
                                       IRExpr(ArgName + "." + p)));
}

// ============================================================
// AST predicates
// ============================================================

std::string GetCalleeName(const CallExpr *CE) {
  if (!CE)
    return "";
  if (const CXXMemberCallExpr *MCE = dyn_cast<CXXMemberCallExpr>(CE)) {
    if (const CXXMethodDecl *MD = MCE->getMethodDecl())
      return MD->getNameAsString();
  }
  if (const FunctionDecl *FD = CE->getDirectCallee())
    return FD->getNameAsString();
  return "";
}

bool IsCallTo(const Expr *E, const std::string &Name) {
  if (!E)
    return false;
  const Expr *Clean = E->IgnoreParenImpCasts();
  if (const CallExpr *CE = dyn_cast<CallExpr>(Clean))
    return GetCalleeName(CE) == Name;
  return false;
}

bool AnyArgIsCallTo(const CallExpr *CE, const std::string &Name) {
  if (!CE)
    return false;
  for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
    if (IsCallTo(CE->getArg(i), Name))
      return true;
  }
  return false;
}

bool AnyArgIsCallToPrefix(const CallExpr *CE, const std::string &Prefix) {
  if (!CE)
    return false;
  for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
    const Expr *Arg = CE->getArg(i)->IgnoreParenImpCasts();
    if (const CallExpr *ArgCE = dyn_cast<CallExpr>(Arg)) {
      std::string name = GetCalleeName(ArgCE);
      if (name.size() >= Prefix.size() &&
          name.compare(0, Prefix.size(), Prefix) == 0)
        return true;
    }
  }
  return false;
}

bool AnyArgIsCallToOneOf(const CallExpr *CE,
                         const std::vector<std::string> &Names) {
  if (!CE)
    return false;
  for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
    const Expr *Arg = CE->getArg(i)->IgnoreParenImpCasts();
    if (const CallExpr *ArgCE = dyn_cast<CallExpr>(Arg)) {
      std::string name = GetCalleeName(ArgCE);
      for (const std::string &N : Names) {
        if (name == N)
          return true;
      }
    }
  }
  return false;
}

bool ContainsCallTo(const Expr *E, const std::string &Name) {
  if (!E)
    return false;
  const Expr *Clean = E->IgnoreParenImpCasts();
  if (const CallExpr *CE = dyn_cast<CallExpr>(Clean)) {
    if (GetCalleeName(CE) == Name)
      return true;
  }
  for (const Stmt *Child : Clean->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      if (ContainsCallTo(ChildExpr, Name))
        return true;
    }
  }
  return false;
}

bool ContainsCallToPrefix(const Expr *E, const std::string &Prefix) {
  if (!E)
    return false;
  const Expr *Clean = E->IgnoreParenImpCasts();
  if (const CallExpr *CE = dyn_cast<CallExpr>(Clean)) {
    std::string name = GetCalleeName(CE);
    if (name.size() >= Prefix.size() &&
        name.compare(0, Prefix.size(), Prefix) == 0)
      return true;
  }
  for (const Stmt *Child : Clean->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      if (ContainsCallToPrefix(ChildExpr, Prefix))
        return true;
    }
  }
  return false;
}

bool ContainsCallToOneOf(const Expr *E,
                         const std::vector<std::string> &Names) {
  if (!E)
    return false;
  const Expr *Clean = E->IgnoreParenImpCasts();
  if (const CallExpr *CE = dyn_cast<CallExpr>(Clean)) {
    std::string name = GetCalleeName(CE);
    for (const std::string &N : Names) {
      if (name == N)
        return true;
    }
  }
  for (const Stmt *Child : Clean->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      if (ContainsCallToOneOf(ChildExpr, Names))
        return true;
    }
  }
  return false;
}

bool ContainsDeclRefNamed(const Expr *E, const std::string &Name) {
  if (!E)
    return false;
  const Expr *Clean = E->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Clean)) {
    if (DRE->getDecl()->getNameAsString() == Name)
      return true;
  }
  for (const Stmt *Child : Clean->children()) {
    if (const Expr *ChildExpr = dyn_cast_or_null<Expr>(Child)) {
      if (ContainsDeclRefNamed(ChildExpr, Name))
        return true;
    }
  }
  return false;
}

bool AnyArgMatches(const CallExpr *CE,
                   const std::vector<std::string> &CallNames,
                   const std::vector<std::string> &DeclNames) {
  if (!CE)
    return false;
  for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
    const Expr *Arg = CE->getArg(i)->IgnoreParenImpCasts();
    if (ContainsCallToOneOf(Arg, CallNames))
      return true;
    for (const std::string &N : DeclNames) {
      if (ContainsDeclRefNamed(Arg, N))
        return true;
    }
  }
  return false;
}

// ============================================================
// Base-case printing with parameter renaming
// ============================================================

namespace {

std::string PrintBaseCaseImpl(const Expr *E, const std::string &Str,
                              const ASTContext *Ctx, const BaseCaseRename &R) {
  std::string Out;
  if (E) {
    Out = (!R.UseDeclPrinter && R.DeclRepls.empty())
              ? PrintExpr(E, Ctx)
              : PrintExprWithDeclReplacements(E, R.DeclRepls, Ctx);
  } else {
    Out = Str;
    for (const auto &KV : R.StringRepls)
      Out = ReplaceWholeWord(Out, KV.first, KV.second);
  }
  return R.StripParens ? StripOuterParens(std::move(Out)) : Out;
}

} // anonymous namespace

std::string PrintBaseCaseCond(const BaseCase &BC, const ASTContext *Ctx,
                              const BaseCaseRename &Rename) {
  return PrintBaseCaseImpl(BC.CondExpr, BC.CondStr, Ctx, Rename);
}

std::string PrintBaseCaseValue(const BaseCase &BC, const ASTContext *Ctx,
                               const BaseCaseRename &Rename) {
  return PrintBaseCaseImpl(BC.ValueExpr, BC.ValueStr, Ctx, Rename);
}

BaseCaseRename MakeParamRename(const FunctionDecl *FD,
                               const std::vector<std::string> &NewNames) {
  BaseCaseRename R;
  R.UseDeclPrinter = true;
  for (unsigned i = 0; i < FD->getNumParams() && i < NewNames.size(); ++i) {
    std::string Old = FD->getParamDecl(i)->getNameAsString();
    if (Old == NewNames[i])
      continue;
    R.DeclRepls[FD->getParamDecl(i)] = NewNames[i];
    R.StringRepls.emplace_back(Old, NewNames[i]);
  }
  return R;
}

BaseCaseRename MakeCurRename(const GenContext &Ctx, const std::string &CurExpr,
                             bool StripParens) {
  BaseCaseRename R;
  R.StripParens = StripParens;
  R.UseDeclPrinter = true;
  for (const ParmVarDecl *P : Ctx.Params) {
    std::string New = CurExpr + "." + P->getNameAsString();
    R.DeclRepls[P] = New;
    R.StringRepls.emplace_back(P->getNameAsString(), New);
  }
  return R;
}

// ============================================================
// Shared code-generation helpers
// ============================================================

void EmitTailRecParamUpdate(IRBlock *blk, const FunctionDecl *FD,
                            const CallExpr *RecCall,
                            const ASTContext *Ctx) {
  if (!RecCall)
    return;
  for (unsigned i = 0;
       i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
    std::string pName = FD->getParamDecl(i)->getNameAsString();
    IRBuilder::add(blk, IRBuilder::var("auto", "next_" + pName,
                                       IRExpr(PrintExpr(RecCall->getArg(i),
                                                        Ctx))));
  }
  for (unsigned i = 0;
       i < FD->getNumParams() && i < RecCall->getNumArgs(); ++i) {
    std::string pName = FD->getParamDecl(i)->getNameAsString();
    IRBuilder::add(blk, IRBuilder::expr(IRExpr(pName + " = next_" + pName)));
  }
}

} // namespace cps
