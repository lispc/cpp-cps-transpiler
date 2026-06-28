struct Stmt { virtual ~Stmt() = default; };
struct ReturnStmt : Stmt {};
struct IfStmt : Stmt {};
struct SwitchStmt : Stmt {};
struct CompoundStmt : Stmt {
  static constexpr int Max = 8;
  const Stmt *Items[Max];
  int Size;
  CompoundStmt() : Size(0) {}
  void addChild(const Stmt *S) { Items[Size++] = S; }
  int size() const { return Size; }
  const Stmt *const *body_begin() const { return Items; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

template <typename T, typename U>
bool isa(const U *P) { return dyn_cast<T>(P) != nullptr; }
