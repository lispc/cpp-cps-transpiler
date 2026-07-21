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
cmake -B build -S . \
  -DLLVM_DIR=/opt/homebrew/Cellar/llvm/22.1.4/lib/cmake/llvm \
  -DClang_DIR=/opt/homebrew/Cellar/llvm/22.1.4/lib/cmake/clang \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

### 运行

```bash
# 转换并打印生成的迭代代码
./build/cps-transpiler tests/test_input_fib.cc --

# 保存到文件
./build/cps-transpiler tests/test_input_fib.cc -- > output.cc
```

### 测试

```bash
python3 run_tests.py              # 直接运行回归测试（用例在 tests/cases/）
cmake --build build --target check # 通过 CMake 运行测试
ctest --test-dir build            # 通过 CTest 运行测试
python3 tests/fuzz_regressions.py # 随机回归 fuzzing
```

测试用例全部外置在 `tests/cases/<name>/`，由 `tests/cases/order.txt` 控制执行顺序。`docs/recursive_functions_inventory.txt` 记录了 `src/*.cc` 中每个递归函数是否已有对应 testcase。
### CLI 选项

```bash
./cps-transpiler input.cc --explain --                    # 打印规则选择
./cps-transpiler input.cc --rule GenericStackRule --      # 强制使用某条规则
./cps-transpiler input.cc --function fib --               # 只转换指定函数
./cps-transpiler input.cc -o output.cc --                 # 输出到文件
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

## Showcase 2：嵌套递归 McCarthy 91 + 三路相互递归 mod3

输入：嵌套递归与多路相互递归

```cpp
// McCarthy 91：著名嵌套递归
int mc91(int n) {
  if (n > 100) return n - 10;
  return mc91(mc91(n + 11));
}

// mod3 系统：三路相互递归
int mod0(int n) {
  if (n == 0) return 1;
  return mod2(n - 1);
}
int mod1(int n) {
  if (n == 0) return 0;
  return mod0(n - 1);
}
int mod2(int n) {
  if (n == 0) return 0;
  return mod1(n - 1);
}
```

输出特点：
- `mc91` 被转换为 defunctionalized continuation 形式，用 `std::vector<mc91Frame>` 模拟嵌套调用
- `mod0/mod1/mod2` 被识别为同一个相互递归组，生成共享的 `enum mod0MutualTag` + dispatcher

完整文件见 [`tests/test_input_showcase2.cc`](tests/test_input_showcase2.cc)。

---

## Showcase 3：非尾调用相互递归

输入：相互递归且结果需要后处理

```cpp
int f(int n);
int g(int n) {
  if (n == 0) return 0;
  return f(n - 1);
}
int f(int n) {
  if (n == 0) return 1;
  return 1 + g(n - 1);
}
```

输出特点：
- 由于 `f` 的递归调用不是尾调用，生成**通用栈 dispatcher**。
- `g` 是尾调用成员，dispatcher 会跳过 Marker 直接压入 `f` 的帧；只有 `f` 才会压入 Marker 并在返回后执行 `1 + v0` 合并。

完整文件见 [`tests/test_input_mutual_nontail.cc`](tests/test_input_mutual_nontail.cc)。

---

## 支持的转换规则

转换器内部维护一条**有序规则链**。对同一个递归函数，按下面的顺序匹配，第一个适用的规则获胜。

| 优先级 | 规则 | 适用场景 | 输出形式 | 示例 |
|--------|------|----------|----------|------|
| 1 | **TailRecursionRule** | 所有递归调用都处于尾部位置 | `while (1)` + 参数重赋值 | `clamp_down(n-1)` |
| 2 | **AccumulatorRule** | 单递归调用与可结合运算组合 | `while` + 累加器/累乘器 | `n * fact(n-1)` |
| 3 | **UnfoldRule** | 构造型递归（anamorphism）：递归调用出现在局部变量初始化里，结果逐层构建 | 下行收集参数 + 上行回放构建语句 | `auto r = f(n-1); r.push_back(n); return r;` |
| 4 | **TuplingRule** | k 阶线性齐次递推 | O(n) 数组/元组循环 | `fib(n)=fib(n-1)+fib(n-2)` |
| 5 | **MemoizationRule** | 含重叠子问题的 k 阶线性递推 | O(n) 一维 DP 表 | `f(n)=f(n-1)+2*f(n-2)+1` |
| 6 | **MultiDimMemoRule** | 多维常数偏移递推（允许 min/max 与不变传递参数） | 多维 DP 表 + 嵌套循环 | `f(i,j)=max(f(i-1,j), f(i,j-1))` |
| 7 | **BinaryStackRule** | 两个递归调用直接由 `+` `*` `\|` `^` 连接（`&&`/`\|\|` 因短路语义不在这里处理） | 显式帧栈 | `fib(n-1)+fib(n-2)` |
| 8 | **TreeFoldRule** | 树 catamorphism/paramorphism：在节点的 `->member` 上递归并组合结果 | 后序遍历双栈（无 marker） | `t->val + f(t->left) + f(t->right)` |
| 9 | **GenericStackRule** | 任意直接递归表达式 | `enum Tag` 显式栈 + 值栈 | `min(f(n-1), f(n-2))` |
| 10 | **TreeTraversalRule** | 树遍历：循环迭代子节点并在每个子节点上递归；支持 boolean any/all 与指针 find-first | 显式节点栈 | `for (child : node->children()) if (f(child)) return true;` |
| 11 | **DefunctionalizedRule** | 兜底：单递归调用或嵌套递归表达式 | enum + switch + 帧栈 | `double_it(fact(n-1))` |

规则引擎现在采用**代价选择**：对每个函数，先收集所有适用的规则，再按预估计的运行时代价（O(n) 规则优先于栈展开规则）选出最优者。这保证 TuplingRule / MemoizationRule 不会输给 BinaryStackRule / GenericStackRule，AccumulatorRule 不会输给 GenericStackRule。

可以通过 `--explain` 查看每个函数最终使用了哪条规则，通过 `--rule=<name>` 强制使用某条规则。

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

### 6. 非齐次递推 → Memoization DP

输入：

```cpp
int memo_weird(int n) {
  if (n <= 1) return n;
  return memo_weird(n - 1) + 2 * memo_weird(n - 2) + 1;
}
```

输出：

```cpp
#include <vector>

int memo_weird(int n) {
  if (n <= 1) {
    if (n <= 1) return n;
    return 0;
  }
  std::vector<int> dp(n + 1);
  dp[0] = 0;
  dp[1] = 1;
  for (int i = 2; i <= n; ++i) {
    dp[i] = ((dp[i - 1] + (2 * dp[i - 2])) + 1);
  }
  return dp[n];
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
[按代价选择转换规则]           │
  1. TailRecursionRule       │
  2. AccumulatorRule         │
  3. UnfoldRule              │
  4. TuplingRule             │
  5. MemoizationRule         │
  6. MultiDimMemoRule        │
  7. BinaryStackRule         │
  8. TreeFoldRule            │
  9. TreeTraversalRule       │
  10. GenericStackRule       │
  11. DefunctionalizedRule ◄─┘
     |
     v
[代码生成] 构建输出 IR，打印为迭代 C++ 代码
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
  virtual CpsResult apply(const FunctionDecl*, const BodyAnalysis&, GenContext&) = 0;
};
```

`applies` 负责**模式识别**，`apply` 负责**代码生成**，返回 `CpsResult`（`std::string` 或 `CpsError`），失败时携带具体的错误码和消息而不是空字符串。新增一种转换模式只需要：新增一个规则类，在 `RuleCatalog` 中登记名称/代价，并把它加入默认规则序列。

原来集中在 `StructuralRecursionRule` 里的 8 个手写状态机形状（`IsInTailPosition`、`EvalCondition`、`ParseLinearTerms` 等）已拆分为 `transformation_rule_structural_subrules.cc` 中的独立规则，每个规则只负责一个形状。

#### TailRecursionRule

收集函数体中所有直接递归调用，如果每个调用都位于尾部位置（即某个分支最终 `return` 的值），则生成 `while (1)` 循环，在循环末尾用 `next_<param>` 更新参数。

#### AccumulatorRule

识别如下形式：

```cpp
return f(new_args) op step;   // op 为 + - * | ^
return step op f(new_args);
return min(f(new_args), step);
return max(f(new_args), step);
```

其中 `step` 不依赖递归结果。生成 `while` 循环保存中间结果；变量名根据运算选择（`sum`、`diff`、`product`、`bits`、`xors`、`min_val`、`max_val`）。

`leading` 语句只执行一次，放在循环之前。

支持多个 base case，但要求它们的返回值相同且不依赖参数（从而可作为累加器的 identity）。循环条件是所有 base case 都不满足：

```cpp
while (!(cond1) && !(cond2) && ...) { ... }
```

#### UnfoldRule

识别构造型递归（anamorphism 半区）：

```cpp
RetType f(int n) {
  if (n <= 0) return Seed();
  auto r = f(n - 1);      // 递归调用藏在局部变量初始化里
  r.push_back(n);         // 任意多条后处理语句
  return r;
}
```

此前这种形状没有任何规则能处理（直接报 NoApplicableRule）。转换采用两阶段：先沿递归参数**下行**把每层参数压入路径栈，命中 base case 得到 seed；再**上行**逐层弹出参数并原样回放后处理语句。参数更新复用尾递归的 `next_<param>` 模式；只允许 `p`（不变传递）或 `p - 正常数` 的实参形式。

#### TuplingRule

识别 k 阶线性递推：

```cpp
f(n) = c1 * f(n-1) + c2 * f(n-2) + ... + ck * f(n-k)
```

当前要求系数 `ci ∈ {+1, -1}`，且每个阶 `1..k` 恰好出现一次。生成 `std::array<int, k>` 保存最近 k 个值，从 `i = k` 线性递推到 `n`，时间复杂度 O(n)、空间复杂度 O(k)。

#### MemoizationRule

识别含重叠子问题的 k 阶线性递推：

```cpp
f(n) = c1 * f(n-1) + c2 * f(n-2) + ... + ck * f(n-k) + const
```

系数可以是任意小整数，允许常数项。要求所有递归调用都是 `f(n - c)` 形式（c 为正整数），且 base cases 覆盖 `0..max(c)-1`。生成自底向上的 `std::vector<RetType> dp(n+1)`，时间复杂度 O(n)、空间复杂度 O(n)。该规则是 TuplingRule 的更通用版本，用于非齐次或系数非 ±1 的递推。

#### MultiDimMemoRule

MemoizationRule 的多维推广（histomorphism 推广到多维 course-of-values）。识别：

```cpp
f(i, j) = combine(f(i - c1, j), f(i, j - c2), ...)
```

- 每个递归调用的实参按位置归类：**index 参数**（整型，实参为 `p` 或 `p - 正常数`）或 **pass-through 参数**（原样传递，如 LCS 的两个序列）。每个调用至少一个维度的 offset > 0，保证字典序下降。
- 允许 `min`/`max`/`std::min`/`std::max` 组合多个子结果（LCS、编辑距离的关键形状）。
- base case 必须覆盖每个 index 维度的边界层 `0..maxOff-1`（用三值条件求值逐维静态检查）。

生成多维嵌套 `std::vector` DP 表 + 升序嵌套循环；每个 cell 先跑 base-case 链再算递推式，时间复杂度 O(各维度乘积)。

#### BinaryStackRule

当递归表达式恰好是 `f(a) op f(b)`，且 `op` 为 `+` `*` `|` `^` 时，生成一个只存储参数的帧栈。利用该运算符的结合性，在 DFS 遍历子树时直接累加 `result`，无需额外的值栈。优点是代码简洁、开销小。

#### TreeFoldRule

识别树上的 catamorphism / paramorphism：

```cpp
int sum(Tree *t) {
  if (!t) return 0;
  return t->val + sum(t->left) + sum(t->right);
}
```

要求：单指针参数，每个递归调用的实参都是该参数的 `->member` 链（如 `t->left`、`t->right`），组合表达式纯净（允许 min/max）。组合表达式除了递归结果外还可以直接引用子节点本身（如 `t->left->val`）——这就是 paramorphism 与 catamorphism 的差别，对本规则的代码生成来说是免费的。

生成后序遍历双栈：帧只含节点指针 + `expanded` 标志，不需要 GenericStackRule 的 marker/count 机器，输出显著更短。

#### GenericStackRule

处理任意直接递归表达式。算法核心是“双栈 DFS”：

- **工作栈** `stack<StackEntry>`：每个 entry 带一个 `enum Tag { Frame, Marker }` 标签，Marker 记录需要弹出多少个递归结果进行合并。
- **值栈** `values`：存储已经计算好的子结果。
- **局部变量捕获**：middle statements 中声明并被递归表达式引用的局部变量（如 `int s = 0; for (...) s += i; return f(n-1) + s;`）会被自动保存到栈帧，保证合并结果时变量仍然可用。

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

所有规则共享一套代码生成基础设施与 AST 辅助函数：

- 输出 IR（`output_ir.h/.cc`）：生成的 C++ 代码先构建为轻量 IR（`IRUnit` / `IRBlock` / `IRIf` / `IRFor` / `IRSwitch` 等节点，表达式仍为字符串），再由 `PrintGeneratedUnit` 统一打印。`IRBuilder` 提供 `if_` / `ifChain` / `for_` / `switch_` 等构造辅助；未建模的片段走 `IRRaw` 逃生舱。include 会在打印时自动提升到文件顶部并去重。
- `StackMachineCodegen`（`stack_machine_codegen.h/.cc`）：显式栈样板（帧结构体、entry/tag 枚举、弹栈循环、marker/frame 分支、帧 unpack）的统一生成器，产出 IR 节点。
- `PrintExpr` / `PrintExprWithReplacements`（`cps_generator.cc`）：把 Clang AST 表达式打印回 C++ 源码，后者支持把指定子表达式替换为变量名。
- `transformation_rules_helpers` 提供的共享辅助函数：
  - `CollectHoles`：在表达式中收集所有直接递归调用，不进入递归调用自身的参数。
  - `IsInTailPosition` / `ContainsRecursiveCall` / `ExprUsesParams` / `IsPureExpr` 等：供规则与生成器统一使用的 AST 分析工具。
  - AST 谓词（`IsCallTo`、`ContainsCallTo`、`AnyArgMatches`、`ContainsDeclRefNamed` 等）：直接基于 Clang AST 判断表达式形状，替代早期脆弱的 `PrintExpr` + 字符串查找。
  - `ContainsWholeWord` / `ReplaceWholeWord`：生成代码时使用的整词标识符查找/替换工具。
  - 代码生成 helper（`EmitTailRecParamUpdate`、`EmitStmtsToIR`、`EmitTargetedUnpacks` 等）：把尾递归参数更新、用户语句重放、帧字段 unpack 等通用模式集中在一处。
- `BuildFunctionSignature`（`cps_generator.cc`）：生成函数签名。

---

## 项目结构

```
cps/
├── examples/
│   └── cps.cc                # 手写 CPS 参考实现（历史遗留）
├── CMakeLists.txt            # 构建配置，提供 check/install 目标
├── README.md
├── run_tests.py              # 自动化回归测试（用例从 tests/cases/order.txt 加载）
├── cps_testlib.py            # 测试流水线共享库（transpile/compile/run）
├── scripts/                  # 辅助脚本
│   └── inventory_recursive.py    # 生成源码递归函数清单
├── docs/                     # 项目文档
│   └── recursive_functions_inventory.txt  # src/*.cc 递归函数与 testcase 覆盖情况
├── benchmarks/               # 性能对比脚本
├── tests/
│   ├── test_input_*.cc       # 测试输入
│   ├── example_output_*.cc   # 期望输出样例
│   └── fuzz_regressions.py   # 随机回归 fuzzing
└── src/
    ├── main.cc                       # Clang Tooling 前端
    ├── cps_generator.h/.cc           # 代码生成入口与共享 helper
    ├── cps_generator_print.cc        # AST 打印（PrintExpr / PrintStmt 等）
    ├── cps_generator_analyze.cc      # AnalyzeBody：函数体归一化
    ├── cps_generator_mutual.cc       # 相互递归代码生成
    ├── cps_result.h                  # CpsResult / CpsError 结构化错误类型
    ├── output_ir.h/.cc               # 生成代码的轻量 IR 与打印机（IRBuilder / PrintGeneratedUnit）
    ├── stack_machine_codegen.h/.cc   # 显式栈样板统一生成器（产出 IR 节点）
    ├── transformation_rule.h         # BodyAnalysis、GenContext、规则接口
    ├── transformation_rules.h/.cc    # CreateDefaultRules() / RuleCatalog（规则注册表）
    ├── transformation_rules_helpers.h/.cc  # 规则与生成器共享的 AST 辅助函数与代码生成 helper
    ├── transformation_rule_tail.cc   # TailRecursionRule
    ├── transformation_rule_acc.cc    # AccumulatorRule
    ├── transformation_rule_unfold.cc  # UnfoldRule（构造型递归）
    ├── transformation_rule_tupling.cc
    ├── transformation_rule_memo.cc
    ├── transformation_rule_multimemo.cc  # MultiDimMemoRule（多维记忆化 DP）
    ├── transformation_rule_binary.cc
    ├── transformation_rule_treefold.cc  # TreeFoldRule（树 catamorphism/paramorphism）
    ├── transformation_rule_generic.cc
    ├── transformation_rule_tree.cc    # TreeTraversalRule（树遍历递归，含 boolean any/all 与指针 find-first）
    ├── transformation_rule_string.cc  # StringStructuralRecursionRule
    ├── transformation_rule_structural_subrules.cc  # 独立的 StructuralRecursion 子规则
    └── transformation_rule_defun.cc
```

---

## 已支持 & 限制

### ✅ 已支持

- 单参数 / 多参数函数，返回基本类型（`int`、`long long`、`unsigned`、`bool`、`void` 等）
- 参数类型为值、指针、引用
- 函数体形式：`[leading] (if-return | switch-return)* [middle] return recursive-expr;`，也支持 `return cond ? base : rec;`
- guard early return（如 `if (n < 0) return 0;`）会随 base case 一起处理
- middle statements 支持 `for` / `while` / do-while` 循环；循环体中声明的局部变量若被递归表达式引用，会被 GenericStackRule 自动捕获到栈帧
- 递归调用可嵌套在**任意表达式**中：二元/一元运算符、函数调用、条件表达式、数组下标等
- 双边递归、单边递归、纯尾递归
- 多个 base case（if-else-if 链或 switch case），包括 accumulator 规则下的多 base case
- 常见可结合运算的 accumulator 转换：`+`、`*`、`|`、`^`、`min`、`max`
- k 阶线性递推的 tupling 转换
- 多维常数偏移递推的记忆化 DP（MultiDimMemoRule）：网格路径、LCS、编辑距离等，允许 min/max 与不变传递参数
- 构造型递归（UnfoldRule）：`auto r = f(n-1); r.push_back(...); return r;` 等逐层构建形状
- 树 catamorphism / paramorphism（TreeFoldRule）：在 `->member` 链上递归类组合结果，组合式可直接引用子节点
- 嵌套递归（递归调用的参数仍是递归调用）
- 相互递归（相同签名的函数组；全尾调用组走枚举 dispatcher，混合组走通用栈 dispatcher 并对尾调用成员做零 Marker 优化）
- 副作用纯度分析：含副作用的表达式自动降级到保持求值顺序的显式栈规则

### 🚧 限制

- 只支持**直接递归**与**相互递归**；不支持函数指针、虚函数等动态分发递归
- TuplingRule 目前仅支持系数为 `±1` 的线性递推
- 相互递归要求函数签名相同
- 更复杂的控制流（循环、异常、goto）不支持
- 生成的代码风格偏向机械翻译，可读性仍有提升空间

---

## 参考

- 手写参考：[examples/cps.cc](examples/cps.cc)
- [Continuation Passing Style - Wikipedia](https://en.wikipedia.org/wiki/Continuation-passing_style)
- [Clang LibTooling](https://clang.llvm.org/docs/LibTooling.html)
- [Recursion Schemes - Wikipedia](https://en.wikipedia.org/wiki/Recursion_schemes)
