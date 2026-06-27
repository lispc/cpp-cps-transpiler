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
  return IsTreeTraversalShape(CS, Ctx.FuncName, Loop, RecIf, RecCall);
}

std::string TreeTraversalRule::apply(const FunctionDecl *FD,
                                     const BodyAnalysis &BA,
                                     GenContext &Ctx) const {
  (void)BA;
  const CompoundStmt *CS = dyn_cast<CompoundStmt>(FD->getBody());

  // Locate the loop and its recursive-call IfStmt.
  const Stmt *LoopStmt = nullptr;
  for (const Stmt *S : CS->body()) {
    if (IsLoopStmt(S)) {
      LoopStmt = S;
      break;
    }
  }
  const Stmt *LoopBody = GetLoopBody(LoopStmt);

  CallExpr *RecCall = nullptr;
  const IfStmt *RecIf =
      FindRecursiveCallReturnIf(LoopBody, Ctx.FuncName, RecCall);

  // Build frame struct.
  std::string frameName = Ctx.FuncName + "Frame";

  CodeEmitter e;
  e.raw("// === Generated tree-traversal code for function: " + Ctx.FuncName +
        " ===\n\n");
  e.line("#include <vector>");
  e.nl();

  std::string sig = BuildFunctionSignature(FD, Ctx.RetType);

  e.block("struct " + frameName, [&](CodeEmitter &b) {
    for (unsigned i = 0; i < FD->getNumParams(); ++i)
      b.line(GetParamStorageType(FD->getParamDecl(i)) + " " +
             FD->getParamDecl(i)->getNameAsString() + ";");
    std::string ctor = frameName + "(";
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        ctor += ", ";
      ctor += GetParamStorageType(FD->getParamDecl(i)) + " " +
              FD->getParamDecl(i)->getNameAsString() + "_";
    }
    ctor += ")";
    std::string init;
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      std::string p = FD->getParamDecl(i)->getNameAsString();
      if (!init.empty())
        init += ", ";
      init += p + "(" + p + "_)";
    }
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
    std::string push0 = "stack.emplace_back(" + frameName + "(";
    for (unsigned i = 0; i < FD->getNumParams(); ++i) {
      if (i > 0)
        push0 += ", ";
      push0 += FD->getParamDecl(i)->getNameAsString();
    }
    push0 += "));";
    b.line(push0);

    b.block("while (!stack.empty())", [&](CodeEmitter &w) {
      w.line("auto cur = stack.back();");
      w.line("stack.pop_back();");
      for (unsigned i = 0; i < FD->getNumParams(); ++i)
        w.line("auto " + FD->getParamDecl(i)->getNameAsString() + " = cur." +
               FD->getParamDecl(i)->getNameAsString() + ";");

      // Emit base cases (all IfStmts before the loop).
      for (const Stmt *S : CS->body()) {
        if (IsLoopStmt(S))
          break;
        if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
          std::string txt = PrintStmt(IfS, Ctx.ASTCtx);
          if (!txt.empty()) {
            txt = EnsureSemicolon(txt);
            w.raw(Indent(txt, w.current_indent()) + "\n");
          }
        }
      }

      // Emit the loop, replacing the recursive-call IfStmt with a push.
      std::string loopSrc = GetSourceText(LoopStmt, Ctx.ASTCtx);
      std::string recIfSrc = GetSourceText(RecIf, Ctx.ASTCtx);

      std::string pushStr = "stack.emplace_back(" + frameName + "(";
      for (unsigned i = 0; i < RecCall->getNumArgs(); ++i) {
        if (i > 0)
          pushStr += ", ";
        pushStr += PrintExpr(RecCall->getArg(i), Ctx.ASTCtx);
      }
      pushStr += "));";

      size_t pos = loopSrc.find(recIfSrc);
      if (pos != std::string::npos) {
        loopSrc.replace(pos, recIfSrc.size(), pushStr);
        // getSourceText may leave the original trailing ';' outside the
        // replaced range, causing double semicolons.
        size_t p2 = loopSrc.find(";;", pos);
        if (p2 != std::string::npos)
          loopSrc.replace(p2, 2, ";");
      }

      w.raw(Indent(NormalizeIndentation(loopSrc), w.current_indent()) + "\n");
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
    if (FinalRet) {
      std::string txt = PrintStmt(FinalRet, Ctx.ASTCtx);
      if (!txt.empty()) {
        txt = EnsureSemicolon(txt);
        b.line(txt);
      }
    }
  });

  return e.str();
}

int TreeTraversalRule::cost() const { return 150; }

const char *TreeTraversalRule::name() const { return "TreeTraversalRule"; }

} // namespace cps
