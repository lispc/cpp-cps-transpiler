


class UtilFunc;

struct fibArg {
  int n;
  UtilFunc* f;
  fibArg(int n, UtilFunc* f) : n(n), f(f) {}
};

template <typename Arg>
struct Unit {
  Arg arg;
  Unit<Arg> (*nextf)(Arg);
  bool finished;
  Unit(Arg arg, Unit<Arg> (*nextf)(Arg), bool finished)
      : arg(arg), nextf(nextf), finished(finished) {}
};

Unit<fibArg> advance(fibArg);
Unit<fibArg> fib_cps(fibArg);

class UtilFunc {
public:
  virtual Unit<fibArg> eval(int x) {
    return Unit<fibArg>(fibArg(x, this), advance, true);
  }
};

class CpsClosure_0 : public UtilFunc {
public:
  int lval;
  UtilFunc* cont;
  CpsClosure_0(int lval, UtilFunc* cont) : lval(lval), cont(cont) {}
  Unit<fibArg> eval(int rval) {
    return Unit<fibArg>(fibArg(lval + rval, cont), advance, false);
  }
};

class CpsClosure_1 : public UtilFunc {
public:
  fibArg saved_arg;
  UtilFunc* cont;
  CpsClosure_1(fibArg saved_arg, UtilFunc* cont) : saved_arg(saved_arg), cont(cont) {}
  Unit<fibArg> eval(int lval) {
    auto arg = saved_arg;
    auto n = arg.n;
    return Unit<fibArg>(fibArg(n - 2, new CpsClosure_0(lval, cont)), fib_cps, false);
  }
};

Unit<fibArg> fib_cps(fibArg arg) {
  auto n = arg.n;
  return n <= 1
    ? Unit<fibArg>(fibArg(n, arg.f), advance, false)
    : Unit<fibArg>(fibArg(n - 1, new CpsClosure_1(arg, arg.f)), fib_cps, false);
}

Unit<fibArg> advance(fibArg arg) {
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

int fib(int n) {
  return trampoline(Unit<fibArg>(fibArg(n, new UtilFunc()), fib_cps, false)).n;
}



#include <iostream>

int main() {
  for (int i = 0; i <= 10; ++i) {
    std::cout << "fib(" << i << ") = " << fib(i) << std::endl;
  }
  return 0;
}
