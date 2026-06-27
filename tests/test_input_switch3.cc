int fact_switch(int n) {
  switch (n) {
    case 0:
    case 1: return 1;
    default: return n * fact_switch(n - 1);
  }
}
