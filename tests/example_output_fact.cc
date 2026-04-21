
// ================================
// Generated CPS + Trampoline code
// ================================

// === Generated CPS code for function: fact ===

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
  factArg saved_arg;
  UtilFunc* cont;
  CpsClosure_0(factArg saved_arg, UtilFunc* cont) : saved_arg(saved_arg), cont(cont) {}
  Unit<factArg> eval(int v0) {
    auto arg = saved_arg;
    auto n = arg.n;
    return Unit<factArg>(factArg((n * v0), cont), advance, false);
  }
};

Unit<factArg> fact_cps(factArg arg) {
  auto n = arg.n;
  return n <= 1
    ? Unit<factArg>(factArg(1, arg.f), advance, false)
    : Unit<factArg>(factArg(n - 1, new CpsClosure_0(arg, arg.f)), fact_cps, false);
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


