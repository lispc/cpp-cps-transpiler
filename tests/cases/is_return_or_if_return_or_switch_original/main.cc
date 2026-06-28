#include <iostream>
int main() {
  ReturnStmt rs;
  IfStmt is;
  SwitchStmt ss;
  CompoundStmt cs_return;
  cs_return.addChild(&rs);
  CompoundStmt cs_if;
  cs_if.addChild(&is);
  CompoundStmt cs_two;
  cs_two.addChild(&rs);
  cs_two.addChild(&is);
  CompoundStmt cs_nested;
  cs_nested.addChild(&cs_return);
  Stmt other;
  std::cout << "return = " << IsReturnOrIfReturnOrSwitch(&rs) << std::endl;
  std::cout << "if = " << IsReturnOrIfReturnOrSwitch(&is) << std::endl;
  std::cout << "switch = " << IsReturnOrIfReturnOrSwitch(&ss) << std::endl;
  std::cout << "compound_return = " << IsReturnOrIfReturnOrSwitch(&cs_return) << std::endl;
  std::cout << "compound_if = " << IsReturnOrIfReturnOrSwitch(&cs_if) << std::endl;
  std::cout << "compound_two = " << IsReturnOrIfReturnOrSwitch(&cs_two) << std::endl;
  std::cout << "compound_nested = " << IsReturnOrIfReturnOrSwitch(&cs_nested) << std::endl;
  std::cout << "other = " << IsReturnOrIfReturnOrSwitch(&other) << std::endl;
  return 0;
}
