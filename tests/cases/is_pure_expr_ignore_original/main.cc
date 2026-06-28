#include <iostream>
int main() {
  FunctionDecl pureFn("pure", true);
  FunctionDecl impureFn("impure", false);
  FunctionDecl targetFn("f", false);
  Expr leaf;
  CallExpr targetCall(&targetFn);
  CallExpr impureCall(&impureFn);
  impureCall.addArg(&leaf);
  CallExpr pureCall(&pureFn);
  pureCall.addArg(&leaf);
  CallExpr pureCallWithImpureArg(&pureFn);
  pureCallWithImpureArg.addArg(&impureCall);
  BinaryOperator assign(BO_Assign, &leaf, &leaf);
  BinaryOperator comma(BO_Comma, &leaf, &leaf);
  BinaryOperator add(BO_Add, &leaf, &leaf);
  UnaryOperator inc(UO_PreInc, &leaf);
  std::cout << "leaf = " << IsPureExprIgnoringRecursiveCallsImpl(&leaf, "f") << std::endl;
  std::cout << "targetCall = " << IsPureExprIgnoringRecursiveCallsImpl(&targetCall, "f") << std::endl;
  std::cout << "impureCall = " << IsPureExprIgnoringRecursiveCallsImpl(&impureCall, "f") << std::endl;
  std::cout << "pureCall = " << IsPureExprIgnoringRecursiveCallsImpl(&pureCall, "f") << std::endl;
  std::cout << "pureCallWithImpureArg = " << IsPureExprIgnoringRecursiveCallsImpl(&pureCallWithImpureArg, "f") << std::endl;
  std::cout << "assign = " << IsPureExprIgnoringRecursiveCallsImpl(&assign, "f") << std::endl;
  std::cout << "comma = " << IsPureExprIgnoringRecursiveCallsImpl(&comma, "f") << std::endl;
  std::cout << "add = " << IsPureExprIgnoringRecursiveCallsImpl(&add, "f") << std::endl;
  std::cout << "inc = " << IsPureExprIgnoringRecursiveCallsImpl(&inc, "f") << std::endl;
  return 0;
}
