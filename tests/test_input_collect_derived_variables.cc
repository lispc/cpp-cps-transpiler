// Direct copy of CollectDerivedVariables from src/transformation_rule_string.cc.
// Minimal stubs for the AST / string API used by the function.

struct ASTContext {};

namespace std {

class string {
  const char *Data;
public:
  string() : Data("") {}
  string(const char *S) : Data(S) {}
  string(const string &O) = default;
  bool operator==(const string &O) const {
    const char *A = Data;
    const char *B = O.Data;
    while (*A && *B && *A == *B) {
      ++A;
      ++B;
    }
    return *A == *B;
  }
};

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
  T &operator[](int i) { return Items[i]; }
  const T &operator[](int i) const { return Items[i]; }
  T &back() { return Items[Size - 1]; }
  const T &back() const { return Items[Size - 1]; }
  bool empty() const { return Size == 0; }
};

template <typename T>
class unordered_set {
public:
  static constexpr int Max = 16;
  T Items[Max];
  int Size;
  unordered_set() : Size(0) {}
  void insert(const T &V) { Items[Size++] = V; }
  bool count(const T &V) const {
    for (int i = 0; i < Size; ++i)
      if (Items[i] == V)
        return true;
    return false;
  }
};

} // namespace std

struct Expr {
  virtual ~Expr() = default;
};

struct Decl {
  virtual ~Decl() = default;
};

struct VarDecl : Decl {
  std::string Name;
  const Expr *Init_;
  VarDecl(const std::string &N, const Expr *I = nullptr) : Name(N), Init_(I) {}
  std::string getNameAsString() const { return Name; }
  const Expr *getInit() const { return Init_; }
};

struct Stmt {
  virtual ~Stmt() = default;
  virtual std::vector<const Stmt *> children() const { return {}; }
};

struct DeclStmt : Stmt {
  std::vector<const Decl *> Decls;
  void add(const Decl *D) { Decls.push_back(D); }
  const std::vector<const Decl *> &decls() const { return Decls; }
  std::vector<const Stmt *> children() const override { return {}; }
};

struct IfStmt : Stmt {
  const VarDecl *CondVar;
  std::vector<const Stmt *> Kids;
  IfStmt(const VarDecl *CV) : CondVar(CV) {}
  const VarDecl *getConditionVariable() const { return CondVar; }
  void addChild(const Stmt *S) { Kids.push_back(S); }
  std::vector<const Stmt *> children() const override { return Kids; }
};

struct Node : Stmt {
  std::vector<const Stmt *> Kids;
  void addChild(const Stmt *S) { Kids.push_back(S); }
  std::vector<const Stmt *> children() const override { return Kids; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

bool IsSubexpressionAccess(const Expr *E, const std::string &ParamName,
                           const std::unordered_set<std::string> &Derived,
                           const ASTContext *Ctx) {
  (void)E;
  (void)ParamName;
  (void)Derived;
  (void)Ctx;
  return true;
}

void CollectDerivedVariables(const Stmt *S, const std::string &ParamName,
                             std::unordered_set<std::string> &Derived,
                             const ASTContext *Ctx) {
  if (!S)
    return;

  if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
        if (const Expr *Init = VD->getInit()) {
          if (IsSubexpressionAccess(Init, ParamName, Derived, Ctx))
            Derived.insert(VD->getNameAsString());
        }
      }
    }
  }

  if (const IfStmt *IfS = dyn_cast<IfStmt>(S)) {
    if (const VarDecl *VD = IfS->getConditionVariable()) {
      if (const Expr *Init = VD->getInit()) {
        if (IsSubexpressionAccess(Init, ParamName, Derived, Ctx))
          Derived.insert(VD->getNameAsString());
      }
    }
  }

  for (const Stmt *Child : S->children())
    CollectDerivedVariables(Child, ParamName, Derived, Ctx);
}
