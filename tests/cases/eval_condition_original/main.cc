#include <iostream>
int val(const EvalResult &r) {
  return r == EvalResult::True ? 1 : (r == EvalResult::False ? 0 : -1);
}
int main() {
  std::string param = "n";
  ValueDecl nDecl("n");
  IntegerLiteral five(5), three(3);
  DeclRefExpr nRef(&nDecl);

  BinaryOperator eq(BO_EQ, &nRef, &five);
  BinaryOperator ne(BO_NE, &nRef, &five);
  BinaryOperator lt(BO_LT, &nRef, &five);
  BinaryOperator gt(BO_GT, &nRef, &three);
  BinaryOperator le(BO_LE, &nRef, &five);
  BinaryOperator ge(BO_GE, &nRef, &five);
  BinaryOperator land(BO_LAnd, &eq, &gt);
  BinaryOperator lor(BO_LOr, &ne, &lt);
  UnaryOperator lnot(UO_LNot, &eq);
  BinaryOperator add(BO_Add, &nRef, &five);

  std::cout << "eq=" << val(EvalConditionForParam(&eq, param, 5)) << std::endl;
  std::cout << "ne=" << val(EvalConditionForParam(&ne, param, 5)) << std::endl;
  std::cout << "lt=" << val(EvalConditionForParam(&lt, param, 5)) << std::endl;
  std::cout << "gt=" << val(EvalConditionForParam(&gt, param, 5)) << std::endl;
  std::cout << "le=" << val(EvalConditionForParam(&le, param, 5)) << std::endl;
  std::cout << "ge=" << val(EvalConditionForParam(&ge, param, 5)) << std::endl;
  std::cout << "land=" << val(EvalConditionForParam(&land, param, 5)) << std::endl;
  std::cout << "lor=" << val(EvalConditionForParam(&lor, param, 5)) << std::endl;
  std::cout << "lnot=" << val(EvalConditionForParam(&lnot, param, 5)) << std::endl;
  std::cout << "unknown=" << val(EvalConditionForParam(&add, param, 5)) << std::endl;
  return 0;
}
