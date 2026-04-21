# CPS Transpiler

将 C++ 递归函数自动转换为**无栈溢出风险**的迭代版本。

基于 LLVM/Clang LibTooling 实现源码到源码的 transpilation，支持两种转换策略：
- **尾递归优化（TRO）**：纯尾递归 → 高效的 `while` 循环
- **CPS + Trampoline**：非尾递归 → Continuation Passing Style + 蹦床迭代

---

## 快速开始

### 依赖

- macOS / Linux
- CMake >= 3.20
- LLVM/Clang 开发库（本项目使用 LLVM 21）

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
# 转换并打印生成的 CPS 代码
./cps-transpiler ../tests/test_input_fib.cc --

# 保存到文件
./cps-transpiler ../tests/test_input_fib.cc -- > output.cc
```

### 测试

```bash
python3 run_tests.py
```

---

## 效果对比

### 输入

```cpp
int fib(int n) {
  if (n <= 1) return n;
  return fib(n - 1) + fib(n - 2);
}
```

### 输出（自动生成）

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

// ... closures ...

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

完整代码见 [`tests/example_output_fib.cc`](tests/example_output_fib.cc)。

---

## 支持的转换场景

| 场景 | 示例 | 策略 |
|------|------|------|
| 双边加法递归 | `fib(n-1) + fib(n-2)` | CPS + Trampoline |
| 单边乘法递归 | `n * fact(n-1)` | CPS + Trampoline |
| 纯尾递归 | `return clamp_down(n-1)` | `while` 循环 |
| 函数调用包裹 | `double_it(fact(n-1))` | CPS + Trampoline |
| 一元运算符 | `-(neg_fact(n-1))` | CPS + Trampoline |
| 条件表达式 | `cond ? f(n-1) : f(n-2)` | CPS + Trampoline |

---

## 工作原理

### 为什么需要 CPS？

`fib(n-1) + fib(n-2)` 不是**尾递归**，编译器无法直接优化为循环。CPS 将"递归调用之后的剩余计算"显式化为 continuation 对象，从而让所有调用都变成尾调用。

### 转换流水线

```
递归 C++ 源码
     |
     v
[Clang AST 解析]
     |
     v
[递归检测]
     |
     v
[尾递归检测]
     |-- 所有递归调用都在尾部位置? --> [while 循环]
     |-- 否                                |
     v                                     v
[CPS 变换器]                       简洁迭代
     - 在表达式中收集递归调用（holes）
     - 按 DFS 顺序生成嵌套 Closure 链
     - 最后一个 Closure 计算最终结果
     |
     v
[代码生成]
     - Arg 结构体、Unit 模板、UtilFunc 基类
     - 具体 Closure 类、CPS 函数、advance、trampoline
     - 保留原签名的 wrapper 函数
```

### 通用表达式 CPS 变换

旧版仅支持 `+ - * /` 二元运算符。新版支持**任意表达式上下文**中的递归调用：

- **二元运算符**：`a + f(n-1)`、`a * f(n-1)`
- **一元运算符**：`-f(n-1)`、`!f(n-1)`
- **函数调用**：`foo(f(n-1), bar)`
- **条件表达式**：`cond ? f(n-1) : f(n-2)`
- **数组下标**：`arr[f(n)]`
- **成员访问**：`obj.val[f(n)]`

核心算法：
1. `CollectHoles`：在 AST 中收集所有直接递归调用
2. `PrintExprWithReplacements`：将已处理的 hole 替换为变量名
3. `CpsExprWithHoles`：生成嵌套 Closure，每个 Closure 的 `eval` 接收一个结果并启动下一个递归调用或计算最终表达式

### 尾递归优化

当函数体中**所有**递归调用都处于尾部位置时，直接生成 `while` 循环，无需任何 CPS 开销：

```cpp
// 输入
int clamp_down(int n) {
  if (n <= 10) return n;
  return clamp_down(n - 1);
}

// 输出
int clamp_down(int n) {
  while (1) {
    if (n <= 10) return n;
    auto new_n = n - 1;
    n = new_n;
  }
}
```

### Trampoline

CPS 转换后的函数返回 `Unit<Arg>` 而不是直接递归调用。外层 `while(1)` 循环不断调用 `nextf` 直到 `finished` 为真，彻底消除了调用栈增长。

---

## 项目结构

```
cps/
├── cps.cc                    # 手写参考实现
├── CMakeLists.txt
├── DESIGN.md                 # 设计草案
├── README.md
├── run_tests.py              # 自动化回归测试
├── tests/
│   ├── test_input_fib.cc
│   ├── example_output_fib.cc
│   ├── test_input_fact.cc
│   ├── example_output_fact.cc
│   ├── test_input_tailrec.cc
│   ├── example_output_tailrec.cc
│   ├── test_input_funcwrap.cc   # 函数调用包裹递归
│   └── test_input_unary.cc      # 一元运算符包裹递归
└── src/
    ├── main.cc               # Clang Tooling 前端
    ├── cps_generator.h/.cc   # AST 分析 + CPS 代码生成 + 尾递归优化
    ├── code_emitter.h        # 代码生成抽象层（缩进管理）
    └── recursion_detector.cc # 占位文件
```

---

## 已支持 & 限制

### ✅ 已支持

- 单参数函数，返回基本类型（如 `int f(int x)`）
- 函数体形式：`if (cond) return base_expr; return recursive_expr;`
- `recursive_expr` 中递归调用可嵌套在**任意表达式**中
- 双边递归、单边递归、纯尾递归
- 直接递归（函数体内直接调用自身）
- 尾递归自动优化为 `while` 循环

### 🚧 限制

- 多参数函数（结构已支持，但缺乏测试）
- 更复杂的控制流（嵌套 if、switch、局部变量声明）
- 递归调用嵌套在另一递归调用的参数中（如 `fact(fact(n-1))`）
- 相互递归

---

## 参考

- 手写参考：[cps.cc](cps.cc)
- [Continuation Passing Style - Wikipedia](https://en.wikipedia.org/wiki/Continuation-passing_style)
- [Clang LibTooling](https://clang.llvm.org/docs/LibTooling.html)
