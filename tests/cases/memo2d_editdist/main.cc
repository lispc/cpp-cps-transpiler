#include <iostream>
#include <cstring>
int main() {
  const char *a = "kitten", *b = "sitting";
  std::cout << editdist(a, b, strlen(a), strlen(b)) << std::endl;
  const char *c = "flaw", *d = "lawn";
  std::cout << editdist(c, d, strlen(c), strlen(d)) << std::endl;
  std::cout << editdist("", "abc", 0, 3) << std::endl;
  return 0;
}
