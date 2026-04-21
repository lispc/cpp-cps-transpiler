// === 自动生成的 CPS + Trampoline 代码（来自 cps-transpiler）===
// 输入: tests/test_input_fact.cc

class UtilFunc;

struct factArg {
  int n;
  UtilFunc* f;
  factArg(int n, UtilFunc* f) : n(n), f(f) {}
};

template <typename Arg>
struct Unit {
  Arg arg;
  Unit<Arg> (*nextf)(Arg);
  bool finished;
  Unit(Arg arg, Unit<Arg> (*nextf)(Arg), bool finished)
      : arg(arg), nextf(nextf), finished(finished) {}
};

Unit<factArg> advance(factArg);
Unit<factArg> fact_cps(factArg);

class UtilFunc {
public:
  virtual Unit<factArg> eval(int x) {
    return Unit<factArg>(factArg(x, this), advance, true);
  }
};

class CpsClosure_0 : public UtilFunc {
public:
  int saved_lhs;
  UtilFunc* cont;
  CpsClosure_0(int saved_lhs, UtilFunc* cont) : saved_lhs(saved_lhs), cont(cont) {}
  Unit<factArg> eval(int rval) {
    return Unit<factArg>(factArg(saved_lhs * rval, cont), advance, false);
  }
};

Unit<factArg> fact_cps(factArg arg) {
  auto n = arg.n;
  return n <= 1
    ? Unit<factArg>(factArg(1, arg.f), advance, false)
    : Unit<factArg>(factArg(n - 1, new CpsClosure_0(n, arg.f)), fact_cps, false);
}

Unit<factArg> advance(factArg arg) {
  auto res = arg.f->eval(arg.n);
  delete arg.f;
  return res;
}

template <typename Arg>
Arg trampoline(Unit<Arg> t) {
  auto pp = t;
  while (1) {
    if (pp.finished) return pp.arg;
    pp = pp.nextf(pp.arg);
  }
}

int fact(int n) {
  return trampoline(Unit<factArg>(factArg(n, new UtilFunc()), fact_cps, false)).n;
}

// === 测试 main ===
#include <iostream>
#include <cassert>

int main() {
  assert(fact(0) == 1);
  assert(fact(1) == 1);
  assert(fact(2) == 2);
  assert(fact(3) == 6);
  assert(fact(5) == 120);
  assert(fact(10) == 3628800);

  for (int i = 0; i <= 10; ++i) {
    std::cout << "fact(" << i << ") = " << fact(i) << std::endl;
  }
  std::cout << "All tests passed!" << std::endl;
  return 0;
}
