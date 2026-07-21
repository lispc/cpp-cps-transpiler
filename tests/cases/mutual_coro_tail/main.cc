int main() {
  LNode l3{"d", 0, nullptr};
  RNode r2{"c", &l3};
  LNode l2{"SKIP", 1, &r2};
  RNode r1{"b", &l2};
  LNode l1{"a", 0, &r1};
  StrBuf os;
  walkLeaf(&l1, os);
  printf("%s\n", os.data);
  return 0;
}
