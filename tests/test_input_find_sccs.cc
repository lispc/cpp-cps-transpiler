// Direct copy of FindSCCs from src/main.cc.
// Minimal std:: stubs so the transpiler can parse the file standalone.

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
  static constexpr int Max = 32;
  T Items[Max];
  int Size;
  vector() : Size(0) {}
  void push_back(const T &V) { Items[Size++] = V; }
  int size() const { return Size; }
  T *begin() { return Items; }
  T *end() { return Items + Size; }
  const T *begin() const { return Items; }
  const T *end() const { return Items + Size; }
  T &back() { return Items[Size - 1]; }
  const T &back() const { return Items[Size - 1]; }
  void pop_back() { --Size; }
  T &operator[](int i) { return Items[i]; }
  const T &operator[](int i) const { return Items[i]; }
  bool empty() const { return Size == 0; }
};

template <typename T1, typename T2>
struct pair {
  T1 first;
  T2 second;
  pair() = default;
  pair(const T1 &a, const T2 &b) : first(a), second(b) {}
};

template <typename K, typename V>
class unordered_map {
public:
  static constexpr int Max = 32;
  K Keys[Max];
  V Values[Max];
  int Size;
  unordered_map() : Size(0) {}
  bool empty() const { return Size == 0; }
  int count(const K &Key) const {
    for (int i = 0; i < Size; ++i)
      if (Keys[i] == Key)
        return 1;
    return 0;
  }
  V &operator[](const K &Key) {
    for (int i = 0; i < Size; ++i)
      if (Keys[i] == Key)
        return Values[i];
    Keys[Size] = Key;
    return Values[Size++];
  }
  struct iterator {
    unordered_map *M;
    int I;
    pair<K, V> Cur;
    iterator(unordered_map *m = nullptr, int i = 0) : M(m), I(i) { refresh(); }
    void refresh() { if (M && I < M->Size) Cur = pair<K, V>(M->Keys[I], M->Values[I]); }
    bool operator!=(const iterator &O) const { return I != O.I; }
    iterator &operator++() { ++I; refresh(); return *this; }
    pair<K, V> &operator*() { return Cur; }
    pair<K, V> *operator->() { return &Cur; }
  };
  struct const_iterator {
    const unordered_map *M;
    int I;
    pair<K, V> Cur;
    const_iterator(const unordered_map *m = nullptr, int i = 0) : M(m), I(i) { refresh(); }
    void refresh() { if (M && I < M->Size) Cur = pair<K, V>(M->Keys[I], M->Values[I]); }
    bool operator!=(const const_iterator &O) const { return I != O.I; }
    const_iterator &operator++() { ++I; refresh(); return *this; }
    const pair<K, V> &operator*() const { return Cur; }
    const pair<K, V> *operator->() const { return &Cur; }
  };
  iterator begin() { return iterator{this, 0}; }
  iterator end() { return iterator{this, Size}; }
  const_iterator begin() const { return const_iterator{this, 0}; }
  const_iterator end() const { return const_iterator{this, Size}; }
  iterator find(const K &Key) {
    for (int i = 0; i < Size; ++i)
      if (Keys[i] == Key)
        return iterator{this, i};
    return end();
  }
  const_iterator find(const K &Key) const {
    for (int i = 0; i < Size; ++i)
      if (Keys[i] == Key)
        return const_iterator{this, i};
    return end();
  }
};

template <typename T>
class unordered_set {
public:
  static constexpr int Max = 32;
  T Items[Max];
  int Size;
  unordered_set() : Size(0) {}
  void insert(const T &V) { Items[Size++] = V; }
  int count(const T &V) const {
    for (int i = 0; i < Size; ++i)
      if (Items[i] == V)
        return 1;
    return 0;
  }
  void erase(const T &V) {
    for (int i = 0; i < Size; ++i) {
      if (Items[i] == V) {
        Items[i] = Items[--Size];
        return;
      }
    }
  }
  struct iterator {
    const unordered_set *S;
    int I;
    iterator(const unordered_set *s = nullptr, int i = 0) : S(s), I(i) {}
    bool operator!=(const iterator &O) const { return I != O.I; }
    iterator &operator++() { ++I; return *this; }
    const T &operator*() const { return S->Items[I]; }
    const T *operator->() const { return &S->Items[I]; }
  };
  iterator begin() const { return iterator{this, 0}; }
  iterator end() const { return iterator{this, Size}; }
};

template <typename T>
const T &min(const T &A, const T &B) {
  return A;
}

template <typename T>
const T &max(const T &A, const T &B) {
  return B;
}

template <typename Sig>
class function {
public:
  function() = default;
  template <typename F>
  function(F &&) {}
  template <typename F>
  function &operator=(F &&) { return *this; }
  template <typename... Args>
  void operator()(Args &&...) const {}
};

} // namespace std

static std::vector<std::vector<std::string>>
FindSCCs(const std::unordered_map<std::string, std::unordered_set<std::string>> &Graph) {
  std::vector<std::vector<std::string>> SCCs;
  std::unordered_map<std::string, int> Index;
  std::unordered_map<std::string, int> LowLink;
  std::unordered_set<std::string> OnStack;
  std::vector<std::string> Stack;
  int idx = 0;

  std::function<void(const std::string &)> strongconnect =
      [&](const std::string &Name) {
    Index[Name] = idx;
    LowLink[Name] = idx;
    ++idx;
    Stack.push_back(Name);
    OnStack.insert(Name);

    auto It = Graph.find(Name);
    if (It != Graph.end()) {
      for (const std::string &Next : It->second) {
        if (!Graph.count(Next))
          continue; // not in graph (not a defined function)
        if (!Index.count(Next)) {
          strongconnect(Next);
          LowLink[Name] = std::min(LowLink[Name], LowLink[Next]);
        } else if (OnStack.count(Next)) {
          LowLink[Name] = std::min(LowLink[Name], Index[Next]);
        }
      }
    }

    if (LowLink[Name] == Index[Name]) {
      std::vector<std::string> SCC;
      while (true) {
        std::string W = Stack.back();
        Stack.pop_back();
        OnStack.erase(W);
        SCC.push_back(W);
        if (W == Name) break;
      }
      SCCs.push_back(SCC);
    }
  };

  for (const auto &KV : Graph) {
    if (!Index.count(KV.first))
      strongconnect(KV.first);
  }
  return SCCs;
}
