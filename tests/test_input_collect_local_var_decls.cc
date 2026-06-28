// Direct copies of CollectLocalVarDecls overloads from src/transformation_rules_helpers.cc.
// Minimal stubs for the AST API used by the functions.

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

} // namespace std

struct Decl;
struct VarDecl;
struct Stmt;

struct Decl {
  virtual ~Decl() = default;
};

struct Stmt {
  virtual ~Stmt() = default;
};

struct DeclStmt : Stmt {
  std::vector<const Decl *> Decls;
  void add(const Decl *D) { Decls.push_back(D); }
  const std::vector<const Decl *> &decls() const { return Decls; }
};

struct VarDecl : Decl {
  std::string Name;
  VarDecl(const std::string &N) : Name(N) {}
  std::string getNameAsString() const { return Name; }
};

template <typename T, typename U>
const T *dyn_cast(const U *P) { return dynamic_cast<const T *>(P); }

void CollectLocalVarDecls(const Stmt *S,
                          std::vector<const VarDecl *> &Out) {
  if (!S)
    return;
  if (const DeclStmt *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D))
        Out.push_back(VD);
    }
  }
}

void CollectLocalVarDecls(const std::vector<const Stmt *> &Stmts,
                          std::vector<const VarDecl *> &Out) {
  for (const Stmt *S : Stmts)
    CollectLocalVarDecls(S, Out);
}
