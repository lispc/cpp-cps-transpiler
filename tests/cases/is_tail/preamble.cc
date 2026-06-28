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
