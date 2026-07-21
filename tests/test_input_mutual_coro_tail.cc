// Mutually recursive void walker with differing member signatures and
// "return g(args);" tail-call style.  The value-combining mutual backends
// require identical signatures and return-expression recursion, so this
// group exercises the coroutine-trampoline backend's return-call rewriting,
// including a return-call inside a braceless if.

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

void walkRight(const RNode *r, StrBuf &os);

void walkLeaf(const LNode *l, StrBuf &os) {
  if (!l)
    return;
  if (l->skip)
    return walkRight(l->next, os);
  buf_put(os, l->tag);
  return walkRight(l->next, os);
}

void walkRight(const RNode *r, StrBuf &os) {
  if (!r) {
    buf_put(os, ".");
    return;
  }
  buf_put(os, r->tag);
  return walkLeaf(r->down, os);
}
