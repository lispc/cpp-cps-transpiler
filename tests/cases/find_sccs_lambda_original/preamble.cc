#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <cstdio>

static std::vector<std::vector<std::string>>
FindSCCs(const std::unordered_map<std::string, std::unordered_set<std::string>> &Graph) {
  std::vector<std::vector<std::string>> SCCs;
  std::unordered_map<std::string, int> Index;
  std::unordered_map<std::string, int> LowLink;
  std::unordered_set<std::string> OnStack;
  std::vector<std::string> Stack;
  int idx = 0;

  std::function<void(const std::string &)> strongconnect =
      [&](const std::string &Name) {
    Index[Name] = idx;
    LowLink[Name] = idx;
    ++idx;
    Stack.push_back(Name);
    OnStack.insert(Name);

    auto It = Graph.find(Name);
    if (It != Graph.end()) {
      for (const std::string &Next : It->second) {
        if (!Graph.count(Next))
          continue;
        if (!Index.count(Next)) {
          strongconnect(Next);
          LowLink[Name] = std::min(LowLink[Name], LowLink[Next]);
        } else if (OnStack.count(Next)) {
          LowLink[Name] = std::min(LowLink[Name], Index[Next]);
        }
      }
    }

    if (LowLink[Name] == Index[Name]) {
      std::vector<std::string> SCC;
      while (true) {
        std::string W = Stack.back();
        Stack.pop_back();
        OnStack.erase(W);
        SCC.push_back(W);
        if (W == Name) break;
      }
      SCCs.push_back(SCC);
    }
  };

  for (const auto &KV : Graph) {
    if (!Index.count(KV.first))
      strongconnect(KV.first);
  }
  return SCCs;
}
