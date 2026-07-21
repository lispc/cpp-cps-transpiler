#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "output_ir.h"
#include "stack_machine_codegen.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Lex/Lexer.h"
#include <set>
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

// Ensure a statement source fragment ends with a semicolon.
std::string EnsureSemicolon(const std::string &S) {
  std::string result = S;
  while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back())))
    result.pop_back();
  if (!result.empty() && result.back() != ';' && result.back() != '}' &&
      result.back() != '{')
    result += ';';
  return result;
}

// Detect parameters used as output references (non-const lvalue references).
bool IsOutputReferenceParam(const ParmVarDecl *PVD) {
  QualType T = PVD->getType();
  if (T->isLValueReferenceType()) {
    QualType Pointee = T->getPointeeType();
    if (!Pointee.isConstQualified())
      return true;
  }
  return false;
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

// Replace literal boolean returns with a values-stack push + continue.
// Used for boolean all/any tree traversals so that base cases contribute a
// value instead of returning from the explicit stack loop.
std::string ReplaceLiteralReturns(const std::string &S,
                                  const std::string &ValuesVar) {
  std::string r = S;
  size_t pos = 0;
  const std::string falseRepl =
      "{ " + ValuesVar + ".push_back(false); continue; }";
  while ((pos = r.find("return false;", pos)) != std::string::npos) {
    r.replace(pos, 13, falseRepl);
    pos += falseRepl.size();
  }
  pos = 0;
  const std::string trueRepl =
      "{ " + ValuesVar + ".push_back(true); continue; }";
  while ((pos = r.find("return true;", pos)) != std::string::npos) {
    r.replace(pos, 12, trueRepl);
    pos += trueRepl.size();
  }
  return r;
}

namespace {

bool IsIdentifierChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

} // anonymous namespace

// Rewrite every "return <expr>;" in a statement block into
// "values.push_back(<expr>); continue;".  Used for find-first tree traversals
// where base cases and found cases contribute a value instead of returning.
std::string ReplaceReturnsWithValuePush(const std::string &S,
                                        const std::string &ValuesVar) {
  std::string r;
  size_t i = 0;
  while (i < S.size()) {
    size_t pos = S.find("return", i);
    if (pos == std::string::npos) {
      r += S.substr(i);
      break;
    }
    bool wordBoundary =
        (pos == 0 || !IsIdentifierChar(S[pos - 1])) &&
        (pos + 6 == S.size() || !IsIdentifierChar(S[pos + 6]));
    if (!wordBoundary) {
      r += S.substr(i, pos - i + 1);
      i = pos + 1;
      continue;
    }
    size_t semi = S.find(';', pos);
    if (semi == std::string::npos) {
      r += S.substr(i);
      break;
    }
    std::string expr = S.substr(pos + 6, semi - (pos + 6));
    // Trim whitespace.
    size_t a = 0;
    while (a < expr.size() &&
           std::isspace(static_cast<unsigned char>(expr[a])))
      ++a;
    size_t b = expr.size();
    while (b > a && std::isspace(static_cast<unsigned char>(expr[b - 1])))
      --b;
    std::string trimmed = expr.substr(a, b - a);
    r += S.substr(i, pos - i);
    r += "{ " + ValuesVar + ".push_back(" + trimmed + "); continue; }";
    i = semi + 1;
  }
  return r;
}

// Strip a trailing ':' (and surrounding whitespace) from a range-based for
// loop variable source fragment.
std::string StripTrailingColon(const std::string &S) {
  std::string r = S;
  while (!r.empty() && std::isspace(static_cast<unsigned char>(r.back())))
    r.pop_back();
  if (!r.empty() && r.back() == ':')
    r.pop_back();
  while (!r.empty() && std::isspace(static_cast<unsigned char>(r.back())))
    r.pop_back();
  return r;
}

// Strip a common leading whitespace prefix from every line in S.
std::string NormalizeIndentation(const std::string &S) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : S) {
    if (c == '\n') {
      lines.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  lines.push_back(cur);

  size_t minIndent = static_cast<size_t>(-1);
  for (const auto &line : lines) {
    if (line.empty())
      continue;
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
      ++i;
    minIndent = std::min(minIndent, i);
  }

  if (minIndent == static_cast<size_t>(-1))
    return S;

  std::string result;
  bool first = true;
  for (const auto &line : lines) {
    if (!first)
      result += "\n";
    if (line.size() > minIndent)
      result += line.substr(minIndent);
    first = false;
  }
  return result;
}

const IfStmt *FindCondVarIf(const VarDecl *VD, const Stmt *Root) {
  if (!VD || !Root)
    return nullptr;
  if (const IfStmt *IfS = dyn_cast<IfStmt>(Root)) {
    if (IfS->getConditionVariable() == VD)
      return IfS;
  }
  for (const Stmt *Child : Root->children()) {
    if (const IfStmt *Found = FindCondVarIf(VD, Child))
      return Found;
  }
  return nullptr;
}

// Print an expression, but inline any condition-variable declared by an
// enclosing if-statement inside LoopBody.  This keeps generated push
// statements valid when the original recursive call used a variable that was
// introduced by a cast guard such as "if (const Expr *X = dyn_cast<Expr>(V))".
std::string GetArgSource(const Expr *E, const Stmt *LoopBody,
                         const ASTContext *Ctx) {
  E = E->IgnoreParenImpCasts();
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (const IfStmt *IfS = FindCondVarIf(VD, LoopBody)) {
        const Expr *Init = IfS->getConditionVariable()->getInit();
        if (Init)
          return PrintExpr(Init, Ctx);
      }
    }
  }
  return PrintExpr(E, Ctx);
}

} // anonymous namespace

bool TreeTraversalRule::applies(const FunctionDecl *FD, const BodyAnalysis &BA,
                                const GenContext &Ctx) const {
  // Only handle functions that the existing return-expression rules cannot
  // handle (i.e., no single recursive return expression).
  if (BA.RecExpr)
    return false;

  const CompoundStmt *CS = dyn_cast<CompoundStmt>(FD->getBody());
  const Stmt *Loop = nullptr;
  const IfStmt *RecIf = nullptr;
  CallExpr *RecCall = nullptr;
  const IfStmt *PostLoopIf = nullptr;
  const Stmt *PostLoopAction = nullptr;
  bool IsBoolAllAny = false;
  bool IsAnd = false;
  bool IsFindFirst = false;
  const Expr *FindFirstReturnExpr = nullptr;
  bool IsVoid = FD->getReturnType()->isVoidType();
  if (!IsTreeTraversalShape(CS, Ctx.FuncName, Loop, RecIf, RecCall, IsVoid,
                            &PostLoopIf, &PostLoopAction,
                            &IsBoolAllAny, &IsAnd,
                            &IsFindFirst, &FindFirstReturnExpr))
    return false;

  // Find-first searches are only supported for pointer-like return types where
  // a non-null result means "found".
  if (IsFindFirst && !FD->getReturnType()->isPointerType() &&
      !FD->getReturnType()->isReferenceType())
    return false;

  // TreeTraversalRule assumes the recursive call is to *this* overload.
  // Overload-based recursion (e.g. CollectLocalVarDecls) calls a different
  // FunctionDecl with the same name; transforming it would generate code that
  // invokes a non-existent overload.
  if (!RecCall || RecCall->getDirectCallee() != FD)
    return false;

  return true;
}

CpsResult TreeTraversalRule::apply(const FunctionDecl *FD,
                                     const BodyAnalysis &BA,
                                     GenContext &Ctx) const {
  (void)BA;
  const CompoundStmt *CS = dyn_cast<CompoundStmt>(FD->getBody());
  bool IsVoid = FD->getReturnType()->isVoidType();

  // Locate the loop and its recursive call.
  const Stmt *LoopStmt = nullptr;
  const IfStmt *RecIf = nullptr;
  CallExpr *RecCall = nullptr;
  const IfStmt *PostLoopIf = nullptr;
  const Stmt *PostLoopAction = nullptr;
  bool IsBoolAllAny = false;
  bool IsAnd = false;
  bool IsFindFirst = false;
  const Expr *FindFirstReturnExpr = nullptr;
  if (!IsTreeTraversalShape(CS, Ctx.FuncName, LoopStmt, RecIf, RecCall, IsVoid,
                            &PostLoopIf, &PostLoopAction,
                            &IsBoolAllAny, &IsAnd,
                            &IsFindFirst, &FindFirstReturnExpr))
    return "";

  const Stmt *LoopBody = GetLoopBody(LoopStmt);

  // Collect local variable declarations that appear before the loop.  If any
  // of them are referenced by base cases or the loop itself, we need to
  // re-emit them inside the explicit-stack loop body, because each popped
  // frame has its own parameter values (e.g.  const Expr *Clean = E->...).
  std::vector<const DeclStmt *> PreLoopDecls;
  for (const Stmt *S : CS->body()) {
    if (S == LoopStmt)
      break;
    if (const DeclStmt *DS = dyn_cast<DeclStmt>(S))
      PreLoopDecls.push_back(DS);
  }
  std::set<std::string> PreLoopDeclNames;
  for (const DeclStmt *DS : PreLoopDecls) {
    for (auto *D : DS->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D))
        PreLoopDeclNames.insert(VD->getNameAsString());
    }
  }

  // Identify output-reference parameters (e.g., non-const lvalue references
  // used as out-arguments). They are not stored in frames.
  std::vector<bool> IsOutRef(FD->getNumParams(), false);
  for (unsigned i = 0; i < FD->getNumParams(); ++i)
    IsOutRef[i] = IsOutputReferenceParam(FD->getParamDecl(i));

  auto buildFrameCtorArgs = [&](CallExpr *CE) {
    std::string s;
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (IsOutRef[i])
        continue;
      if (!s.empty())
        s += ", ";
      s += GetArgSource(CE->getArg(i), LoopBody, Ctx.ASTCtx);
    }
    return s;
  };

  auto buildFrameCtorArgsFromParams = [&]() {
    std::string s;
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (IsOutRef[i])
        continue;
      if (!s.empty())
        s += ", ";
      s += FD->getParamDecl(i)->getNameAsString();
    }
    return s;
  };

  // Identify post-loop statements. For void tree traversals these are emitted
  // after children have been processed (post-order phase).
  std::vector<const Stmt *> PostLoopStmts;
  bool pastLoop = false;
  for (const Stmt *S : CS->body()) {
    if (S == LoopStmt) {
      pastLoop = true;
      continue;
    }
    if (pastLoop)
      PostLoopStmts.push_back(S);
  }
  bool HasPostLoop = IsVoid && !PostLoopStmts.empty();

  // Determine which pre-loop local declarations are actually referenced by
  // the base cases, the loop, or the post-loop if-statement.  For each
  // referenced variable, remember its initializer so we can inline it in
  // base-case checks and re-emit the declaration just before the loop.
  std::set<std::string> ReferencedPreLoopDecls;
  std::map<std::string, std::string> PreLoopVarReplacements;
  {
    std::vector<const Stmt *> RefTargets;
    for (const Stmt *S : CS->body()) {
      if (S == LoopStmt) {
        RefTargets.push_back(S);
        break;
      }
      if (isa<IfStmt>(S))
        RefTargets.push_back(S);
    }
    if (PostLoopIf)
      RefTargets.push_back(PostLoopIf);
    for (const DeclStmt *DS : PreLoopDecls) {
      for (auto *D : DS->decls()) {
        const VarDecl *VD = dyn_cast<VarDecl>(D);
        if (!VD)
          continue;
        const std::string &Name = VD->getNameAsString();
        for (const Stmt *T : RefTargets) {
          std::string src = T ? GetSourceText(T, Ctx.ASTCtx) : "";
          if (!src.empty() && ContainsWholeWord(src, Name)) {
            ReferencedPreLoopDecls.insert(Name);
            if (const Expr *Init = VD->getInit())
              PreLoopVarReplacements[Name] =
                  StripOuterParens(PrintExpr(Init, Ctx.ASTCtx));
            break;
          }
        }
      }
    }
  }

  auto applyPreLoopReplacements = [&](std::string txt) -> std::string {
    for (const auto &KV : PreLoopVarReplacements)
      txt = ReplaceWholeWord(txt, KV.first, "(" + KV.second + ")");
    return txt;
  };

  StackMachineCodegen smg(Ctx.FuncName, Ctx.RetType);
  const std::string frameName = smg.frameName();
  const std::string stackName = smg.stackName();
  const std::string valuesName = smg.valuesName();
  const std::string curName = smg.curName();

  IRBuilder b;
  b.comment("=== Generated tree-traversal code for function: " + Ctx.FuncName +
            " ===");
  smg.emitIncludes(b);

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  // Frame struct: one field per non-output parameter, plus bookkeeping fields
  // for the post-loop phase (done) and value-combining markers.
  for (unsigned i = 0; i < FD->getNumParams(); ++i) {
    if (IsOutRef[i])
      continue;
    smg.addParam(FD->getParamDecl(i));
  }
  const bool NeedsMarker = IsBoolAllAny || IsFindFirst;
  const bool NeedsDone = HasPostLoop || NeedsMarker;
  if (NeedsDone)
    smg.addPlainField("bool", "done", "false", /*NoUnpack=*/true);
  if (NeedsMarker) {
    smg.addPlainField("bool", "is_marker", "false", /*NoUnpack=*/true);
    smg.addPlainField("std::size_t", "marker_count", "0", /*NoUnpack=*/true);
    if (IsBoolAllAny)
      smg.addPlainField("bool", "marker_and", "false", /*NoUnpack=*/true);
  }
  smg.emitFrameStruct(b);

  auto body = IRBuilder::block();

  // Emit leading statements (those before the first base case / loop).
  for (const Stmt *S : CS->body()) {
    if (IsLoopStmt(S) || isa<IfStmt>(S))
      break;
    std::string txt = PrintStmt(S, Ctx.ASTCtx);
    if (!txt.empty())
      IRBuilder::add(body.get(), IRBuilder::rawStmt(txt));
  }

  // Initialize stack with the original arguments.
  IRBuilder::add(body.get(),
                 IRBuilder::var("std::vector<" + frameName + ">", stackName));
  if (NeedsMarker)
    smg.emitValuesDecl(body.get());
  IRBuilder::add(body.get(),
                 IRBuilder::expr(IRExpr(stackName + ".emplace_back(" +
                                        frameName + "(" +
                                        buildFrameCtorArgsFromParams() +
                                        "))")));

  auto w = IRBuilder::block();
  IRBuilder::add(w.get(), IRBuilder::var("auto", curName,
                                         IRExpr(stackName + ".back()")));
  IRBuilder::add(w.get(), IRBuilder::expr(IRExpr(stackName + ".pop_back()")));
  smg.emitUnpackCurrent(w.get());

  if (IsBoolAllAny) {
    auto mw = IRBuilder::block();
    IRBuilder::add(
        mw.get(),
        IRBuilder::var("bool", "res",
                       IRExpr(curName + ".marker_and ? true : false")));
    auto fw = IRBuilder::block();
    IRBuilder::add(fw.get(),
                   IRBuilder::var("bool", "v", IRExpr(valuesName + ".back()")));
    IRBuilder::add(fw.get(),
                   IRBuilder::expr(IRExpr(valuesName + ".pop_back()")));
    IRBuilder::add(fw.get(),
                   IRBuilder::expr(IRExpr("res = " + curName +
                                          ".marker_and ? (res && v) : (res || v)")));
    IRBuilder::add(mw.get(),
                   IRBuilder::for_("std::size_t i = 0",
                                   IRExpr("i < " + curName + ".marker_count"),
                                   "++i", std::move(fw)));
    IRBuilder::add(mw.get(),
                   IRBuilder::expr(IRExpr(valuesName + ".push_back(res)")));
    IRBuilder::add(mw.get(), IRBuilder::continue_());
    IRBuilder::add(w.get(),
                   IRBuilder::if_(IRExpr(curName + ".is_marker"),
                                  std::move(mw)));
  }

  if (IsFindFirst) {
    auto mw = IRBuilder::block();
    IRBuilder::add(mw.get(),
                   IRBuilder::var(Ctx.RetType, "res", IRExpr("nullptr")));
    auto fw = IRBuilder::block();
    IRBuilder::add(fw.get(), IRBuilder::var(Ctx.RetType, "v",
                                            IRExpr(valuesName + ".back()")));
    IRBuilder::add(fw.get(),
                   IRBuilder::expr(IRExpr(valuesName + ".pop_back()")));
    IRBuilder::add(fw.get(),
                   IRBuilder::if_(IRExpr("v"),
                                  IRBuilder::expr(IRExpr("res = v"))));
    IRBuilder::add(mw.get(),
                   IRBuilder::for_("std::size_t i = 0",
                                   IRExpr("i < " + curName + ".marker_count"),
                                   "++i", std::move(fw)));
    IRBuilder::add(mw.get(),
                   IRBuilder::expr(IRExpr(valuesName + ".push_back(res)")));
    IRBuilder::add(mw.get(), IRBuilder::continue_());
    IRBuilder::add(w.get(),
                   IRBuilder::if_(IRExpr(curName + ".is_marker"),
                                  std::move(mw)));
  }

  // How a "return" inside a pre-loop base case is rewritten.
  enum class PreLoopReturnMode {
    // Void traversal: skip the rest of this frame.
    VoidToContinue,
    // Find-first: contribute the returned value to the values stack.
    PushValue,
    // Boolean all/any: contribute the returned literal to the values stack.
    PushLiteralBool,
  };

  // Emit the statements that precede the loop: referenced local declarations
  // (recomputed per frame, since each popped frame has its own parameter
  // values) followed by the base-case checks.
  auto emitPreLoopStmts = [&](IRBlock *target, PreLoopReturnMode mode) {
    for (const Stmt *S : CS->body()) {
      if (IsLoopStmt(S))
        break;
      if (S == PostLoopIf)
        continue;
      if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
        for (auto *D : DS->decls()) {
          if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
            if (ReferencedPreLoopDecls.count(VD->getNameAsString())) {
              std::string txt =
                  NormalizeIndentation(PrintStmt(DS, Ctx.ASTCtx));
              if (!txt.empty())
                IRBuilder::add(target, IRBuilder::rawStmt(txt));
              break;
            }
          }
        }
        continue;
      }
      // Boolean all/any also lifts plain expression statements; the other
      // modes only handle if-statements (anything else was already emitted
      // before the stack initialization).
      if (!isa<IfStmt>(S) &&
          !(mode == PreLoopReturnMode::PushLiteralBool && isa<Expr>(S)))
        continue;
      std::string txt = NormalizeIndentation(PrintStmt(S, Ctx.ASTCtx));
      if (txt.empty())
        continue;
      txt = EnsureSemicolon(txt);
      txt = applyPreLoopReplacements(txt);
      switch (mode) {
      case PreLoopReturnMode::VoidToContinue:
        // Inside the explicit stack loop, a void base case "return;" means
        // "skip the rest of this frame", not "exit the function".
        if (IsVoid)
          txt = ReplaceWholeWord(txt, "return", "continue");
        break;
      case PreLoopReturnMode::PushValue:
        txt = ReplaceReturnsWithValuePush(txt, valuesName);
        break;
      case PreLoopReturnMode::PushLiteralBool:
        txt = ReplaceLiteralReturns(txt, valuesName);
        break;
      }
      IRBuilder::add(target, IRBuilder::rawStmt(txt));
    }
  };

  // Range-based for loops are rewritten to reverse iteration so that
  // children are processed in original DFS order on the explicit stack.
  const CXXForRangeStmt *ForRange = dyn_cast<CXXForRangeStmt>(LoopStmt);
  std::string rangeSrc;
  std::string loopVarSrc;
  if (ForRange) {
    rangeSrc = GetSourceText(ForRange->getRangeInit(), Ctx.ASTCtx);
    loopVarSrc = StripTrailingColon(
        GetSourceText(ForRange->getLoopVariable(), Ctx.ASTCtx));
  }

  auto emitRangeDecl = [&](IRBlock *target) {
    IRBuilder::add(target, IRBuilder::var("auto &&", "__cps_range",
                                          IRExpr(rangeSrc)));
  };

  auto emitReversedLoop = [&](IRBlock *target, const std::string &pushStr) {
    auto fb = IRBuilder::block();
    IRBuilder::add(fb.get(), IRBuilder::rawStmt(loopVarSrc + " = *__cps_it;"));
    IRBuilder::add(fb.get(), IRBuilder::rawStmt(pushStr));
    IRBuilder::add(target,
                   IRBuilder::for_("auto __cps_it = __cps_range.rbegin()",
                                   IRExpr("__cps_it != __cps_range.rend()"),
                                   "++__cps_it", std::move(fb)));
  };

  // Boolean all/any and find-first traversals over a range loop: count the
  // children, push the empty result directly when there are none, push a
  // marker frame that later combines the child values, then push one frame
  // per child in reverse order.
  auto emitCountedRangeTraversal = [&](IRBlock *target,
                                       const std::string &markerPush,
                                       const std::string &emptyPushValue,
                                       const std::string &childPush) {
    auto rb = IRBuilder::block();
    emitRangeDecl(rb.get());
    IRBuilder::add(rb.get(),
                   IRBuilder::var("std::size_t", "__cps_n", IRExpr("0")));
    IRBuilder::add(rb.get(),
                   IRBuilder::for_("auto __cps_it = __cps_range.begin()",
                                   IRExpr("__cps_it != __cps_range.end()"),
                                   "++__cps_it",
                                   IRBuilder::expr(IRExpr("++__cps_n"))));
    auto eb = IRBuilder::block();
    IRBuilder::add(eb.get(),
                   IRBuilder::expr(IRExpr(valuesName + ".push_back(" +
                                          emptyPushValue + ")")));
    IRBuilder::add(eb.get(), IRBuilder::continue_());
    IRBuilder::add(rb.get(),
                   IRBuilder::if_(IRExpr("__cps_n == 0"), std::move(eb)));
    IRBuilder::add(rb.get(), IRBuilder::rawStmt(markerPush));
    emitReversedLoop(rb.get(), childPush);
    IRBuilder::add(rb.get(), IRBuilder::continue_());
    IRBuilder::add(target, std::move(rb));
  };

  // Rewrite the source of a non-range loop so that the statement containing
  // the recursive call is replaced by pushStr.  Returns false when the
  // replacement anchor cannot be located (source-text matching is inherently
  // fragile; fail loudly instead of emitting code that drops the push).
  auto rewriteNonRangeLoop = [&](std::string &loopSrc,
                                 const std::string &pushStr) -> bool {
    std::string anchor;
    if (RecIf) {
      anchor = GetSourceText(RecIf, Ctx.ASTCtx);
    } else {
      // Replace the statement that directly contains the recursive call.
      const Stmt *Encl =
          FindDirectChildStmtContainingCall(LoopBody, RecCall);
      anchor = Encl ? GetSourceText(Encl, Ctx.ASTCtx)
                    : GetSourceText(RecCall, Ctx.ASTCtx);
    }
    if (anchor.empty())
      return false;
    size_t pos = loopSrc.find(anchor);
    if (pos == std::string::npos)
      return false;
    loopSrc.replace(pos, anchor.size(), pushStr);
    size_t p2 = loopSrc.find(";;", pos);
    if (p2 != std::string::npos)
      loopSrc.replace(p2, 2, ";");
    return true;
  };

  auto emitPostLoopIf = [&](IRBlock *target) -> bool {
    if (!PostLoopIf || !PostLoopAction)
      return true;
    const Stmt *InnerThen = GetInnermostThen(PostLoopIf);
    std::string postIfSrc = GetSourceText(PostLoopIf, Ctx.ASTCtx);
    std::string innerThenSrc = GetSourceText(InnerThen, Ctx.ASTCtx);
    std::string actionSrc =
        EnsureSemicolon(GetSourceText(PostLoopAction, Ctx.ASTCtx));
    std::string replacement = "{\n" + actionSrc + "\ncontinue;\n}";
    size_t pos = postIfSrc.find(innerThenSrc);
    if (pos == std::string::npos)
      return false;
    postIfSrc.replace(pos, innerThenSrc.size(), replacement);
    IRBuilder::add(target,
                   IRBuilder::rawStmt(NormalizeIndentation(postIfSrc)));
    return true;
  };

  auto anchorError = [&]() {
    return MakeError(CpsErrorCode::InternalError,
                     "could not locate the recursive call in the loop source",
                     Ctx.FuncName);
  };

  if (HasPostLoop) {
    auto dw = IRBuilder::block();
    for (const Stmt *S : PostLoopStmts) {
      std::string txt = NormalizeIndentation(PrintStmt(S, Ctx.ASTCtx));
      if (!txt.empty()) {
        txt = EnsureSemicolon(txt);
        IRBuilder::add(dw.get(), IRBuilder::rawStmt(txt));
      }
    }
    auto ew = IRBuilder::block();
    emitPreLoopStmts(ew.get(), PreLoopReturnMode::VoidToContinue);

    std::string pushStr = stackName + ".emplace_back(" + frameName + "(" +
                          buildFrameCtorArgs(RecCall) + "));";
    // Marker is pushed with current parameters, not recursive-call arguments.
    std::string markerStr = stackName + ".emplace_back(" + frameName + "(" +
                            buildFrameCtorArgsFromParams() + ", true));";

    if (ForRange) {
      auto rb = IRBuilder::block();
      emitRangeDecl(rb.get());
      IRBuilder::add(rb.get(), IRBuilder::rawStmt(markerStr));
      emitReversedLoop(rb.get(), pushStr);
      IRBuilder::add(ew.get(), std::move(rb));
    } else {
      IRBuilder::add(ew.get(), IRBuilder::rawStmt(markerStr));
      std::string loopSrc = GetSourceText(LoopStmt, Ctx.ASTCtx);
      if (!rewriteNonRangeLoop(loopSrc, pushStr))
        return anchorError();
      IRBuilder::add(ew.get(),
                     IRBuilder::rawStmt(NormalizeIndentation(loopSrc)));
    }
    IRBuilder::add(w.get(), IRBuilder::if_(IRExpr(curName + ".done"),
                                           std::move(dw), std::move(ew)));
  } else {
    if (IsBoolAllAny) {
      emitPreLoopStmts(w.get(), PreLoopReturnMode::PushLiteralBool);

      // Boolean all/any over a range-based for loop.  Push a marker frame
      // followed by one frame per child in reverse order.
      std::string emptyValue = IsAnd ? "true" : "false";
      std::string markerPush = stackName + ".emplace_back(" + frameName + "(" +
                               buildFrameCtorArgsFromParams() +
                               ", false, true, __cps_n, " + emptyValue + "));";
      std::string childPush = stackName + ".emplace_back(" + frameName + "(" +
                              buildFrameCtorArgs(RecCall) + "));";
      emitCountedRangeTraversal(w.get(), markerPush, emptyValue, childPush);
    } else if (IsFindFirst) {
      emitPreLoopStmts(w.get(), PreLoopReturnMode::PushValue);

      // Find-first search over a range-based for loop.  Push a marker frame
      // followed by one frame per child in reverse order.  The marker later
      // picks the first non-null child result.
      std::string markerPush = stackName + ".emplace_back(" + frameName + "(" +
                               buildFrameCtorArgsFromParams() +
                               ", false, true, __cps_n));";
      std::string childPush = stackName + ".emplace_back(" + frameName + "(" +
                              buildFrameCtorArgs(RecCall) + "));";
      emitCountedRangeTraversal(w.get(), markerPush, "nullptr", childPush);
    } else {
      emitPreLoopStmts(w.get(), PreLoopReturnMode::VoidToContinue);

      // Emit the loop. For range-based for loops we rewrite to reverse
      // iteration so that children are processed in original DFS order on
      // the explicit stack.
      std::string pushStr = stackName + ".emplace_back(" + frameName + "(" +
                            buildFrameCtorArgs(RecCall) + "));";
      if (ForRange) {
        auto rb = IRBuilder::block();
        emitRangeDecl(rb.get());
        emitReversedLoop(rb.get(), pushStr);
        IRBuilder::add(w.get(), std::move(rb));
      } else {
        std::string loopSrc = GetSourceText(LoopStmt, Ctx.ASTCtx);
        if (!rewriteNonRangeLoop(loopSrc, pushStr))
          return anchorError();
        IRBuilder::add(w.get(),
                       IRBuilder::rawStmt(NormalizeIndentation(loopSrc)));
      }
      if (!emitPostLoopIf(w.get()))
        return anchorError();
    }
  }
  IRBuilder::add(body.get(),
                 IRBuilder::while_(IRExpr("!" + stackName + ".empty()"),
                                   std::move(w)));

  // Emit final return.
  const ReturnStmt *FinalRet = nullptr;
  for (const Stmt *S : CS->body()) {
    if (IsLoopStmt(S))
      continue; // reset after loop
    if (const ReturnStmt *RS = dyn_cast<ReturnStmt>(S)) {
      FinalRet = RS;
      break;
    }
  }
  if (IsBoolAllAny) {
    std::string emptyValue = IsAnd ? "true" : "false";
    IRBuilder::add(body.get(),
                   IRBuilder::ret(IRExpr(valuesName + ".empty() ? " +
                                         emptyValue + " : " + valuesName +
                                         ".back()")));
  } else if (IsFindFirst) {
    std::string defaultExpr = "nullptr";
    if (FinalRet && FinalRet->getRetValue())
      defaultExpr =
          StripOuterParens(PrintExpr(FinalRet->getRetValue(), Ctx.ASTCtx));
    IRBuilder::add(body.get(),
                   IRBuilder::ret(IRExpr(valuesName + ".empty() ? " +
                                         defaultExpr + " : " + valuesName +
                                         ".back()")));
  } else {
    if (FinalRet && !IsVoid) {
      std::string txt = NormalizeIndentation(PrintStmt(FinalRet, Ctx.ASTCtx));
      if (!txt.empty()) {
        txt = EnsureSemicolon(txt);
        IRBuilder::add(body.get(), IRBuilder::rawStmt(txt));
      }
    }
  }

  b.function(sig, std::move(body));
  return PrintGeneratedUnit(b.unit);
}

} // namespace cps
