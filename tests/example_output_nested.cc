Error while trying to load a compilation database:
Could not auto-detect compilation database for file "tests/test_input_nested.cc"
No compilation database found in /Users/zhangzhuo/repos/personal/cps/tests or any parent directory
fixed-compilation-database: Error while opening fixed database: No such file or directory
json-compilation-database: Error while opening JSON database: No such file or directory
Running without flags.
[Detected recursive function] nested_fact

// ================================
// Generated iterative code
// ================================

// === Generated defunctionalized code for function: nested_fact ===

#include <vector>

struct nested_factArg {
  int n;
  nested_factArg(int n) : n(n) {}
  nested_factArg() : n(0) {}
};

enum class nested_factCont {
  Done,
  K0,
  K1,
};

struct nested_factFrame {
  nested_factCont tag;
  std::vector<int> vals;
  bool has_arg;
  nested_factArg saved_arg;
  nested_factFrame(nested_factCont t) : tag(t), has_arg(false) {}
  nested_factFrame(nested_factCont t, std::vector<int> v) : tag(t), vals(std::move(v)), has_arg(false) {}
  nested_factFrame(nested_factCont t, std::vector<int> v, nested_factArg a) : tag(t), vals(std::move(v)), has_arg(true), saved_arg(a) {}
};

int nested_fact(int n) {
  std::vector<nested_factFrame> k;
  k.emplace_back(nested_factCont::Done);
  nested_factArg arg = nested_factArg(0);
  arg.n = n;
  int val = 0;
  dispatch:
  while (1) {
    auto n = arg.n;
    if (n <= 1) {
      val = 1;
    }
    else {
      k.emplace_back(nested_factCont::K0, std::vector<int>{}, arg);
      arg = nested_factArg(n - 1);
      goto dispatch;
    }
    while (!k.empty()) {
      auto f = k.back();
      k.pop_back();
      switch (f.tag) {
      case nested_factCont::Done:
        return val;
      case nested_factCont::K0: {
        k.emplace_back(nested_factCont::K1, std::vector<int>{val}, f.saved_arg);
        arg = nested_factArg(val);
        goto dispatch;
      }
      case nested_factCont::K1: {
        break;
      }
      }
    }
    return val;
  }
}


