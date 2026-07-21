#include <cstdio>

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
