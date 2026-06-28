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
  std::cout << "has_group_h = " << ContainsGroupCall(&call_h, group) << std::endl;
  std::cout << "has_group_f = " << ContainsGroupCall(&call_f, group) << std::endl;
  std::cout << "leaf = " << ContainsGroupCall(&leaf, group) << std::endl;
  return 0;
}
