#include <iostream>
#include <cstring>
int main() {
  const char *a = "abcde", *b = "ace";
  std::cout << lcs(a, b, strlen(a), strlen(b)) << std::endl;
  const char *c = "aggtab", *d = "gxtxayb";
  std::cout << lcs(c, d, strlen(c), strlen(d)) << std::endl;
  std::cout << lcs("abc", "def", 3, 3) << std::endl;
  return 0;
}
