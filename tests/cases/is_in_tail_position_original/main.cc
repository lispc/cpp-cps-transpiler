#include <iostream>
int main() {
  FunctionDecl target("target");
  FunctionDecl other("other");
  Expr cond;
  BinaryOperator land(BO_LAnd, &cond, &cond);

  CallExpr targetCall(&target);
  ReturnStmt rs1(&targetCall);
  std::cout << "tail_target = " << IsInTailPosition(&rs1, &target) << std::endl;

  CallExpr otherCall(&other);
  ReturnStmt rs2(&otherCall);
  std::cout << "tail_other = " << IsInTailPosition(&rs2, &target) << std::endl;

  CallExpr tc2(&target);
  ReturnStmt rs_then(&tc2), rs_else(&tc2);
  IfStmt if1(&land, &rs_then, &rs_else);
  std::cout << "tail_if_both = " << IsInTailPosition(&if1, &target) << std::endl;

  ReturnStmt rs_then2(&otherCall);
  IfStmt if2(&land, &rs_then2, &rs_else);
  std::cout << "tail_if_mixed = " << IsInTailPosition(&if2, &target) << std::endl;

  CompoundStmt cs;
  cs.addChild(&rs2);
  cs.addChild(&rs1);
  std::cout << "tail_compound = " << IsInTailPosition(&cs, &target) << std::endl;

  std::cout << "tail_null = " << IsInTailPosition(nullptr, &target) << std::endl;

  ReturnStmt rs_empty(nullptr);
  std::cout << "tail_empty_return = " << IsInTailPosition(&rs_empty, &target) << std::endl;

  return 0;
}
