#include <iostream>
int main() {
  Expr e1{1}, e2{2}, e3{3};
  Stmt ret_e1{ST_RETURN, &e1, nullptr, nullptr, nullptr};
  Stmt expr_e2{ST_EXPR, nullptr, &e2, nullptr, nullptr};
  Stmt if1{ST_IF, nullptr, nullptr, &ret_e1, &expr_e2}; // tail for e1 and e2
  Stmt if2{ST_IF, nullptr, nullptr, &expr_e2, &expr_e2}; // not tail for e1
  Stmt deep{ST_IF, nullptr, nullptr, &if1, &ret_e1}; // tail for e1
  std::cout << "is_tail(&e1, &ret_e1) = " << is_tail(&e1, &ret_e1) << std::endl;
  std::cout << "is_tail(&e2, &ret_e1) = " << is_tail(&e2, &ret_e1) << std::endl;
  std::cout << "is_tail(&e1, &if1) = " << is_tail(&e1, &if1) << std::endl;
  std::cout << "is_tail(&e3, &if1) = " << is_tail(&e3, &if1) << std::endl;
  std::cout << "is_tail(&e1, &if2) = " << is_tail(&e1, &if2) << std::endl;
  std::cout << "is_tail(&e1, &deep) = " << is_tail(&e1, &deep) << std::endl;
  return 0;
}
