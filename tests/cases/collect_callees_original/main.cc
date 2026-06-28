#include <iostream>
int main() {
  FunctionDecl f("foo"), g("bar");
  Expr leaf;
  CallExpr call_f(&f);
  call_f.addArg(&leaf);
  CallExpr call_g(&g);
  call_g.addArg(&call_f);
  std::unordered_set<std::string> callees;
  CollectCallees(&call_g, callees);
  std::cout << "count = " << callees.Size << std::endl;
  bool has_foo = false, has_bar = false;
  for (int i = 0; i < callees.Size; ++i) {
    if (callees.Items[i] == "foo") has_foo = true;
    if (callees.Items[i] == "bar") has_bar = true;
  }
  std::cout << "has_foo = " << has_foo << std::endl;
  std::cout << "has_bar = " << has_bar << std::endl;
  return 0;
}
