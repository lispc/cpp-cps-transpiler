// Direct copy of UnwrapTrailingStmt from src/cps_generator.cc.
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
  int size() const { return Size; }
  T *begin() { return Items; }
  T *end() { return Items + Size; }
  const T *begin() const { return Items; }
  const T *end() const { return Items + Size; }
};

} // namespace std

struct Stmt {
  virtual ~Stmt() = default;
};

struct CompoundStmt : Stmt {
  std::vector<const Stmt *> Body;
  bool body_empty() const { return Body.size() == 0; }
  const std::vector<const Stmt *> &body() const { return Body; }
  void add(const Stmt *S) { Body.push_back(S); }
};

struct IfStmt : Stmt {
  const Stmt *Then_;
  const Stmt *Else_;
  IfStmt(const Stmt *T, const Stmt *E = nullptr) : Then_(T), Else_(E) {}
  const Stmt *getThen() const { return Then_; }
  const Stmt *getElse() const { return Else_; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

const Stmt *UnwrapTrailingStmt(const Stmt *S) {
  if (const CompoundStmt *CS = dyn_cast<CompoundStmt>(S)) {
    if (CS->body_empty())
      return nullptr;
    const Stmt *Last = nullptr;
    for (const Stmt *B : CS->body())
      Last = B;
    return UnwrapTrailingStmt(Last);
  }
  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    // An if-without-else is just a guarded block; the trailing statement of
    // its then-branch is what matters for base-case detection.
    if (IfS->getElse())
      return S;
    return UnwrapTrailingStmt(IfS->getThen());
  }
  return S;
}
