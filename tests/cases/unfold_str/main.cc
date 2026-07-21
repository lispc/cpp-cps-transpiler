#include <iostream>
int main() {
  std::cout << shout('a', 0).buf << std::endl;
  std::cout << shout('b', 3).buf << std::endl;
  std::cout << shout('z', 6).buf << std::endl;
  return 0;
}
