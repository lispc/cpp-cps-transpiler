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
  if (dynamic_cast<const IRFor *>(s))
    return false;
  if (dynamic_cast<const IRSwitch *>(s))
    return false;
  if (dynamic_cast<const IRBlock *>(s))
    return false;
  if (dynamic_cast<const IRRaw *>(s))
    return false;
  if (dynamic_cast<const IRComment *>(s))
    return false;
  if (dynamic_cast<const IRLocalStruct *>(s))
    return false;
  if (dynamic_cast<const IRLabel *>(s))
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
  } else if (dynamic_cast<const IRBreak *>(s)) {
    os << "break";
  } else if (dynamic_cast<const IRContinue *>(s)) {
    os << "continue";
  } else if (const IRGoto *gs = dynamic_cast<const IRGoto *>(s)) {
    os << "goto " << gs->label;
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

// Print multi-line raw text with every non-empty line indented to `level`.
void printRawLines(std::ostringstream &os, const std::string &text, int level) {
  std::istringstream iss(text);
  std::string line;
  bool first = true;
  while (std::getline(iss, line)) {
    if (!first)
      os << "\n";
    if (!line.empty()) {
      indent(os, level);
      os << line;
    }
    first = false;
  }
}

void printStructData(std::ostringstream &os, const IRStructData &data,
                     int level) {
  indent(os, level);
  os << "struct " << data.name << " {\n";
  for (const auto &f : data.fields) {
    indent(os, level + 1);
    os << f.type << " " << f.name << ";\n";
  }
  for (const auto &ctor : data.ctors) {
    indent(os, level + 1);
    os << data.name << "(";
    for (size_t i = 0; i < ctor.params.size(); ++i) {
      if (i > 0)
        os << ", ";
      os << ctor.params[i].type << " " << ctor.params[i].name;
      if (!ctor.params[i].defaultValue.empty())
        os << " = " << ctor.params[i].defaultValue;
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
  indent(os, level);
  os << "};";
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

// Print an if/else-if/else chain starting at `is`. The chain itself is
// printed without a leading indent; the caller handles indentation and the
// trailing newline.
void printIfChain(std::ostringstream &os, const IRIf *is, int level) {
  os << "if (" << is->cond.text << ") ";
  if (const IRBlock *thenBlk =
          dynamic_cast<const IRBlock *>(is->thenBranch.get())) {
    printBracedBlock(os, thenBlk, level);
  } else if (dynamic_cast<const IRIf *>(is->thenBranch.get())) {
    // A bare if inside a then-branch needs braces to stay unambiguous.
    os << "{\n";
    indent(os, level + 1);
    printIfChain(os, static_cast<const IRIf *>(is->thenBranch.get()),
                 level + 1);
    os << "\n";
    indent(os, level);
    os << "}";
  } else {
    printInlineStmt(os, is->thenBranch.get());
  }
  if (is->elseBranch) {
    if (const IRIf *elseIf =
            dynamic_cast<const IRIf *>(is->elseBranch.get())) {
      os << " else ";
      printIfChain(os, elseIf, level);
    } else if (const IRBlock *elseBlk =
                   dynamic_cast<const IRBlock *>(is->elseBranch.get())) {
      os << " else ";
      printBracedBlock(os, elseBlk, level);
    } else {
      os << " else ";
      printInlineStmt(os, is->elseBranch.get());
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
    printIfChain(os, is, level);
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

  if (const IRFor *fs = dynamic_cast<const IRFor *>(s)) {
    indent(os, level);
    os << "for (" << fs->init;
    if (!fs->cond.text.empty() || !fs->incr.empty())
      os << "; " << fs->cond.text << "; " << fs->incr;
    os << ") ";
    if (const IRBlock *bodyBlk =
            dynamic_cast<const IRBlock *>(fs->body.get())) {
      printBracedBlock(os, bodyBlk, level);
      os << "\n";
    } else {
      printInlineStmt(os, fs->body.get());
      os << "\n";
    }
    return;
  }

  if (const IRSwitch *ss = dynamic_cast<const IRSwitch *>(s)) {
    indent(os, level);
    os << "switch (" << ss->cond.text << ") {\n";
    for (const auto &c : ss->cases) {
      if (c.labels.empty()) {
        indent(os, level + 1);
        os << "default:\n";
      } else {
        for (const auto &label : c.labels) {
          indent(os, level + 1);
          os << "case " << label << ":\n";
        }
      }
      indent(os, level + 1);
      printBracedBlock(os, c.body.get(), level + 1);
      os << "\n";
    }
    indent(os, level);
    os << "}\n";
    return;
  }

  if (const IRComment *cs = dynamic_cast<const IRComment *>(s)) {
    std::istringstream iss(cs->text);
    std::string line;
    while (std::getline(iss, line)) {
      indent(os, level);
      os << "//";
      if (!line.empty())
        os << " " << line;
      os << "\n";
    }
    return;
  }

  if (const IRLocalStruct *ls = dynamic_cast<const IRLocalStruct *>(s)) {
    printStructData(os, ls->data, level);
    os << "\n";
    return;
  }

  if (const IRLabel *ls = dynamic_cast<const IRLabel *>(s)) {
    // C++ labels are conventionally outdented one level.
    indent(os, level > 0 ? level - 1 : 0);
    os << ls->name << ":\n";
    return;
  }

  if (const IRRaw *rs = dynamic_cast<const IRRaw *>(s)) {
    printRawLines(os, rs->text, level);
    os << "\n";
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
    printStructData(os, sd->data, 0);
    os << "\n";
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

  // Hoist #include directives to the top of the unit and deduplicate them.
  {
    std::vector<std::string> includes;
    for (const auto &tl : unit.items) {
      if (const IRInclude *inc = dynamic_cast<const IRInclude *>(tl.get())) {
        bool seen = false;
        for (const auto &h : includes)
          seen = seen || (h == inc->header);
        if (!seen)
          includes.push_back(inc->header);
      }
    }
    for (const auto &h : includes)
      os << "#include <" << h << ">\n";
    if (!includes.empty())
      os << "\n";
  }

  for (const auto &tl : unit.items) {
    if (dynamic_cast<const IRInclude *>(tl.get()))
      continue;
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

IRBuilder &IRBuilder::comment(const std::string &text) {
  std::string out;
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    out += "//";
    if (!line.empty())
      out += " " + line;
    out += "\n";
  }
  unit.items.push_back(std::make_unique<IRRawTop>(out));
  return *this;
}

IRBuilder &IRBuilder::function(const std::string &signature,
                               std::unique_ptr<IRBlock> body) {
  unit.items.push_back(
      std::make_unique<IRFunctionDef>(signature, std::move(body)));
  return *this;
}

IRBuilder &IRBuilder::structDef(IRStructData data) {
  unit.items.push_back(std::make_unique<IRStructDef>(std::move(data)));
  return *this;
}

IRBuilder &IRBuilder::enumDef(const std::string &name,
                              std::vector<std::string> enumerators) {
  unit.items.push_back(
      std::make_unique<IREnumDef>(name, std::move(enumerators)));
  return *this;
}

std::unique_ptr<IRBlock> IRBuilder::block() {
  return std::make_unique<IRBlock>();
}

IRBlock &IRBuilder::add(IRBlock *blk, std::unique_ptr<IRStmt> s) {
  blk->body.push_back(std::move(s));
  return *blk;
}

IRBlock &IRBuilder::addExpr(IRBlock *blk, const std::string &e) {
  return add(blk, expr(IRExpr(e)));
}

IRBlock &IRBuilder::addRaw(IRBlock *blk, const std::string &text) {
  return add(blk, rawStmt(text));
}

IRBlock &IRBuilder::addVar(IRBlock *blk, const std::string &type,
                           const std::string &name, const std::string &init) {
  return add(blk, var(type, name, IRExpr(init)));
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

std::unique_ptr<IRStmt>
IRBuilder::ifChain(std::vector<std::pair<std::string, std::unique_ptr<IRBlock>>>
                       branches,
                   std::unique_ptr<IRStmt> elseBranch) {
  std::unique_ptr<IRStmt> chain = std::move(elseBranch);
  for (size_t i = branches.size(); i-- > 0;) {
    chain = if_(IRExpr(std::move(branches[i].first)),
                std::move(branches[i].second), std::move(chain));
  }
  return chain;
}

std::unique_ptr<IRFor>
IRBuilder::for_(std::string init, IRExpr cond, std::string incr,
                std::unique_ptr<IRStmt> body) {
  auto s = std::make_unique<IRFor>();
  s->init = std::move(init);
  s->cond = std::move(cond);
  s->incr = std::move(incr);
  s->body = std::move(body);
  return s;
}

std::unique_ptr<IRSwitch> IRBuilder::switch_(IRExpr cond) {
  auto s = std::make_unique<IRSwitch>();
  s->cond = std::move(cond);
  return s;
}

IRSwitch &IRBuilder::case_(IRSwitch *sw, std::vector<std::string> labels,
                           std::unique_ptr<IRBlock> body) {
  IRCase c;
  c.labels = std::move(labels);
  c.body = std::move(body);
  sw->cases.push_back(std::move(c));
  return *sw;
}

std::unique_ptr<IRBreak> IRBuilder::break_() {
  return std::make_unique<IRBreak>();
}

std::unique_ptr<IRContinue> IRBuilder::continue_() {
  return std::make_unique<IRContinue>();
}

std::unique_ptr<IRLabel> IRBuilder::label(std::string name) {
  return std::make_unique<IRLabel>(std::move(name));
}

std::unique_ptr<IRGoto> IRBuilder::goto_(std::string label) {
  return std::make_unique<IRGoto>(std::move(label));
}

std::unique_ptr<IRComment> IRBuilder::commentStmt(std::string text) {
  return std::make_unique<IRComment>(std::move(text));
}

std::unique_ptr<IRLocalStruct> IRBuilder::localStruct(IRStructData data) {
  return std::make_unique<IRLocalStruct>(std::move(data));
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
