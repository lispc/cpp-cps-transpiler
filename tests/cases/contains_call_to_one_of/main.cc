int main() {
  CallExpr leaf("leaf");
  CallExpr inner("inner", {&leaf});
  CallExpr root("root", {&inner});

  std::vector<std::string> Hits;
  Hits.push_back("leaf");
  Hits.push_back("missing");

  std::vector<std::string> Misses;
  Misses.push_back("missing");
  Misses.push_back("other");

  int found = ContainsCallToOneOf(&root, Hits) ? 1 : 0;
  int not_found = ContainsCallToOneOf(&root, Misses) ? 1 : 0;
  int empty = ContainsCallToOneOf(nullptr, Hits) ? 1 : 0;

  printf("found = %d\n", found);
  printf("not_found = %d\n", not_found);
  printf("empty = %d\n", empty);
  return 0;
}
