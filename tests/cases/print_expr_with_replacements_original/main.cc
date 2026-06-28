int main() {
  Expr a, b;
  BinaryOperator add(&a, &b);
  std::unordered_map<const Expr *, std::string> Repls;
  Repls[&add] = "REPLACED";
  std::printf("leaf = %s\n", PrintExprWithReplacements(&a, Repls, nullptr).c_str());
  std::printf("replaced = %s\n", PrintExprWithReplacements(&add, Repls, nullptr).c_str());
  Expr foo;
  CallExpr call(&foo, 2);
  call.setArg(0, &a);
  call.setArg(1, &b);
  std::printf("call = %s\n", PrintExprWithReplacements(&call, Repls, nullptr).c_str());
  return 0;
}
