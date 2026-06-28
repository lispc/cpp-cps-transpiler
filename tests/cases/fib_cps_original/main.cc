int main() {
  int fib0 = trampoline(Unit<FibArg>(FibArg(0, new UtilFunc()), fib_rec_cps, false)).x;
  int fib1 = trampoline(Unit<FibArg>(FibArg(1, new UtilFunc()), fib_rec_cps, false)).x;
  int fib2 = trampoline(Unit<FibArg>(FibArg(2, new UtilFunc()), fib_rec_cps, false)).x;
  int fib3 = trampoline(Unit<FibArg>(FibArg(3, new UtilFunc()), fib_rec_cps, false)).x;
  int fib4 = trampoline(Unit<FibArg>(FibArg(4, new UtilFunc()), fib_rec_cps, false)).x;
  int fib5 = trampoline(Unit<FibArg>(FibArg(5, new UtilFunc()), fib_rec_cps, false)).x;

  printf("fib0 = %d\n", fib0);
  printf("fib1 = %d\n", fib1);
  printf("fib2 = %d\n", fib2);
  printf("fib3 = %d\n", fib3);
  printf("fib4 = %d\n", fib4);
  printf("fib5 = %d\n", fib5);
  return 0;
}
