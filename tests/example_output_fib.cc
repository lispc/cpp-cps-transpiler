
// ================================
// Generated CPS + Trampoline code
// ================================

// === Generated CPS code for function: fib ===

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

class CpsClosure_1 : public UtilFunc {
  public:
  int v0;
  fibArg saved_arg;
  UtilFunc* cont;
  CpsClosure_1(int v0, fibArg saved_arg, UtilFunc* cont) : v0(v0), saved_arg(saved_arg), cont(cont) {}
  Unit<fibArg> eval(int v1) {
    auto arg = saved_arg;
    auto n = arg.n;
    return Unit<fibArg>(fibArg((v0 + v1), cont), advance, false);
  }
};

class CpsClosure_0 : public UtilFunc {
  public:
  fibArg saved_arg;
  UtilFunc* cont;
  CpsClosure_0(fibArg saved_arg, UtilFunc* cont) : saved_arg(saved_arg), cont(cont) {}
  Unit<fibArg> eval(int v0) {
    auto arg = saved_arg;
    auto n = arg.n;
    return Unit<fibArg>(fibArg(n - 2, new CpsClosure_1(v0, saved_arg, cont)), fib_cps, false);
  }
};

Unit<fibArg> fib_cps(fibArg arg) {
  auto n = arg.n;
  return n <= 1
    ? Unit<fibArg>(fibArg(n, arg.f), advance, false)
    : Unit<fibArg>(fibArg(n - 1, new CpsClosure_0(arg, arg.f)), fib_cps, false);
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


