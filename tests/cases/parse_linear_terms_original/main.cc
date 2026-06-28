#include <iostream>
int main() {
  std::string func = "f";
  std::string param = "n";
  ValueDecl nDecl("n");
  DeclRefExpr nRef(&nDecl);
  FunctionDecl fDecl("f");

  auto makeF = [&](int k) -> CallExpr * {
    IntegerLiteral *ik = new IntegerLiteral(k);
    BinaryOperator *sub = new BinaryOperator(BO_Sub, &nRef, ik);
    CallExpr *call = new CallExpr(&fDecl);
    call->addArg(sub);
    return call;
  };

  CallExpr *f1 = makeF(1);
  std::vector<LinearTerm> terms;
  int maxOrder = 0;
  bool ok = ParseLinearTerms(f1, func, param, terms, maxOrder);
  std::cout << "single_ok=" << ok << " max=" << maxOrder << " terms=" << terms.size() << std::endl;

  CallExpr *f2 = makeF(2);
  CallExpr *f3 = makeF(3);
  BinaryOperator *sub1 = new BinaryOperator(BO_Sub, f1, f2);
  BinaryOperator *expr = new BinaryOperator(BO_Add, sub1, f3);
  terms.clear(); maxOrder = 0;
  ok = ParseLinearTerms(expr, func, param, terms, maxOrder);
  std::cout << "expr_ok=" << ok << " max=" << maxOrder;
  for (size_t i = 0; i < terms.size(); ++i)
    std::cout << " [" << terms[i].Order << "," << terms[i].Sign << "]";
  std::cout << std::endl;

  BinaryOperator *inner = new BinaryOperator(BO_Sub, f1, f2);
  UnaryOperator *neg = new UnaryOperator(UO_Minus, inner);
  terms.clear(); maxOrder = 0;
  ok = ParseLinearTerms(neg, func, param, terms, maxOrder);
  std::cout << "neg_ok=" << ok << " max=" << maxOrder;
  for (size_t i = 0; i < terms.size(); ++i)
    std::cout << " [" << terms[i].Order << "," << terms[i].Sign << "]";
  std::cout << std::endl;

  return 0;
}
