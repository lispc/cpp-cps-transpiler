// Coroutine-trampoline backend for mutually recursive void function groups.
//
// The value-combining mutual backends in cps_generator_mutual.cc require
// identical signatures and recursion expressed as return expressions.  Visitor
// style code (e.g. the output_ir printer: printStmt / printBlock /
// printBracedBlock / printIfChain) fits neither: member signatures differ only
// in the node pointer type, and recursive calls are statements interleaved
// with side effects, possibly inside loops.
//
// For an all-void group whose intra-group calls appear in statement position
// (or as "return g(args);" tail statements), we keep each body almost
// verbatim as a C++20 coroutine and replace every group call with
//   co_yield __cps_<callee>_impl(args...);
// A driver runs an explicit stack of coroutine handles: resuming a frame
// executes statements until the next co_yield, which suspends the frame and
// pushes the freshly created (still lazy) child frame.  Locals, loop state
// and control flow are preserved by the coroutine itself, so no per-shape
// statement analysis is needed.  The native call stack never grows with
// traversal depth; coroutine frames are heap-allocated and the driver loop
// is iterative.

#include "cps_generator.h"
#include "cps_result.h"
#include "output_ir.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using namespace clang;

namespace cps {

namespace {

// One source replacement inside a function body, expressed as offsets into
// the body source text.
struct BodyEdit {
  size_t Begin = 0;
  size_t End = 0;
  std::string Text;
};

// Walk a function body and collect every rewrite needed by the coroutine
// backend.  Returns false (setting Reason) when the body contains a shape the
// backend cannot handle:
//   - an intra-group call that is not a standalone statement or a
//     "return g(args);" tail statement,
//   - a lambda (its "return" statements must not be rewritten),
//   - a non-group "return <expr>;" is supported by rewriting it into
//     "{ <expr>; co_return; }".
class CoroBodyWalker {
public:
  CoroBodyWalker(const std::unordered_set<const FunctionDecl *> &Group,
                 const ASTContext *Ctx)
      : Group(Group), Ctx(Ctx), SM(Ctx->getSourceManager()),
        LO(Ctx->getLangOpts()) {}

  bool Walk(const Stmt *Body) {
    const CompoundStmt *CS = dyn_cast_or_null<CompoundStmt>(Body);
    if (!CS) {
      Reason = "function body is not a compound statement";
      return false;
    }
    BodyBegin = SM.getFileOffset(SM.getExpansionLoc(CS->getBeginLoc()));
    BodyText = GetSourceText(CS, Ctx);
    size_t BodyEnd = SM.getFileOffset(Lexer::getLocForEndOfToken(
        SM.getExpansionLoc(CS->getEndLoc()), 0, SM, LO));
    if (BodyEnd - BodyBegin != BodyText.size()) {
      Reason = "could not map function body source range";
      return false;
    }
    if (!walkStmt(CS, true))
      return false;
    // Strip the outer braces; the body is emitted inside a function
    // definition that provides its own.
    if (BodyText.size() >= 2 && BodyText.front() == '{' &&
        BodyText.back() == '}')
      StrippedBody = BodyText.substr(1, BodyText.size() - 2);
    else
      StrippedBody = BodyText;
    return true;
  }

  // Apply the collected edits and return the rewritten body (without the
  // outer braces).
  std::string RewrittenBody() const {
    std::string Result = StrippedBody;
    std::vector<BodyEdit> Sorted = Edits;
    std::sort(Sorted.begin(), Sorted.end(),
              [](const BodyEdit &A, const BodyEdit &B) {
                return A.Begin > B.Begin;
              });
    // Edit offsets are relative to BodyText; StrippedBody drops the leading
    // '{', shifting every position left by one.
    size_t Delta = StrippedBody.size() == BodyText.size() ? 0 : 1;
    for (const BodyEdit &E : Sorted) {
      size_t B = E.Begin - Delta;
      size_t Len = E.End - E.Begin;
      Result.replace(B, Len, E.Text);
    }
    return Result;
  }

  std::string Reason;

private:
  bool isGroupCall(const Expr *E, const CallExpr *&Out) {
    const CallExpr *CE = dyn_cast<CallExpr>(E);
    if (!CE)
      return false;
    const FunctionDecl *Callee = CE->getDirectCallee();
    if (!Callee || !Group.count(Callee->getCanonicalDecl()))
      return false;
    Out = CE;
    return true;
  }

  bool containsGroupCall(const Stmt *S) const {
    if (!S)
      return false;
    if (const CallExpr *CE = dyn_cast<CallExpr>(S)) {
      if (const FunctionDecl *Callee = CE->getDirectCallee()) {
        if (Group.count(Callee->getCanonicalDecl()))
          return true;
      }
    }
    for (const Stmt *C : S->children()) {
      if (containsGroupCall(C))
        return true;
    }
    if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : DS->decls()) {
        if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
          if (VD->getInit() && containsGroupCall(VD->getInit()))
            return true;
        }
      }
    }
    return false;
  }

  size_t offsetOfBegin(SourceLocation Loc) const {
    return SM.getFileOffset(SM.getExpansionLoc(Loc)) - BodyBegin;
  }

  size_t offsetOfEnd(SourceLocation Loc) const {
    return SM.getFileOffset(
               Lexer::getLocForEndOfToken(SM.getExpansionLoc(Loc), 0, SM, LO)) -
           BodyBegin;
  }

  std::string makeThunk(const CallExpr *CE) const {
    std::string S = "co_yield __cps_" +
                    CE->getDirectCallee()->getNameAsString() + "_impl(";
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (i > 0)
        S += ", ";
      S += GetSourceText(CE->getArg(i), Ctx);
    }
    S += ")";
    return S;
  }

  bool checkCallArgs(const CallExpr *CE) {
    for (unsigned i = 0; i < CE->getNumArgs(); ++i) {
      if (containsGroupCall(CE->getArg(i))) {
        Reason = "nested mutual recursive calls are not supported";
        return false;
      }
    }
    return true;
  }

  // stmtPos is true when S sits in statement position (direct child of a
  // compound statement, an if/loop body, etc.), where a co_yield expression
  // statement may be substituted for a call.
  bool walkStmt(const Stmt *S, bool stmtPos) {
    if (!S)
      return true;

    if (isa<LambdaExpr>(S)) {
      Reason = "lambdas inside coroutine-trampoline bodies are not supported";
      return false;
    }

    if (stmtPos) {
      if (const Expr *E = dyn_cast<Expr>(S)) {
        const Expr *Un = E->IgnoreImplicit();
        const CallExpr *CE = nullptr;
        if (isGroupCall(Un, CE)) {
          if (!checkCallArgs(CE))
            return false;
          BodyEdit Edit;
          Edit.Begin = offsetOfBegin(S->getBeginLoc());
          Edit.End = offsetOfEnd(S->getEndLoc());
          Edit.Text = makeThunk(CE);
          Edits.push_back(std::move(Edit));
          return true;
        }
      }
    }

    if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
      const Expr *V = RS->getRetValue();
      BodyEdit Edit;
      Edit.Begin = offsetOfBegin(RS->getBeginLoc());
      if (!V) {
        // "return;" -> "co_return;" (keep the original semicolon).
        Edit.End = offsetOfEnd(RS->getEndLoc());
        Edit.Text = "co_return";
        Edits.push_back(std::move(Edit));
        return true;
      }
      const CallExpr *CE = nullptr;
      if (isGroupCall(V->IgnoreImplicit(), CE)) {
        if (!checkCallArgs(CE))
          return false;
        // "return g(args);" -> "{ co_yield <thunk>; co_return; }".
        Edit.End = consumeSemicolon(offsetOfEnd(RS->getEndLoc()));
        Edit.Text = "{ " + makeThunk(CE) + "; co_return; }";
        Edits.push_back(std::move(Edit));
        return true;
      }
      // "return <void-expr>;" is not expressible in a coroutine; split it.
      if (containsGroupCall(V)) {
        Reason = "nested mutual recursive calls are not supported";
        return false;
      }
      Edit.End = consumeSemicolon(offsetOfEnd(RS->getEndLoc()));
      Edit.Text = "{ " + GetSourceText(V, Ctx) + "; co_return; }";
      Edits.push_back(std::move(Edit));
      return true;
    }

    if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : DS->decls()) {
        if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
          if (const Expr *Init = VD->getInit()) {
            if (!walkStmt(Init, false))
              return false;
          }
        }
      }
      return true;
    }

    if (isa<Expr>(S)) {
      for (const Stmt *C : S->children()) {
        if (!walkStmt(C, false))
          return false;
      }
      return true;
    }

    if (isa<CompoundStmt>(S) || isa<SwitchCase>(S) || isa<LabelStmt>(S)) {
      for (const Stmt *C : S->children()) {
        if (!walkStmt(C, true))
          return false;
      }
      return true;
    }

    if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
      const VarDecl *CV = IfS->getConditionVariable();
      if ((CV && CV->getInit() && !walkStmt(CV->getInit(), false)) ||
          !walkStmt(IfS->getInit(), true) ||
          !walkStmt(IfS->getCond(), false) ||
          !walkStmt(IfS->getThen(), true) ||
          !walkStmt(IfS->getElse(), true))
        return false;
      return true;
    }

    if (const ForStmt *FS = dyn_cast<ForStmt>(S)) {
      const VarDecl *CV = FS->getConditionVariable();
      if ((CV && CV->getInit() && !walkStmt(CV->getInit(), false)) ||
          !walkStmt(FS->getInit(), true) ||
          !walkStmt(FS->getCond(), false) || !walkStmt(FS->getInc(), false) ||
          !walkStmt(FS->getBody(), true))
        return false;
      return true;
    }

    if (const WhileStmt *WS = dyn_cast<WhileStmt>(S)) {
      const VarDecl *CV = WS->getConditionVariable();
      if ((CV && CV->getInit() && !walkStmt(CV->getInit(), false)) ||
          !walkStmt(WS->getCond(), false) || !walkStmt(WS->getBody(), true))
        return false;
      return true;
    }

    if (const DoStmt *DS = dyn_cast<DoStmt>(S)) {
      if (!walkStmt(DS->getBody(), true) || !walkStmt(DS->getCond(), false))
        return false;
      return true;
    }

    if (const CXXForRangeStmt *FR = dyn_cast<CXXForRangeStmt>(S)) {
      if (!walkStmt(FR->getInit(), true) ||
          !walkStmt(FR->getRangeInit(), false) ||
          !walkStmt(FR->getBody(), true))
        return false;
      return true;
    }

    if (const SwitchStmt *SS = dyn_cast<SwitchStmt>(S)) {
      const VarDecl *CV = SS->getConditionVariable();
      if ((CV && CV->getInit() && !walkStmt(CV->getInit(), false)) ||
          !walkStmt(SS->getInit(), true) ||
          !walkStmt(SS->getCond(), false) || !walkStmt(SS->getBody(), true))
        return false;
      return true;
    }

    // break/continue/null and anything else: no statement positions inside.
    for (const Stmt *C : S->children()) {
      if (!walkStmt(C, false))
        return false;
    }
    return true;
  }

  // Extend an end offset past the next ';' in the body text, so that
  // "return ...;" is replaced as a whole statement.
  size_t consumeSemicolon(size_t End) const {
    size_t Pos = BodyText.find(';', End);
    if (Pos == std::string::npos)
      return End;
    return Pos + 1;
  }

  const std::unordered_set<const FunctionDecl *> &Group;
  const ASTContext *Ctx;
  const SourceManager &SM;
  const LangOptions &LO;

  size_t BodyBegin = 0;
  std::string BodyText;
  std::string StrippedBody;
  std::vector<BodyEdit> Edits;
};

std::string BuildParamList(const FunctionDecl *FD) {
  std::string S;
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    const ParmVarDecl *P = FD->getParamDecl(i);
    if (i > 0)
      S += ", ";
    S += NormalizeTypeName(P->getType().getAsString());
    S += " ";
    S += P->getNameAsString();
  }
  return S;
}

std::string BuildArgNameList(const FunctionDecl *FD) {
  std::string S;
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (i > 0)
      S += ", ";
    S += FD->getParamDecl(i)->getNameAsString();
  }
  return S;
}

} // anonymous namespace

CpsResult GenerateMutualCoroutineCPS(
    const std::vector<const clang::FunctionDecl *> &FDs) {
  if (FDs.empty())
    return MakeError(CpsErrorCode::InternalError, "empty mutual recursion group");

  const ASTContext *Ctx = &FDs[0]->getASTContext();
  std::string groupName = FDs[0]->getNameAsString();

  for (const FunctionDecl *FD : FDs) {
    if (!FD->getReturnType()->isVoidType()) {
      return MakeError(CpsErrorCode::UnsupportedBodyShape,
                       "coroutine trampoline requires an all-void mutual "
                       "recursion group",
                       groupName);
    }
  }

  std::unordered_set<const FunctionDecl *> Group;
  for (const FunctionDecl *FD : FDs)
    Group.insert(FD->getCanonicalDecl());

  std::string baseName = "__cps_" + groupName;
  std::string taskName = baseName + "_Task";
  std::string driveName = baseName + "_drive";

  // Rewrite each body; bail out on the first unsupported shape.
  std::vector<std::string> Bodies;
  for (const FunctionDecl *FD : FDs) {
    CoroBodyWalker Walker(Group, Ctx);
    if (!Walker.Walk(FD->getBody())) {
      return MakeError(CpsErrorCode::UnsupportedBodyShape, Walker.Reason,
                       FD->getNameAsString());
    }
    Bodies.push_back(Walker.RewrittenBody());
  }

  IRBuilder b;
  b.comment("=== Generated mutual-recursion code (coroutine trampoline, "
            "requires C++20) ===");
  b.include("coroutine");
  b.include("vector");

  // Task type: a lazy void coroutine that yields freshly created child tasks.
  // The child coroutine is lazy (initial_suspend never runs its body), so
  // creating it before the parent suspends has no observable effect; the
  // driver resumes it only after the parent frame is on the stack.
  b.raw("struct " + taskName + " {\n"
        "  struct promise_type {\n"
        "    std::coroutine_handle<promise_type> __cps_child;\n"
        "    " + taskName + " get_return_object() {\n"
        "      return " + taskName +
        "{std::coroutine_handle<promise_type>::from_promise(*this)};\n"
        "    }\n"
        "    std::suspend_always initial_suspend() noexcept { return {}; }\n"
        "    std::suspend_always final_suspend() noexcept { return {}; }\n"
        "    std::suspend_always yield_value(" + taskName + " __cps_t) {\n"
        "      __cps_child = __cps_t.__cps_h;\n"
        "      return {};\n"
        "    }\n"
        "    void return_void() {}\n"
        "    void unhandled_exception() { throw; }\n"
        "  };\n"
        "  std::coroutine_handle<promise_type> __cps_h;\n"
        "};");
  b.blank();

  // Forward declarations so member bodies can reference each other.
  for (const FunctionDecl *FD : FDs) {
    b.raw("static " + taskName + " __cps_" + FD->getNameAsString() + "_impl(" +
          BuildParamList(FD) + ");");
  }
  b.blank();

  // Driver: explicit stack of suspended coroutine handles.
  b.raw("static void " + driveName + "(" + taskName + " __cps_root) {\n"
        "  std::vector<std::coroutine_handle<" + taskName +
        "::promise_type>> __cps_stack;\n"
        "  __cps_stack.push_back(__cps_root.__cps_h);\n"
        "  while (!__cps_stack.empty()) {\n"
        "    auto __cps_cur = __cps_stack.back();\n"
        "    __cps_cur.resume();\n"
        "    if (__cps_cur.done()) {\n"
        "      __cps_stack.pop_back();\n"
        "      __cps_cur.destroy();\n"
        "    } else {\n"
        "      __cps_stack.push_back(__cps_cur.promise().__cps_child);\n"
        "    }\n"
        "  }\n"
        "}");
  b.blank();

  // Coroutine bodies.
  for (size_t i = 0; i < FDs.size(); ++i) {
    auto body = IRBuilder::block();
    IRBuilder::add(body.get(), IRBuilder::rawStmt(Bodies[i]));
    b.function("static " + taskName + " __cps_" + FDs[i]->getNameAsString() +
                   "_impl(" + BuildParamList(FDs[i]) + ")",
               std::move(body));
    b.blank();
  }

  // Wrappers with the original signatures.
  for (const FunctionDecl *FD : FDs) {
    auto body = IRBuilder::block();
    IRBuilder::add(body.get(),
                   IRBuilder::expr(IRExpr(driveName + "(__cps_" +
                                          FD->getNameAsString() + "_impl(" +
                                          BuildArgNameList(FD) + "))")));
    b.function(BuildFunctionSignature(FD, "void"), std::move(body));
    b.blank();
  }

  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
