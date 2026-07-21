// Adapted from printStmt / printBlock / printBracedBlock / printIfChain in
// src/output_ir.cc.
// Mutually recursive void visitor over a small statement IR.  The member
// signatures differ only in the node pointer type, and recursive calls are
// statements interleaved with output, so the value-combining mutual backends
// cannot handle this group; it exercises the coroutine-trampoline backend.

struct StrBuf {
  char data[4096];
  int len;
  StrBuf() : len(0) { data[0] = 0; }
};

void buf_put(StrBuf &b, const char *s) {
  for (int i = 0; s[i]; ++i)
    b.data[b.len++] = s[i];
  b.data[b.len] = 0;
}

void buf_indent(StrBuf &b, int level) {
  for (int i = 0; i < level; ++i)
    buf_put(b, "  ");
}

// kind: 1 = Block, 2 = If, 3 = Expr
struct IRStmt {
  int kind;
};

struct IRBlockNode : IRStmt {
  const IRStmt *items[8];
  int count;
};

struct IRIfNode : IRStmt {
  const char *cond;
  const IRStmt *thenS;
  const IRStmt *elseS;
};

struct IRExprNode : IRStmt {
  const char *text;
};

void printStmt(StrBuf &os, const IRStmt *s, int level);
void printBlock(StrBuf &os, const IRBlockNode *blk, int level);

void printBracedBlock(StrBuf &os, const IRBlockNode *blk, int level) {
  buf_put(os, "{\n");
  printBlock(os, blk, level + 1);
  buf_indent(os, level);
  buf_put(os, "}");
}

void printBlock(StrBuf &os, const IRBlockNode *blk, int level) {
  for (int i = 0; i < blk->count; ++i) {
    const IRStmt *s = blk->items[i];
    if (s->kind == 1) {
      buf_indent(os, level);
      printBracedBlock(os, (const IRBlockNode *)s, level);
      buf_put(os, "\n");
    } else {
      printStmt(os, s, level);
    }
  }
}

void printIfChain(StrBuf &os, const IRIfNode *is, int level) {
  buf_put(os, "if (");
  buf_put(os, is->cond);
  buf_put(os, ") ");
  if (is->thenS->kind == 1) {
    printBracedBlock(os, (const IRBlockNode *)is->thenS, level);
  } else {
    buf_put(os, "{\n");
    buf_indent(os, level + 1);
    printIfChain(os, (const IRIfNode *)is->thenS, level + 1);
    buf_put(os, "\n");
    buf_indent(os, level);
    buf_put(os, "}");
  }
  if (is->elseS) {
    if (is->elseS->kind == 2) {
      buf_put(os, " else ");
      printIfChain(os, (const IRIfNode *)is->elseS, level);
    } else {
      buf_put(os, " else ");
      printBracedBlock(os, (const IRBlockNode *)is->elseS, level);
    }
  }
}

void printStmt(StrBuf &os, const IRStmt *s, int level) {
  if (!s)
    return;
  if (const IRBlockNode *blk =
          s->kind == 1 ? (const IRBlockNode *)s : nullptr) {
    buf_indent(os, level);
    printBracedBlock(os, blk, level);
    buf_put(os, "\n");
    return;
  }
  if (const IRIfNode *is = s->kind == 2 ? (const IRIfNode *)s : nullptr) {
    buf_indent(os, level);
    printIfChain(os, is, level);
    buf_put(os, "\n");
    return;
  }
  buf_indent(os, level);
  buf_put(os, ((const IRExprNode *)s)->text);
  buf_put(os, ";\n");
}
