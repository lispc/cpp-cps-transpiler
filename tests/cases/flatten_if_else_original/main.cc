int main() {
  Expr cond5{5}, value5{105};
  Expr cond10{10}, value10{110};
  Expr cond15{15}, value15{115};
  Expr rec{99};

  ReturnStmt retRec(&rec);
  IfStmt inner(&cond15, new ReturnStmt(&value15), &retRec);
  IfStmt middle(&cond10, new ReturnStmt(&value10), &inner);
  IfStmt top(&cond5, new ReturnStmt(&value5), &middle);

  ASTContext ctx;
  BodyAnalysis ba;
  FlattenIfElse(&top, ba, &ctx);

  int base_n_5 = 0, base_n_10 = 0, base_n_15 = 0;
  for (const auto &bc : ba.BaseCases) {
    if (bc.CondExpr && bc.CondExpr->Tag == 5) base_n_5 = 1;
    if (bc.CondExpr && bc.CondExpr->Tag == 10) base_n_10 = 1;
    if (bc.CondExpr && bc.CondExpr->Tag == 15) base_n_15 = 1;
  }
  int recursive_call = (ba.IsRecursive && ba.RecExpr && ba.RecExpr->Tag == 99) ? 1 : 0;

  printf("base_n_5 = %d\n", base_n_5);
  printf("base_n_10 = %d\n", base_n_10);
  printf("base_n_15 = %d\n", base_n_15);
  printf("recursive_call = %d\n", recursive_call);
  return 0;
}
