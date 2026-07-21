#include <cstdio>

struct StrBuf {
  char data[1024];
  int len;
  StrBuf() : len(0) { data[0] = 0; }
};

void buf_put(StrBuf &b, const char *s) {
  for (int i = 0; s[i]; ++i)
    b.data[b.len++] = s[i];
  b.data[b.len] = 0;
}

struct RNode;

struct LNode {
  const char *tag;
  int skip;
  const RNode *next;
};

struct RNode {
  const char *tag;
  const LNode *down;
};
