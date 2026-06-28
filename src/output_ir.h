#ifndef OUTPUT_IR_H
#define OUTPUT_IR_H

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cps {

// Lightweight output IR for generated C++ code.
// This is *not* a full C++ AST; expressions remain string-backed so that the
// transpiler can keep using Clang's pretty-printer for the parts it already
// understands.  The IR gives us structured control flow and top-level
// declarations, which removes most of the ad-hoc string concatenation that
// currently lives in the transformation rules.

struct IRExpr {
  std::string text;
  IRExpr() = default;
  explicit IRExpr(std::string t) : text(std::move(t)) {}
  explicit IRExpr(const char *t) : text(t) {}
  bool empty() const { return text.empty(); }
};

struct IRStmt {
  virtual ~IRStmt() = default;
};

struct IRBlock : IRStmt {
  std::vector<std::unique_ptr<IRStmt>> body;
};

struct IRIf : IRStmt {
  IRExpr cond;
  std::unique_ptr<IRStmt> thenBranch;
  std::unique_ptr<IRStmt> elseBranch; // nullptr if no else
};

struct IRWhile : IRStmt {
  IRExpr cond;
  std::unique_ptr<IRStmt> body; // normally a IRBlock
};

struct IRReturn : IRStmt {
  std::optional<IRExpr> value;
  IRReturn() = default;
  explicit IRReturn(IRExpr v) : value(std::move(v)) {}
};

struct IRExprStmt : IRStmt {
  IRExpr expr;
  explicit IRExprStmt(IRExpr e) : expr(std::move(e)) {}
};

struct IRVarDecl : IRStmt {
  std::string type;
  std::string name;
  std::optional<IRExpr> init;
  IRVarDecl(std::string t, std::string n, std::optional<IRExpr> i = std::nullopt)
      : type(std::move(t)), name(std::move(n)), init(std::move(i)) {}
};

// Escape hatch for source-text fragments that are not yet modelled in the IR.
struct IRRaw : IRStmt {
  std::string text;
  explicit IRRaw(std::string t) : text(std::move(t)) {}
};

struct IRTopLevel {
  virtual ~IRTopLevel() = default;
};

struct IRRawTop : IRTopLevel {
  std::string text;
  explicit IRRawTop(std::string t) : text(std::move(t)) {}
};

struct IRInclude : IRTopLevel {
  std::string header;
  explicit IRInclude(std::string h) : header(std::move(h)) {}
};

struct IRStructField {
  std::string type;
  std::string name;
  IRStructField(std::string t, std::string n) : type(std::move(t)), name(std::move(n)) {}
};

struct IRStructCtor {
  std::vector<std::pair<std::string, std::string>> params; // (type, name)
  std::vector<std::pair<std::string, std::string>> init;   // (field, arg)
  IRStructCtor(std::vector<std::pair<std::string, std::string>> p,
             std::vector<std::pair<std::string, std::string>> i)
      : params(std::move(p)), init(std::move(i)) {}
};

struct IRStructDef : IRTopLevel {
  std::string name;
  std::vector<IRStructField> fields;
  std::vector<IRStructCtor> ctors;
  IRStructDef(std::string n, std::vector<IRStructField> f, std::vector<IRStructCtor> c)
      : name(std::move(n)), fields(std::move(f)), ctors(std::move(c)) {}
};

struct IREnumDef : IRTopLevel {
  std::string name;
  std::vector<std::string> enumerators;
  IREnumDef(std::string n, std::vector<std::string> e)
      : name(std::move(n)), enumerators(std::move(e)) {}
};

struct IRFunctionDef : IRTopLevel {
  // Pre-built signature string, e.g. "int fib(int n)".
  std::string signature;
  std::unique_ptr<IRBlock> body;
  IRFunctionDef(std::string sig, std::unique_ptr<IRBlock> b)
      : signature(std::move(sig)), body(std::move(b)) {}
};

struct IRUnit {
  std::vector<std::unique_ptr<IRTopLevel>> items;
};

// Print a generated unit to a C++ source string.
std::string PrintGeneratedUnit(const IRUnit &unit);

// ============================================================
// Fluent builder helper
// ============================================================

class IRBuilder {
public:
  IRUnit unit;

  IRBuilder &raw(const std::string &text);
  IRBuilder &include(const std::string &header);
  IRBuilder &blank();

  IRBuilder &function(const std::string &signature,
                      std::unique_ptr<IRBlock> body);

  // Convenience: create and return a new IRBlock.
  static std::unique_ptr<IRBlock> block();

  // Convenience: add a statement to a block.
  static IRBlock &add(IRBlock *blk, std::unique_ptr<IRStmt> s);

  static std::unique_ptr<IRIf>
  if_(IRExpr cond, std::unique_ptr<IRStmt> thenBranch,
      std::unique_ptr<IRStmt> elseBranch = nullptr);

  static std::unique_ptr<IRWhile>
  while_(IRExpr cond, std::unique_ptr<IRBlock> body);

  static std::unique_ptr<IRReturn> ret();
  static std::unique_ptr<IRReturn> ret(IRExpr value);

  static std::unique_ptr<IRExprStmt> expr(IRExpr e);
  static std::unique_ptr<IRVarDecl> var(const std::string &type,
                                          const std::string &name,
                                          std::optional<IRExpr> init = std::nullopt);
  static std::unique_ptr<IRRaw> rawStmt(const std::string &text);
};

} // namespace cps

#endif // OUTPUT_IR_H
