// Direct copy of ContainsCall from src/transformation_rule_tree.cc.
// Minimal Clang-AST stubs so the transpiler can parse the file standalone.

namespace std {

template <typename T>
class vector {
public:
  static constexpr int Max = 16;
  T Items[Max];
  int Size;
  vector() : Size(0) {}
  void push_back(const T &V) { Items[Size++] = V; }
  T &back() { return Items[Size - 1]; }
  void pop_back() { --Size; }
  int size() const { return Size; }
  T *begin() { return Items; }
  T *end() { return Items + Size; }
  const T *begin() const { return Items; }
  const T *end() const { return Items + Size; }
};

} // namespace std

struct Stmt {
  virtual ~Stmt() = default;
  virtual std::vector<const Stmt *> children() const { return {}; }
};

struct CallExpr : Stmt {};

bool ContainsCall(const Stmt *Root, const CallExpr *Target) {
  if (!Root)
    return false;
  if (Root == Target)
    return true;
  for (const Stmt *Child : Root->children()) {
    if (ContainsCall(Child, Target))
      return true;
  }
  return false;
}
