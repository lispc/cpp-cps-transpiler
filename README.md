# CPS Transpiler

将 C++ **直接递归函数**自动转换为**无栈溢出风险**的迭代版本。

本项目基于 LLVM/Clang LibTooling 实现源码到源码的 transpilation。与早期版本不同，当前实现采用**可扩展的程序变换规则引擎**：先对函数体做统一归一化，再按优先级尝试一组专门化的转换规则，从而把不同形状的递归代码转换成最贴切的迭代形式。

---

## 快速开始

### 依赖

- macOS / Linux
- CMake >= 3.20
- LLVM/Clang 开发库（本项目使用 LLVM 22）

```bash
brew install llvm
```

### 构建

```bash
mkdir build && cd build
cmake .. \
  -DLLVM_DIR=/opt/homebrew/Cellar/llvm/22.1.4/lib/cmake/llvm \
  -DClang_DIR=/opt/homebrew/Cellar/llvm/22.1.4/lib/cmake/clang
make -j$(sysctl -n hw.ncpu)
```

### 运行

```bash
# 转换并打印生成的迭代代码
./cps-transpiler ../tests/test_input_fib.cc --

# 保存到文件
./cps-transpiler ../tests/test_input_fib.cc -- > output.cc
```

### 测试

```bash
python3 run_tests.py
```

---

## Showcase：二项式系数 `C(n, k)`

输入：经典的多参数双边递归

```cpp
int binomial(int n, int k) {
  if (k == 0 || k == n) return 1;
  return binomial(n - 1, k - 1) + binomial(n - 1, k);
}
```

自动输出：可读的多参数显式栈迭代版本

```cpp
#include <vector>

struct binomialFrame {
  int n;
  int k;
  binomialFrame(int n, int k) : n(n), k(k) {}
};

int binomial(int n, int k) {
  std::vector<binomialFrame> stack;
  stack.emplace_back(n, k);
  int result = 0;
  while (!stack.empty()) {
    auto cur = stack.back();
    stack.pop_back();
    if (cur.k == 0 || cur.k == cur.n) {
      result += 1;
    }
    else {
      stack.emplace_back(cur.n - 1, cur.k);
      stack.emplace_back(cur.n - 1, cur.k - 1);
    }
  }
  return result;
}
```

完整文件见 [`tests/test_input_showcase.cc`](tests/test_input_showcase.cc) 与 [`tests/example_output_showcase.cc`](tests/example_output_showcase.cc)。

---

## 支持的转换规则

转换器内部维护一条**有序规则链**。对同一个递归函数，按下面的顺序匹配，第一个适用的规则获胜。

| 优先级 | 规则 | 适用场景 | 输出形式 | 示例 |
|--------|------|----------|----------|------|
| 1 | **TailRecursionRule** | 所有递归调用都处于尾部位置 | `while (1)` + 参数重赋值 | `clamp_down(n-1)` |
| 2 | **AccumulatorRule** | 单递归调用与可结合运算组合 | `while` + 累加器/累乘器 | `n * fact(n-1)` |
| 3 | **TuplingRule** | k 阶线性齐次递推 | O(n) 数组/元组循环 | `fib(n)=fib(n-1)+fib(n-2)` |
| 4 | **BinaryStackRule** | 两个递归调用直接由 `+` `*` `\|` `^` 连接 | 显式帧栈 | `fib(n-1)+fib(n-2)` |
| 5 | **GenericStackRule** | 任意直接递归表达式 | `std::variant` 显式栈 + 值栈 | `min(f(n-1), f(n-2))` |
| 6 | **DefunctionalizedRule** | 兜底：单递归调用或嵌套递归表达式 | enum + switch + 帧栈 | `double_it(fact(n-1))` |

规则顺序很重要：TuplingRule 必须在 BinaryStackRule 之前，否则 `fib` 会被展开成 O(2^n) 的显式栈版本；AccumulatorRule 必须在 GenericStackRule 之前，否则 `fact` 也会走通用栈。

---

## 效果对比

### 1. 尾递归 → `while` 循环

输入：

```cpp
int clamp_down(int n) {
  if (n <= 10) return n;
  return clamp_down(n - 1);
}
```

输出：

```cpp
int clamp_down(int n) {
  while (1) {
    if (n <= 10) return n;
    auto new_n = n - 1;
    n = new_n;
  }
}
```

### 2. 单边乘法递归 → 累加器

输入：

```cpp
int fact(int n) {
  if (n <= 1) return 1;
  return n * fact(n - 1);
}
```

输出：

```cpp
int fact(int n) {
  int product = 1;
  while (!(n <= 1)) {
    product = product * n;
    auto next_n = n - 1;
    n = next_n;
  }
  return product;
}
```

### 3. Fibonacci → Tupling O(n)

输入：

```cpp
int fib(int n) {
  if (n <= 1) return n;
  return fib(n - 1) + fib(n - 2);
}
```

输出（节选）：

```cpp
#include <array>

int fib(int n) {
  if (n <= 1) {
    if (n <= 1) return n;
    return 0;
  }
  std::array<int, 2> vals;
  vals[0] = 0;
  vals[1] = 1;
  for (int i = 2; i <= n; ++i) {
    int next = (vals[1] + vals[0]);
    for (int j = 0; j < 1; ++j)
      vals[j] = vals[j + 1];
    vals[1] = next;
  }
  return vals[1];
}
```

### 4. 二元 min 递归 → 通用显式栈

输入：

```cpp
int min_tree(int n) {
  if (n <= 0) return 10;
  if (n == 1) return 1;
  return min(min_tree(n - 1), min_tree(n - 2));
}
```

输出（节选）：

```cpp
#include <vector>
#include <variant>
#include <algorithm>

struct min_treeFrame {
  int n;
  min_treeFrame(int n) : n(n) {}
};

struct min_treeCombineMarker {
  int count;
  min_treeCombineMarker(int c) : count(c) {}
};

int min_tree(int n) {
  std::vector<std::variant<min_treeFrame, min_treeCombineMarker>> stack;
  stack.emplace_back(min_treeFrame(n));
  std::vector<int> values;
  while (!stack.empty()) {
    auto entry = stack.back();
    stack.pop_back();
    if (std::holds_alternative<min_treeCombineMarker>(entry)) {
      int v0 = values.back(); values.pop_back();
      int v1 = values.back(); values.pop_back();
      values.push_back(std::min(v0, v1));
    } else {
      auto cur = std::get<min_treeFrame>(entry);
      auto n = cur.n;
      if (n <= 0)
        values.push_back(10);
      else if (n == 1)
        values.push_back(1);
      else {
        stack.emplace_back(min_treeCombineMarker(2));
        stack.emplace_back(min_treeFrame(n - 1));
        stack.emplace_back(min_treeFrame(n - 2));
      }
    }
  }
  return values.back();
}
```

完整输出见 [`tests/example_output_*.cc`](tests/)。

### 5. 多 base case 累加器

输入：

```cpp
int acc_multi_base(int n) {
  if (n <= 0) return 0;
  if (n == 1) return 0;
  return acc_multi_base(n - 1) + n;
}
```

输出：

```cpp
int acc_multi_base(int n) {
  int sum = 0;
  while (!(n <= 0) && !(n == 1)) {
    sum = sum + n;
    auto next_n = n - 1;
    n = next_n;
  }
  return sum;
}
```

---

## 工作原理

### 整体流水线

```
递归 C++ 源码
     |
     v
[Clang AST 解析]
     |
     v
[递归检测]  ──直接调用自身？──┐
     |                       │
     v                       │
[函数体归一化]                │
  - 提取 leading statements   │
  - 提取 base cases           │
  - 提取 middle statements    │
  - 提取 recursive return     │
     |                       │
     v                       │
[按优先级尝试转换规则]         │
  1. TailRecursionRule       │
  2. AccumulatorRule         │
  3. TuplingRule             │
  4. BinaryStackRule         │
  5. GenericStackRule        │
  6. DefunctionalizedRule ◄──┘
     |
     v
[代码生成] 输出迭代 C++ 代码
```

### 1. 函数体归一化 `AnalyzeBody`

转换器不直接处理原始 AST，而是先把函数体归一化成一种固定的结构：

```
[leading-stmts]*
(if-return)*
[middle-stmts]*
return recursive-expr;
```

- **Leading statements**：出现在第一个 base case / return 之前的语句，例如局部变量声明、日志打印等。这些语句只在函数入口处执行一次。
- **Base cases**：一串 `if (cond) return expr;` 或 `if-else-if` 链。转换器会按顺序检查它们。
- **Middle statements**：出现在最后一个 base case 之后、递归 return 之前的语句。这些语句会在每次迭代中、base-case 检查之后执行。
- **Recursive expression**：最终的 `return` 表达式，里面可能包含一个或多个直接递归调用。

例如：

```cpp
int with_local(int n) {
  if (n <= 1) return 1;      // base case
  auto m = n - 1;             // middle statement
  return with_local(m) * n;   // recursive expression
}
```

### 2. 规则匹配

每个规则实现统一的接口：

```cpp
class TransformationRule {
public:
  virtual bool applies(const FunctionDecl*, const BodyAnalysis&, const GenContext&) = 0;
  virtual std::string apply(const FunctionDecl*, const BodyAnalysis&, GenContext&) = 0;
};
```

`applies` 负责**模式识别**，`apply` 负责**代码生成**。新增一种转换模式只需要新增一个规则类并插入 `CreateDefaultRules()` 的合适位置。

#### TailRecursionRule

收集函数体中所有直接递归调用，如果每个调用都位于尾部位置（即某个分支最终 `return` 的值），则生成 `while (1)` 循环，在循环末尾用 `next_<param>` 更新参数。

#### AccumulatorRule

识别如下形式：

```cpp
return f(new_args) op step;   // op 为 + * | ^
return step op f(new_args);
return min(f(new_args), step);
return max(f(new_args), step);
```

其中 `step` 不依赖递归结果。生成 `while` 循环保存中间结果；变量名根据运算选择（`sum`、`product`、`bits`、`xors`、`min_val`、`max_val`）。

支持多个 base case，但要求它们的返回值相同且不依赖参数（从而可作为累加器的 identity）。循环条件是所有 base case 都不满足：

```cpp
while (!(cond1) && !(cond2) && ...) { ... }
```

#### TuplingRule

识别 k 阶线性递推：

```cpp
f(n) = c1 * f(n-1) + c2 * f(n-2) + ... + ck * f(n-k)
```

当前要求系数 `ci ∈ {+1, -1}`，且每个阶 `1..k` 恰好出现一次。生成 `std::array<int, k>` 保存最近 k 个值，从 `i = k` 线性递推到 `n`，时间复杂度 O(n)、空间复杂度 O(k)。

#### BinaryStackRule

当递归表达式恰好是 `f(a) op f(b)`，且 `op` 为 `+` `*` `|` `^` 时，生成一个只存储参数的帧栈。利用该运算符的结合性，在 DFS 遍历子树时直接累加 `result`，无需额外的值栈。优点是代码简洁、开销小。

#### GenericStackRule

处理任意直接递归表达式。算法核心是“双栈 DFS”：

- **工作栈** `stack<std::variant<Frame, CombineMarker>>`：存储待计算的帧，以及一个专用标记 `CombineMarker{count}`，表示当前帧需要弹出多少个递归结果进行合并。
- **值栈** `values`：存储已经计算好的子结果。

遍历过程：

1. 弹出一个帧。
2. 如果它是 base case，把结果压入 `values`。
3. 如果它是递归节点，先把 `CombineMarker{N}` 压入工作栈，再把 `N` 个子调用帧压入工作栈。这样当子调用全部计算完毕后，栈顶的标记会触发合并。
4. 如果它是 `CombineMarker`，从 `values` 弹出 `count` 个值，代入原始递归表达式计算当前结果，再压回 `values`。

该规则会自动处理 `min(f(n-1), f(n-2))`、`f(n-1) + 2 * f(n-2)` 等结果后加工场景。

#### DefunctionalizedRule

兜底规则，覆盖：

- 单递归调用且本身就是整个递归表达式：`return f(new_args);`
- 更复杂的嵌套递归表达式，例如 `return double_it(f(n-1));`

实现方式为 defunctionalized continuation：用 `enum` 表示当前要执行的 continuation，用 `std::vector<Frame>` 作为continuation 栈，每完成一个递归调用就回到 switch 中决定下一步。该规则保证任意直接递归都能被转换，但生成的代码可读性不如前面的专门规则。

### 3. 代码生成

所有规则共享一套代码生成基础设施：

- `CodeEmitter`：轻量级缩进管理器，支持 `line`、`block`、`raw` 等操作。
- `PrintExpr` / `PrintExprWithReplacements`：把 Clang AST 表达式打印回 C++ 源码，后者支持把指定子表达式替换为变量名。
- `CollectHoles`：在表达式中收集所有直接递归调用，不进入递归调用自身的参数。
- `BuildFunctionSignature` / `EmitFrameStruct`：生成函数签名和参数帧结构体。

---

## 项目结构

```
cps/
├── cps.cc                    # 手写 CPS 参考实现（历史遗留）
├── CMakeLists.txt
├── README.md
├── run_tests.py              # 自动化回归测试
├── benchmarks/               # 性能对比脚本
├── tests/
│   ├── test_input_*.cc       # 测试输入
│   └── example_output_*.cc   # 期望输出样例
└── src/
    ├── main.cc               # Clang Tooling 前端
    ├── cps_generator.h/.cc   # AST 分析、代码生成入口、共享 helper
    ├── code_emitter.h         # 缩进管理代码生成器
    ├── transformation_rule.h  # BodyAnalysis、GenContext、规则接口
    └── transformation_rules.h/.cc  # 具体规则实现
```

---

## 已支持 & 限制

### ✅ 已支持

- 单参数 / 多参数函数，返回基本类型（如 `int`）
- 参数类型为值、指针、引用
- 函数体形式：`[leading] (if-return)* [middle] return recursive-expr;`
- 递归调用可嵌套在**任意表达式**中：二元/一元运算符、函数调用、条件表达式、数组下标等
- 双边递归、单边递归、纯尾递归
- 多个 base case（if-else-if 链），包括 accumulator 规则下的多 base case
- 常见可结合运算的 accumulator 转换：`+`、`*`、`|`、`^`、`min`、`max`
- k 阶线性递推的 tupling 转换

### 🚧 限制

- 只支持**直接递归**（函数体内直接调用自身），不支持相互递归
- 递归调用不能嵌套在另一递归调用的参数中，例如 `fact(fact(n-1))`
- TuplingRule 目前仅支持系数为 `±1` 的线性递推
- 更复杂的控制流（`switch`、循环、异常）不支持
- 生成的代码风格偏向机械翻译，可读性仍有提升空间

---

## 参考

- 手写参考：[cps.cc](cps.cc)
- [Continuation Passing Style - Wikipedia](https://en.wikipedia.org/wiki/Continuation-passing_style)
- [Clang LibTooling](https://clang.llvm.org/docs/LibTooling.html)
- [Recursion Schemes - Wikipedia](https://en.wikipedia.org/wiki/Recursion_schemes)
