#include <iostream>
int main() {
  Expr target;
  Expr other;
  ReturnStmt rs_target(&target);
  ReturnStmt rs_other(&other);
  std::cout << "tail_return_target = " << IsInTailPosition(&target, &rs_target, "f") << std::endl;
  std::cout << "tail_return_other = " << IsInTailPosition(&target, &rs_other, "f") << std::endl;
  IfStmt if_both(nullptr, &rs_target, &rs_target);
  std::cout << "tail_if_both = " << IsInTailPosition(&target, &if_both, "f") << std::endl;
  IfStmt if_mixed(nullptr, &rs_other, &rs_target);
  std::cout << "tail_if_mixed = " << IsInTailPosition(&target, &if_mixed, "f") << std::endl;
  IfStmt if_none(nullptr, &rs_other, &rs_other);
  std::cout << "tail_if_none = " << IsInTailPosition(&target, &if_none, "f") << std::endl;
  CompoundStmt cs;
  cs.addChild(&rs_other);
  cs.addChild(&rs_target);
  std::cout << "tail_compound = " << IsInTailPosition(&target, &cs, "f") << std::endl;
  CompoundStmt cs2;
  cs2.addChild(&rs_target);
  cs2.addChild(&rs_other);
  std::cout << "tail_compound2 = " << IsInTailPosition(&target, &cs2, "f") << std::endl;
  std::cout << "tail_null = " << IsInTailPosition(nullptr, &rs_target, "f") << std::endl;
  std::cout << "tail_expr = " << IsInTailPosition(&target, &target, "f") << std::endl;
  return 0;
}
