enum {
  ST_RETURN,
  ST_EXPR,
  ST_IF
};

struct Expr {
  int id;
};

struct Stmt {
  int kind;
  Expr *ret;
  Expr *expr;
  Stmt *then_;
  Stmt *else_;
};

bool is_tail(Expr *e, Stmt *s) {
  if (!s)
    return false;
  switch (s->kind) {
  case ST_RETURN:
    return s->ret == e;
  case ST_EXPR:
    return s->expr == e;
  default:
    return is_tail(e, s->then_) || is_tail(e, s->else_);
  }
}
