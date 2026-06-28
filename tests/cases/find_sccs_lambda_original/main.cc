int main() {
  std::unordered_map<std::string, std::unordered_set<std::string>> g;
  g["a"].insert("b");
  g["b"].insert("a");
  g["b"].insert("c");
  g["c"].insert("c");
  g["d"].insert("e");
  g["e"].insert("d");

  auto sccs = FindSCCs(g);
  int scc_ab = 0, scc_c = 0, scc_de = 0;
  for (const auto &scc : sccs) {
    bool has_a = false, has_b = false, has_c_node = false, has_d = false, has_e = false;
    for (const auto &n : scc) {
      if (n == "a") has_a = true;
      if (n == "b") has_b = true;
      if (n == "c") has_c_node = true;
      if (n == "d") has_d = true;
      if (n == "e") has_e = true;
    }
    if (has_a && has_b && scc.size() == 2) scc_ab = 1;
    if (has_c_node && scc.size() == 1) scc_c = 1;
    if (has_d && has_e && scc.size() == 2) scc_de = 1;
  }

  printf("scc_ab = %d\n", scc_ab);
  printf("scc_c = %d\n", scc_c);
  printf("scc_de = %d\n", scc_de);
  return 0;
}
