int main() {
  IRExprNode e1{{3}, "x = 1"};
  IRExprNode e2{{3}, "y = x + 1"};
  IRBlockNode inner{{1}, {&e1, &e2}, 2};
  IRExprNode e3{{3}, "z = 0"};
  IRBlockNode inner2{{1}, {&e3}, 1};
  IRIfNode cond2{{2}, "y > 1", &inner2, nullptr};
  IRIfNode cond1{{2}, "x > 0", &inner, &cond2};
  IRBlockNode top{{1}, {&cond1, &e1}, 2};
  StrBuf os;
  printBlock(os, &top, 0);
  printf("%s", os.data);
  return 0;
}
