# CPS Transpiler

基于 LLVM/Clang LibTooling 的 C++ 源码到源码 transpiler，自动将**递归函数**转换为 **CPS（Continuation Passing Style）+ Trampoline** 的迭代版本，消除调用栈溢出的风险。

灵感来自手写版 `cps.cc` —— 本项目旨在**自动化**同样的转换过程。

---

## 快速开始

### 依赖

- macOS / Linux
- CMake >= 3.20
- LLVM/Clang 开发库（本项目使用 LLVM 21）
- Homebrew 安装的 LLVM：
  ```bash
  brew install llvm
  ```

### 构建

```bash
mkdir build && cd build
cmake .. \
  -DLLVM_DIR=/opt/homebrew/Cellar/llvm/21.1.1/lib/cmake/llvm \
  -DClang_DIR=/opt/homebrew/Cellar/llvm/21.1.1/lib/cmake/clang
make -j$(sysctl -n hw.ncpu)
```

### 运行

```bash
# 转换递归函数并打印生成的 CPS 代码
./cps-transpiler ../tests/test_input_fib.cc --

# 保存到文件
./cps-transpiler ../tests/test_input_fib.cc -- > output.cc
```

### 测试

项目目前包含 3 组测试：

```bash
# 1. fibonacci —— 双边加法递归（CPS + Trampoline）
clang++ -std=c++17 ../tests/example_output_fib.cc -o example_output_fib
./example_output_fib
# fib(0) = 0 ... fib(10) = 55

# 2. 阶乘 —— 单边乘法递归（CPS + Trampoline）
clang++ -std=c++17 ../tests/example_output_fact.cc -o example_output_fact
./example_output_fact
# fact(0) = 1 ... fact(10) = 3628800

# 3. clamp_down —— 纯尾递归（自动优化为 while 循环）
clang++ -std=c++17 ../tests/example_output_tailrec.cc -o example_output_tailrec
./example_output_tailrec
# clamp_down(15) = 10, clamp_down(100) = 10
```

---

## 示例

### 输入（递归版）

```cpp
int fib(int n) {
  if (n <= 1) return n;
  return fib(n - 1) + fib(n - 2);
}
```

### 输出（自动生成的 CPS + Trampoline 迭代版）

```cpp
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

// ... UtilFunc, CpsClosure_0, CpsClosure_1 ...

Unit<fibArg> fib_cps(fibArg arg) {
  auto n = arg.n;
  return n <= 1
    ? Unit<fibArg>(fibArg(n, arg.f), advance, false)
    : Unit<fibArg>(fibArg(n - 1, new CpsClosure_1(arg, arg.f)), fib_cps, false);
}

int fib(int n) {
  return trampoline(Unit<fibArg>(fibArg(n, new UtilFunc()), fib_cps, false)).n;
}
```

完整生成代码见 [`example_output_fib.cc`](example_output_fib.cc)。

---

## 工作原理

### 为什么需要 CPS？

`fib(n-1) + fib(n-2)` 不是**尾递归**，编译器无法直接优化为循环。CPS 将"递归调用之后的剩余计算"显式化为一个对象（continuation），从而让所有调用都变成尾调用。

### 转换流水线

```
递归 C++ 源码
     |
     v
[Clang AST 解析]
     |
     v
[递归检测] —— 检查函数体内是否有直接调用自身的 CallExpr
     |
     v
[尾递归检测]
     |-- 是 pure tail recursion? --> [while 循环优化]
     |-- 否                              |
     v                                   v
[CPS 变换器]                    简洁的 while(1)
     - 打包参数为 Arg 结构
     - 双边递归 → 嵌套 Closure
     - 单边递归 → 单 Closure 捕获非递归侧值
     - 基础情况通过 continuation 传递结果
     |
     v
[代码生成]
     - Arg 结构体、Unit 模板、UtilFunc 基类
     - 具体 Closure 类、CPS 函数、advance、trampoline
     - 保留原签名的 wrapper 函数
```

### 尾递归优化

如果 `return` 后面**直接**就是递归调用（没有任何后续运算），transpiler 会跳过 CPS，直接生成等价的 `while` 循环：

```cpp
// 输入
int clamp_down(int n) {
  if (n <= 10) return n;
  return clamp_down(n - 1);   // 纯尾递归
}

// 输出（无需 trampoline/closure）
int clamp_down(int n) {
  while (1) {
    if (n <= 10) return n;
    auto new_n = n - 1;
    n = new_n;
  }
}
```

这比 CPS+Trampoline 更高效（无堆分配、无虚函数调用）。

### Trampoline（针对非尾递归）

对于 `fib`、`fact` 这类在递归调用后还有运算的函数，CPS 转换后由外层 `while(1)` 循环驱动整个计算，直到 `finished` 标志为真，彻底消除了递归调用栈的增长。

---

## 项目结构

```
cps/
├── cps.cc                  # 手写参考版（Continuation Passing Style）
├── CMakeLists.txt
├── DESIGN.md               # 设计草案（供讨论用）
├── README.md
├── tests/
│   ├── test_input_fib.cc       # 测试输入：双边递归 fibonacci
│   ├── example_output_fib.cc   # Transpiler 输出（可编译运行）
│   ├── test_input_fact.cc      # 测试输入：单边递归 factorial
│   ├── example_output_fact.cc
│   ├── test_input_tailrec.cc   # 测试输入：纯尾递归
│   └── example_output_tailrec.cc
└── src/
    ├── main.cc             # Transpiler 入口（Clang Tooling 框架）
    ├── cps_generator.h/.cc # 核心：AST 分析 + CPS 代码生成 + 尾递归优化
    └── recursion_detector.cc
```

---

## 当前支持 & 限制

### ✅ 已支持

- 单参数函数，返回基本类型（如 `int f(int x)`）
- 函数体形式：`if (cond) return base_expr; return recursive_expr;`
- `recursive_expr` 中递归调用嵌套在 `+ - * /` 二元运算中
- 双边递归（`fib(n-1) + fib(n-2)`）
- 单边递归（`n * fact(n-1)`）
- 直接递归（函数体内直接调用自身）
- **纯尾递归自动优化为 `while` 循环**（无需 trampoline）

### 🚧 尚未支持（后续扩展方向）

- 多参数函数
- 相互递归
- 更复杂的控制流（嵌套 if、switch）
- 任意非递归子表达式作为递归调用的上下文

---

## 设计思路

本项目遵循**最小可行产品（MVP）**原则：

1. **先跑通**：从最简单的 `int fib(int)` 开始，验证整个流水线（解析 → 检测 → 变换 → 生成 → 编译 → 运行）
2. **再扩展**：逐步支持多参数、更多表达式类型、尾递归优化等

核心算法在 `src/cps_generator.cc` 中的 `CpsExpr`：对 AST 表达式做深度优先分析，遇到递归调用就生成启动 trampoline 下一跳的 `Unit`，遇到非递归部分就生成等待结果的 Closure，遇到二元运算符则根据左右两边是否含递归调用来选择生成策略。

---

## 参考

- 手写参考：[cps.cc](cps.cc) —— 很久以前手写的 CPS + Trampoline fibonacci
- Continuation Passing Style（Wikipedia）
- [Clang LibTooling 文档](https://clang.llvm.org/docs/LibTooling.html)
