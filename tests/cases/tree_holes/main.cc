#include <iostream>
int main() {
  TreeNode n4{4, {}};
  TreeNode n5{5, {}};
  TreeNode n2{2, {}};
  n2.children.push_back(&n4);
  n2.children.push_back(&n5);
  TreeNode n3{3, {}};
  TreeNode root{1, {}};
  root.children.push_back(&n2);
  root.children.push_back(&n3);
  IntList holes;
  collect_holes(&root, holes);
  for (int i = 0; i < holes.get_size(); ++i) {
    std::cout << "hole[" << i << "] = " << holes[i] << std::endl;
  }
  return 0;
}
