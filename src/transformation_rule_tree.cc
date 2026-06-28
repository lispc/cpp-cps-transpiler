#include "transformation_rules.h"
#include "transformation_rule.h"
#include "transformation_rules_helpers.h"
#include "code_emitter.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Lex/Lexer.h"
#include <string>
#include <vector>

namespace cps {

using namespace clang;

namespace {

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
std::string ReplaceLiteralReturns(const std::string &S) {
  std::string r = S;
  size_t pos = 0;
  const std::string falseRepl = "{ values.push_back(false); continue; }";
  while ((pos = r.find("return false;", pos)) != std::string::npos) {
    r.replace(pos, 13, falseRepl);
    pos += falseRepl.size();
  }
  pos = 0;
  const std::string trueRepl = "{ values.push_back(true); continue; }";
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

  std::string frameName = Ctx.FuncName + "Frame";

  CodeEmitter e;
  e.raw("// === Generated tree-traversal code for function: " + Ctx.FuncName +
        " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  e.block("struct " + frameName, [&](CodeEmitter &b) {
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (IsOutRef[i])
        continue;
      b.line(GetParamStorageType(FD->getParamDecl(i)) + " " +
             FD->getParamDecl(i)->getNameAsString() + ";");
    }
    bool NeedsMarker = IsBoolAllAny || IsFindFirst;
    bool NeedsDone = HasPostLoop || NeedsMarker;
    if (NeedsDone)
      b.line("bool done;");
    if (NeedsMarker) {
      b.line("bool is_marker;");
      b.line("std::size_t marker_count;");
      if (IsBoolAllAny)
        b.line("bool marker_and;");
    }
    std::string ctor = frameName + "(";
    std::string init;
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (IsOutRef[i])
        continue;
      std::string p = FD->getParamDecl(i)->getNameAsString();
      if (!init.empty()) {
        ctor += ", ";
        init += ", ";
      }
      ctor += GetParamStorageType(FD->getParamDecl(i)) + " " + p + "_";
      init += p + "(" + p + "_)";
    }
    if (NeedsDone) {
      if (!init.empty()) {
        ctor += ", ";
        init += ", ";
      }
      ctor += "bool done_ = false";
      init += "done(done_)";
    }
    if (NeedsMarker) {
      if (!init.empty()) {
        ctor += ", ";
        init += ", ";
      }
      ctor += "bool is_marker_ = false";
      init += "is_marker(is_marker_)";
      ctor += ", std::size_t marker_count_ = 0";
      init += ", marker_count(marker_count_)";
      if (IsBoolAllAny) {
        ctor += ", bool marker_and_ = false";
        init += ", marker_and(marker_and_)";
      }
    }
    ctor += ")";
    if (!init.empty())
      ctor += " : " + init;
    ctor += " {}";
    b.line(ctor);
  }, ";");
  e.nl();

  e.block(sig, [&](CodeEmitter &b) {
    // Emit leading statements (those before the first base case / loop).
    for (const Stmt *S : CS->body()) {
      if (IsLoopStmt(S) || isa<IfStmt>(S))
        break;
      std::string txt = PrintStmt(S, Ctx.ASTCtx);
      if (!txt.empty())
        b.line(txt);
    }

    // Initialize stack with the original arguments.
    b.line("std::vector<" + frameName + "> stack;");
    if (IsBoolAllAny)
      b.line("std::vector<bool> values;");
    if (IsFindFirst)
      b.line("std::vector<" + Ctx.RetType + "> values;");
    std::string push0 = "stack.emplace_back(" + frameName + "(";
    bool firstPush = true;
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (IsOutRef[i])
        continue;
      if (!firstPush)
        push0 += ", ";
      push0 += FD->getParamDecl(i)->getNameAsString();
      firstPush = false;
    }
    push0 += "));";
    b.line(push0);

    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("auto cur = stack.back();");
      w.line("stack.pop_back();");
      for (unsigned i = 0; i < FD->getNumParams(); ++i) {
        if (IsOutRef[i])
          continue;
        w.line("auto " + FD->getParamDecl(i)->getNameAsString() + " = cur." +
               FD->getParamDecl(i)->getNameAsString() + ";");
      }

      if (IsBoolAllAny) {
        w.block("if (cur.is_marker)", [&](CodeEmitter &mw) {
          mw.line("bool res = cur.marker_and ? true : false;");
          mw.block("for (std::size_t i = 0; i < cur.marker_count; ++i)",
                   [&](CodeEmitter &fw) {
                     fw.line("bool v = values.back(); values.pop_back();");
                     fw.line("res = cur.marker_and ? (res && v) : (res || v);");
                   });
          mw.line("values.push_back(res);");
          mw.line("continue;");
        });
      }

      if (IsFindFirst) {
        w.block("if (cur.is_marker)", [&](CodeEmitter &mw) {
          mw.line(Ctx.RetType + " res = nullptr;");
          mw.block("for (std::size_t i = 0; i < cur.marker_count; ++i)",
                   [&](CodeEmitter &fw) {
                     fw.line(Ctx.RetType + " v = values.back(); values.pop_back();");
                     fw.line("if (v) res = v;");
                   });
          mw.line("values.push_back(res);");
          mw.line("continue;");
        });
      }

      auto emitBaseCases = [&](CodeEmitter &target, bool inPostLoop) {
        for (const Stmt *S : CS->body()) {
          if (IsLoopStmt(S))
            break;
          if (S == PostLoopIf)
            continue;
          if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
            std::string txt = NormalizeIndentation(PrintStmt(IfS, Ctx.ASTCtx));
            if (!txt.empty()) {
              txt = EnsureSemicolon(txt);
              // Inside the explicit stack loop, a void base case "return;"
              // means "skip the rest of this frame", not "exit the function".
              if (IsVoid && !inPostLoop)
                txt = ReplaceWholeWord(txt, "return", "continue");
              target.raw(Indent(txt, target.current_indent() * 2) + "\n");
            }
          }
        }
      };

      auto emitFindFirstBaseCases = [&](CodeEmitter &target) {
        for (const Stmt *S : CS->body()) {
          if (IsLoopStmt(S))
            break;
          if (S == PostLoopIf)
            continue;
          if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
            std::string txt = NormalizeIndentation(PrintStmt(IfS, Ctx.ASTCtx));
            if (!txt.empty()) {
              txt = EnsureSemicolon(txt);
              txt = ReplaceReturnsWithValuePush(txt, "values");
              target.raw(Indent(txt, target.current_indent() * 2) + "\n");
            }
          }
        }
      };

      auto emitPostLoopIf = [&](CodeEmitter &target) {
        if (!PostLoopIf || !PostLoopAction)
          return;
        const Stmt *InnerThen = GetInnermostThen(PostLoopIf);
        std::string postIfSrc = GetSourceText(PostLoopIf, Ctx.ASTCtx);
        std::string innerThenSrc = GetSourceText(InnerThen, Ctx.ASTCtx);
        std::string actionSrc =
            EnsureSemicolon(GetSourceText(PostLoopAction, Ctx.ASTCtx));
        std::string replacement = "{\n" + actionSrc + "\ncontinue;\n}";
        size_t pos = postIfSrc.find(innerThenSrc);
        if (pos != std::string::npos) {
          postIfSrc.replace(pos, innerThenSrc.size(), replacement);
          target.raw(Indent(NormalizeIndentation(postIfSrc),
                            target.current_indent() * 2) +
                     "\n");
        }
      };

      if (HasPostLoop) {
        w.block("if (cur.done)", [&](CodeEmitter &dw) {
          for (const Stmt *S : PostLoopStmts) {
            std::string txt = NormalizeIndentation(PrintStmt(S, Ctx.ASTCtx));
            if (!txt.empty()) {
              txt = EnsureSemicolon(txt);
              dw.raw(Indent(txt, dw.current_indent() * 2) + "\n");
            }
          }
        });
        w.block("else", [&](CodeEmitter &ew) {
          emitBaseCases(ew, false);

          std::string pushStr =
              "stack.emplace_back(" + frameName + "(" +
              buildFrameCtorArgs(RecCall) + "));";
          std::string markerStr =
              "stack.emplace_back(" + frameName + "(" +
              buildFrameCtorArgsFromParams() + ", true));";

          const CXXForRangeStmt *ForRange = dyn_cast<CXXForRangeStmt>(LoopStmt);
          if (ForRange) {
            std::string rangeSrc =
                GetSourceText(ForRange->getRangeInit(), Ctx.ASTCtx);
            std::string loopVarSrc = StripTrailingColon(
                GetSourceText(ForRange->getLoopVariable(), Ctx.ASTCtx));
            ew.block("", [&](CodeEmitter &rb) {
              rb.line("auto &&__cps_range = " + rangeSrc + ";");
              rb.line(markerStr);
              rb.block("for (auto __cps_it = __cps_range.rbegin(); __cps_it != "
                       "__cps_range.rend(); ++__cps_it)",
                       [&](CodeEmitter &fb) {
                         fb.line(loopVarSrc + " = *__cps_it;");
                         fb.line(pushStr);
                       });
            });
          } else {
            // Marker is pushed with current parameters, not recursive-call
            // arguments; reuse the current parameter names.
            std::string markerStrNonRange =
                "stack.emplace_back(" + frameName + "(" +
                buildFrameCtorArgsFromParams() + ", true));";
            ew.line(markerStrNonRange);
            std::string loopSrc = GetSourceText(LoopStmt, Ctx.ASTCtx);
            if (RecIf) {
              std::string recIfSrc = GetSourceText(RecIf, Ctx.ASTCtx);
              size_t pos = loopSrc.find(recIfSrc);
              if (pos != std::string::npos) {
                loopSrc.replace(pos, recIfSrc.size(), pushStr);
                size_t p2 = loopSrc.find(";;", pos);
                if (p2 != std::string::npos)
                  loopSrc.replace(p2, 2, ";");
              }
            } else {
              // Replace the statement that directly contains the recursive call.
              const Stmt *Encl =
                  FindDirectChildStmtContainingCall(LoopBody, RecCall);
              std::string toReplace = Encl ? GetSourceText(Encl, Ctx.ASTCtx)
                                           : GetSourceText(RecCall, Ctx.ASTCtx);
              if (!toReplace.empty()) {
                size_t pos = loopSrc.find(toReplace);
                if (pos != std::string::npos) {
                  loopSrc.replace(pos, toReplace.size(), pushStr);
                  size_t p2 = loopSrc.find(";;", pos);
                  if (p2 != std::string::npos)
                    loopSrc.replace(p2, 2, ";");
                }
              }
            }
            ew.raw(Indent(NormalizeIndentation(loopSrc), ew.current_indent() * 2) +
                   "\n");
          }
        });
      } else {
        if (IsBoolAllAny) {
          // Emit pre-loop base-case statements.  Literal boolean returns are
          // converted into pushes onto the values stack so that the marker
          // frame can combine them with AND/OR.
          for (const Stmt *S : CS->body()) {
            if (S == LoopStmt)
              break;
            if (S == PostLoopIf)
              continue;
            if (!isa<IfStmt>(S) && !isa<Expr>(S))
              continue;
            std::string txt = NormalizeIndentation(PrintStmt(S, Ctx.ASTCtx));
            if (txt.empty())
              continue;
            txt = EnsureSemicolon(txt);
            txt = ReplaceLiteralReturns(txt);
            w.raw(Indent(txt, w.current_indent() * 2) + "\n");
          }

          // Boolean all/any over a range-based for loop.  Push a marker frame
          // followed by one frame per child in reverse order.
          const CXXForRangeStmt *ForRange = dyn_cast<CXXForRangeStmt>(LoopStmt);
          std::string rangeSrc =
              GetSourceText(ForRange->getRangeInit(), Ctx.ASTCtx);
          std::string loopVarSrc = StripTrailingColon(
              GetSourceText(ForRange->getLoopVariable(), Ctx.ASTCtx));
          std::string emptyValue = IsAnd ? "true" : "false";
          std::string markerPush = "stack.emplace_back(" + frameName + "(" +
                                   buildFrameCtorArgsFromParams() +
                                   ", false, true, __cps_n, " + emptyValue +
                                   "));";
          std::string childPush = "stack.emplace_back(" + frameName + "(" +
                                  buildFrameCtorArgs(RecCall) + "));";
          w.block("", [&](CodeEmitter &rb) {
            rb.line("auto &&__cps_range = " + rangeSrc + ";");
            rb.line("std::size_t __cps_n = 0;");
            rb.block("for (auto __cps_it = __cps_range.begin(); __cps_it != "
                     "__cps_range.end(); ++__cps_it)",
                     [&](CodeEmitter &cb) { cb.line("++__cps_n;"); });
            rb.block("if (__cps_n == 0)", [&](CodeEmitter &eb) {
              eb.line("values.push_back(" + emptyValue + ");");
              eb.line("continue;");
            });
            rb.line(markerPush);
            rb.block("for (auto __cps_it = __cps_range.rbegin(); __cps_it != "
                     "__cps_range.rend(); ++__cps_it)",
                     [&](CodeEmitter &fb) {
                       fb.line(loopVarSrc + " = *__cps_it;");
                       fb.line(childPush);
                     });
            rb.line("continue;");
          });
        } else if (IsFindFirst) {
          emitFindFirstBaseCases(w);

          // Find-first search over a range-based for loop.  Push a marker frame
          // followed by one frame per child in reverse order.  The marker later
          // picks the first non-null child result.
          const CXXForRangeStmt *ForRange = dyn_cast<CXXForRangeStmt>(LoopStmt);
          std::string rangeSrc =
              GetSourceText(ForRange->getRangeInit(), Ctx.ASTCtx);
          std::string loopVarSrc = StripTrailingColon(
              GetSourceText(ForRange->getLoopVariable(), Ctx.ASTCtx));
          std::string markerPush = "stack.emplace_back(" + frameName + "(" +
                                   buildFrameCtorArgsFromParams() +
                                   ", false, true, __cps_n));";
          std::string childPush = "stack.emplace_back(" + frameName + "(" +
                                  buildFrameCtorArgs(RecCall) + "));";
          w.block("", [&](CodeEmitter &rb) {
            rb.line("auto &&__cps_range = " + rangeSrc + ";");
            rb.line("std::size_t __cps_n = 0;");
            rb.block("for (auto __cps_it = __cps_range.begin(); __cps_it != "
                     "__cps_range.end(); ++__cps_it)",
                     [&](CodeEmitter &cb) { cb.line("++__cps_n;"); });
            rb.block("if (__cps_n == 0)", [&](CodeEmitter &eb) {
              eb.line("values.push_back(nullptr);");
              eb.line("continue;");
            });
            rb.line(markerPush);
            rb.block("for (auto __cps_it = __cps_range.rbegin(); __cps_it != "
                     "__cps_range.rend(); ++__cps_it)",
                     [&](CodeEmitter &fb) {
                       fb.line(loopVarSrc + " = *__cps_it;");
                       fb.line(childPush);
                     });
            rb.line("continue;");
          });
        } else {
          emitBaseCases(w, false);

          // Emit the loop. For range-based for loops we rewrite to reverse
          // iteration so that children are processed in original DFS order on
          // the explicit stack.
          std::string pushStr = "stack.emplace_back(" + frameName + "(" +
                                buildFrameCtorArgs(RecCall) + "));";
          const CXXForRangeStmt *ForRange = dyn_cast<CXXForRangeStmt>(LoopStmt);
          if (ForRange) {
            std::string rangeSrc =
                GetSourceText(ForRange->getRangeInit(), Ctx.ASTCtx);
            std::string loopVarSrc = StripTrailingColon(
                GetSourceText(ForRange->getLoopVariable(), Ctx.ASTCtx));
            w.block("", [&](CodeEmitter &rb) {
              rb.line("auto &&__cps_range = " + rangeSrc + ";");
              rb.block("for (auto __cps_it = __cps_range.rbegin(); __cps_it != "
                       "__cps_range.rend(); ++__cps_it)",
                       [&](CodeEmitter &fb) {
                         fb.line(loopVarSrc + " = *__cps_it;");
                         fb.line(pushStr);
                       });
            });
          } else {
            std::string loopSrc = GetSourceText(LoopStmt, Ctx.ASTCtx);
            if (RecIf) {
              std::string recIfSrc = GetSourceText(RecIf, Ctx.ASTCtx);
              size_t pos = loopSrc.find(recIfSrc);
              if (pos != std::string::npos) {
                loopSrc.replace(pos, recIfSrc.size(), pushStr);
                size_t p2 = loopSrc.find(";;", pos);
                if (p2 != std::string::npos)
                  loopSrc.replace(p2, 2, ";");
              }
            } else {
              // Replace the statement that directly contains the recursive call.
              const Stmt *Encl =
                  FindDirectChildStmtContainingCall(LoopBody, RecCall);
              std::string toReplace = Encl ? GetSourceText(Encl, Ctx.ASTCtx)
                                           : GetSourceText(RecCall, Ctx.ASTCtx);
              if (!toReplace.empty()) {
                size_t pos = loopSrc.find(toReplace);
                if (pos != std::string::npos) {
                  loopSrc.replace(pos, toReplace.size(), pushStr);
                  size_t p2 = loopSrc.find(";;", pos);
                  if (p2 != std::string::npos)
                    loopSrc.replace(p2, 2, ";");
                }
              }
            }
            w.raw(Indent(NormalizeIndentation(loopSrc), w.current_indent() * 2) +
                   "\n");
          }
          emitPostLoopIf(w);
        }
      }
    });

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
      b.line("return values.empty() ? " + emptyValue + " : values.back();");
    } else if (IsFindFirst) {
      std::string defaultExpr = "nullptr";
      if (FinalRet && FinalRet->getRetValue())
        defaultExpr = StripOuterParens(
            PrintExpr(FinalRet->getRetValue(), Ctx.ASTCtx));
      b.line("return values.empty() ? " + defaultExpr + " : values.back();");
    } else {
      if (FinalRet && !IsVoid) {
        std::string txt = NormalizeIndentation(PrintStmt(FinalRet, Ctx.ASTCtx));
        if (!txt.empty()) {
          txt = EnsureSemicolon(txt);
          b.line(txt);
        }
      }
    }
  });

  return e.str();
}

int TreeTraversalRule::cost() const { return RuleCatalog::TreeTraversal.Cost; }

const char *TreeTraversalRule::name() const {
  return RuleCatalog::TreeTraversal.Name;
}

} // namespace cps
