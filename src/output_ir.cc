#include "output_ir.h"
#include <sstream>

namespace cps {

namespace {

void printStmt(std::ostringstream &os, const IRStmt *s, int level);

void indent(std::ostringstream &os, int level) {
  os << std::string(level * 2, ' ');
}

bool needsSemicolon(const IRStmt *s) {
  if (dynamic_cast<const IRIf *>(s))
    return false;
  if (dynamic_cast<const IRWhile *>(s))
    return false;
  if (dynamic_cast<const IRBlock *>(s))
    return false;
  if (dynamic_cast<const IRRaw *>(s))
    return false;
  return true;
}

// Print the content of a non-compound statement without any leading or trailing
// newline. Used when an if/else has a single-statement branch.
void printInlineStmt(std::ostringstream &os, const IRStmt *s) {
  if (const IRReturn *rs = dynamic_cast<const IRReturn *>(s)) {
    if (rs->value)
      os << "return " << rs->value->text;
    else
      os << "return";
  } else if (const IRVarDecl *vds = dynamic_cast<const IRVarDecl *>(s)) {
    os << vds->type << " " << vds->name;
    if (vds->init)
      os << " = " << vds->init->text;
  } else if (const IRExprStmt *es = dynamic_cast<const IRExprStmt *>(s)) {
    os << es->expr.text;
  } else if (const IRRaw *rs = dynamic_cast<const IRRaw *>(s)) {
    os << rs->text;
  }
  if (needsSemicolon(s))
    os << ";";
}

void printBlock(std::ostringstream &os, const IRBlock *blk, int level);

void printBracedBlock(std::ostringstream &os, const IRBlock *blk, int level) {
  os << "{\n";
  printBlock(os, blk, level + 1);
  indent(os, level);
  os << "}";
}

void printBlock(std::ostringstream &os, const IRBlock *blk, int level) {
  for (const auto &s : blk->body) {
    if (const IRBlock *inner = dynamic_cast<const IRBlock *>(s.get())) {
      indent(os, level);
      printBracedBlock(os, inner, level);
      os << "\n";
    } else {
      printStmt(os, s.get(), level);
    }
  }
}

void printStmt(std::ostringstream &os, const IRStmt *s, int level) {
  if (!s)
    return;

  if (const IRBlock *blk = dynamic_cast<const IRBlock *>(s)) {
    indent(os, level);
    printBracedBlock(os, blk, level);
    os << "\n";
    return;
  }

  if (const IRIf *is = dynamic_cast<const IRIf *>(s)) {
    indent(os, level);
    os << "if (" << is->cond.text << ") ";
    if (const IRBlock *thenBlk =
            dynamic_cast<const IRBlock *>(is->thenBranch.get())) {
      printBracedBlock(os, thenBlk, level);
    } else {
      printInlineStmt(os, is->thenBranch.get());
    }
    if (is->elseBranch) {
      os << " else ";
      if (const IRBlock *elseBlk =
              dynamic_cast<const IRBlock *>(is->elseBranch.get())) {
        printBracedBlock(os, elseBlk, level);
      } else {
        printInlineStmt(os, is->elseBranch.get());
      }
    }
    os << "\n";
    return;
  }

  if (const IRWhile *ws = dynamic_cast<const IRWhile *>(s)) {
    indent(os, level);
    os << "while (" << ws->cond.text << ") ";
    if (const IRBlock *bodyBlk =
            dynamic_cast<const IRBlock *>(ws->body.get())) {
      printBracedBlock(os, bodyBlk, level);
      os << "\n";
    } else {
      printInlineStmt(os, ws->body.get());
      os << "\n";
    }
    return;
  }

  indent(os, level);
  printInlineStmt(os, s);
  os << "\n";
}

void printTopLevel(std::ostringstream &os, const IRTopLevel *tl) {
  if (const IRRawTop *r = dynamic_cast<const IRRawTop *>(tl)) {
    os << r->text;
    if (!r->text.empty() && r->text.back() != '\n')
      os << "\n";
    return;
  }

  if (const IRInclude *inc = dynamic_cast<const IRInclude *>(tl)) {
    os << "#include <" << inc->header << ">\n";
    return;
  }

  if (const IREnumDef *en = dynamic_cast<const IREnumDef *>(tl)) {
    os << "enum class " << en->name << " {\n";
    for (size_t i = 0; i < en->enumerators.size(); ++i) {
      os << "  " << en->enumerators[i];
      if (i + 1 < en->enumerators.size())
        os << ",";
      os << "\n";
    }
    os << "};\n";
    return;
  }

  if (const IRStructDef *sd = dynamic_cast<const IRStructDef *>(tl)) {
    os << "struct " << sd->name << " {\n";
    for (const auto &f : sd->fields)
      os << "  " << f.type << " " << f.name << ";\n";
    for (const auto &ctor : sd->ctors) {
      os << "  " << sd->name << "(";
      for (size_t i = 0; i < ctor.params.size(); ++i) {
        if (i > 0)
          os << ", ";
        os << ctor.params[i].first << " " << ctor.params[i].second;
      }
      os << ")";
      if (!ctor.init.empty()) {
        os << " : ";
        for (size_t i = 0; i < ctor.init.size(); ++i) {
          if (i > 0)
            os << ", ";
          os << ctor.init[i].first << "(" << ctor.init[i].second << ")";
        }
      }
      os << " {}\n";
    }
    os << "};\n";
    return;
  }

  if (const IRFunctionDef *fd = dynamic_cast<const IRFunctionDef *>(tl)) {
    os << fd->signature << " {\n";
    printBlock(os, fd->body.get(), 1);
    os << "}\n";
    return;
  }
}

} // anonymous namespace

std::string PrintGeneratedUnit(const IRUnit &unit) {
  std::ostringstream os;
  for (const auto &tl : unit.items) {
    printTopLevel(os, tl.get());
    os << "\n";
  }
  std::string result = os.str();
  // Trim excessive trailing newlines, but keep a single final newline.
  while (result.size() > 1 && result.back() == '\n' &&
         result[result.size() - 2] == '\n') {
    result.pop_back();
  }
  return result;
}

// ============================================================
// IRBuilder
// ============================================================

IRBuilder &IRBuilder::raw(const std::string &text) {
  unit.items.push_back(std::make_unique<IRRawTop>(text));
  return *this;
}

IRBuilder &IRBuilder::include(const std::string &header) {
  unit.items.push_back(std::make_unique<IRInclude>(header));
  return *this;
}

IRBuilder &IRBuilder::blank() {
  unit.items.push_back(std::make_unique<IRRawTop>(""));
  return *this;
}

IRBuilder &IRBuilder::function(const std::string &signature,
                               std::unique_ptr<IRBlock> body) {
  unit.items.push_back(
      std::make_unique<IRFunctionDef>(signature, std::move(body)));
  return *this;
}

std::unique_ptr<IRBlock> IRBuilder::block() {
  return std::make_unique<IRBlock>();
}

IRBlock &IRBuilder::add(IRBlock *blk, std::unique_ptr<IRStmt> s) {
  blk->body.push_back(std::move(s));
  return *blk;
}

std::unique_ptr<IRIf>
IRBuilder::if_(IRExpr cond, std::unique_ptr<IRStmt> thenBranch,
               std::unique_ptr<IRStmt> elseBranch) {
  auto s = std::make_unique<IRIf>();
  s->cond = std::move(cond);
  s->thenBranch = std::move(thenBranch);
  s->elseBranch = std::move(elseBranch);
  return s;
}

std::unique_ptr<IRWhile>
IRBuilder::while_(IRExpr cond, std::unique_ptr<IRBlock> body) {
  auto s = std::make_unique<IRWhile>();
  s->cond = std::move(cond);
  s->body = std::move(body);
  return s;
}

std::unique_ptr<IRReturn> IRBuilder::ret() {
  return std::make_unique<IRReturn>();
}

std::unique_ptr<IRReturn> IRBuilder::ret(IRExpr value) {
  return std::make_unique<IRReturn>(std::move(value));
}

std::unique_ptr<IRExprStmt> IRBuilder::expr(IRExpr e) {
  return std::make_unique<IRExprStmt>(std::move(e));
}

std::unique_ptr<IRVarDecl> IRBuilder::var(const std::string &type,
                                            const std::string &name,
                                            std::optional<IRExpr> init) {
  return std::make_unique<IRVarDecl>(type, name, std::move(init));
}

std::unique_ptr<IRRaw> IRBuilder::rawStmt(const std::string &text) {
  return std::make_unique<IRRaw>(text);
}

} // namespace cps
