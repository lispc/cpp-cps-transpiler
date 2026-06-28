#include <iostream>
int main() {
  FunctionDecl pureFn("pure", true);
  FunctionDecl impureFn("impure", false);
  Expr leaf;
  CallExpr pureCall(&pureFn);
  pureCall.addArg(&leaf);
  CallExpr impureCall(&impureFn);
  impureCall.addArg(&leaf);
  BinaryOperator assign(BO_Assign, &leaf, &leaf);
  BinaryOperator add(BO_Add, &leaf, &leaf);
  BinaryOperator addAssign(BO_Add, &leaf, &assign);
  UnaryOperator inc(UO_PreInc, &leaf);
  BinaryOperator comma(BO_Comma, &leaf, &leaf);
  std::cout << "leaf = " << IsPureExprImpl(&leaf) << std::endl;
  std::cout << "pureCall = " << IsPureExprImpl(&pureCall) << std::endl;
  std::cout << "impureCall = " << IsPureExprImpl(&impureCall) << std::endl;
  std::cout << "assign = " << IsPureExprImpl(&assign) << std::endl;
  std::cout << "add = " << IsPureExprImpl(&add) << std::endl;
  std::cout << "addAssign = " << IsPureExprImpl(&addAssign) << std::endl;
  std::cout << "inc = " << IsPureExprImpl(&inc) << std::endl;
  std::cout << "comma = " << IsPureExprImpl(&comma) << std::endl;
  return 0;
}
