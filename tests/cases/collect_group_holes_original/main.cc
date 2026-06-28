#include <iostream>
int main() {
  FunctionDecl f("foo"), g("bar"), h("baz");
  std::unordered_set<std::string> group;
  group.insert("foo");
  group.insert("bar");
  Expr leaf;
  CallExpr call_f(&f);
  call_f.addArg(&leaf);
  CallExpr call_g(&g);
  call_g.addArg(&call_f);
  CallExpr call_h(&h);
  call_h.addArg(&call_g);
  std::vector<CallExpr *> holes;
  CollectGroupHoles(&call_h, group, holes);
  std::cout << "count = " << holes.size() << std::endl;
  bool has_f = false, has_g = false;
  for (int i = 0; i < holes.size(); ++i) {
    if (holes[i]->getDirectCallee()->getNameAsString() == "foo") has_f = true;
    if (holes[i]->getDirectCallee()->getNameAsString() == "bar") has_g = true;
  }
  std::cout << "has_foo = " << has_f << std::endl;
  std::cout << "has_bar = " << has_g << std::endl;
  return 0;
}
