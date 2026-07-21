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

// C++ for loop. `init` covers both classic ("int i = 0") and range-for
// ("auto child : node->children") forms; cond/incr are empty for range-for.
struct IRFor : IRStmt {
  std::string init;
  IRExpr cond;
  std::string incr;
  std::unique_ptr<IRStmt> body; // normally a IRBlock
};

struct IRBreak : IRStmt {};
struct IRContinue : IRStmt {};

// A "name:" jump label (printed outdented one level, C++ style).
struct IRLabel : IRStmt {
  std::string name;
  explicit IRLabel(std::string n) : name(std::move(n)) {}
};

// A "goto name;" statement.
struct IRGoto : IRStmt {
  std::string label;
  explicit IRGoto(std::string l) : label(std::move(l)) {}
};

// A "// ..." comment line (multi-line text becomes multiple comment lines).
struct IRComment : IRStmt {
  std::string text;
  explicit IRComment(std::string t) : text(std::move(t)) {}
};

struct IRCase {
  std::vector<std::string> labels; // empty means `default:`
  std::unique_ptr<IRBlock> body;
};

struct IRSwitch : IRStmt {
  IRExpr cond;
  std::vector<IRCase> cases;
};

struct IRStructField {
  std::string type;
  std::string name;
  IRStructField(std::string t, std::string n) : type(std::move(t)), name(std::move(n)) {}
};

struct IRCtorParam {
  std::string type;
  std::string name;
  std::string defaultValue; // empty means no default
  IRCtorParam(std::string t, std::string n, std::string d = "")
      : type(std::move(t)), name(std::move(n)), defaultValue(std::move(d)) {}
};

struct IRStructCtor {
  std::vector<IRCtorParam> params;
  std::vector<std::pair<std::string, std::string>> init; // (field, arg)
  IRStructCtor(std::vector<IRCtorParam> p,
               std::vector<std::pair<std::string, std::string>> i)
      : params(std::move(p)), init(std::move(i)) {}
};

// Payload shared by top-level and function-local struct definitions.
struct IRStructData {
  std::string name;
  std::vector<IRStructField> fields;
  std::vector<IRStructCtor> ctors;
  IRStructData() = default;
  IRStructData(std::string n, std::vector<IRStructField> f,
               std::vector<IRStructCtor> c)
      : name(std::move(n)), fields(std::move(f)), ctors(std::move(c)) {}
};

// A struct definition inside a function body (e.g. a local Frame struct).
struct IRLocalStruct : IRStmt {
  IRStructData data;
  explicit IRLocalStruct(IRStructData d) : data(std::move(d)) {}
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

struct IRStructDef : IRTopLevel {
  IRStructData data;
  IRStructDef(std::string n, std::vector<IRStructField> f,
              std::vector<IRStructCtor> c)
      : data(std::move(n), std::move(f), std::move(c)) {}
  explicit IRStructDef(IRStructData d) : data(std::move(d)) {}
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

  // Top-level "// ..." comment (multi-line text becomes multiple lines).
  IRBuilder &comment(const std::string &text);

  IRBuilder &function(const std::string &signature,
                      std::unique_ptr<IRBlock> body);
  IRBuilder &structDef(IRStructData data);
  IRBuilder &enumDef(const std::string &name,
                     std::vector<std::string> enumerators);

  // Convenience: create and return a new IRBlock.
  static std::unique_ptr<IRBlock> block();

  // Convenience: add a statement to a block.
  static IRBlock &add(IRBlock *blk, std::unique_ptr<IRStmt> s);

  // Convenience: append an expression statement (the IR printer appends the
  // semicolon), a raw statement (text must carry its own semicolons), or a
  // `type name = init;` variable declaration to a block.
  static IRBlock &addExpr(IRBlock *blk, const std::string &e);
  static IRBlock &addRaw(IRBlock *blk, const std::string &text);
  static IRBlock &addVar(IRBlock *blk, const std::string &type,
                         const std::string &name, const std::string &init);

  static std::unique_ptr<IRIf>
  if_(IRExpr cond, std::unique_ptr<IRStmt> thenBranch,
      std::unique_ptr<IRStmt> elseBranch = nullptr);

  // Fold a list of (cond, then-block) pairs plus an optional final else
  // statement into an if / else-if / else chain.
  static std::unique_ptr<IRStmt>
  ifChain(std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>>
              branches,
          std::unique_ptr<IRStmt> elseBranch = nullptr);

  static std::unique_ptr<IRWhile>
  while_(IRExpr cond, std::unique_ptr<IRBlock> body);

  static std::unique_ptr<IRFor>
  for_(std::string init, IRExpr cond, std::string incr,
       std::unique_ptr<IRStmt> body);

  static std::unique_ptr<IRSwitch> switch_(IRExpr cond);
  // Append a case (labels empty = default) with the given body to a switch.
  static IRSwitch &case_(IRSwitch *sw, std::vector<std::string> labels,
                         std::unique_ptr<IRBlock> body);

  static std::unique_ptr<IRBreak> break_();
  static std::unique_ptr<IRContinue> continue_();
  static std::unique_ptr<IRLabel> label(std::string name);
  static std::unique_ptr<IRGoto> goto_(std::string label);

  static std::unique_ptr<IRComment> commentStmt(std::string text);
  static std::unique_ptr<IRLocalStruct> localStruct(IRStructData data);

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
