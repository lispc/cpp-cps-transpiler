#ifndef CODE_EMITTER_H
#define CODE_EMITTER_H

#include <sstream>
#include <string>
#include <utility>

namespace cps {

// Lightweight RAII code emitter with indentation management.
class CodeEmitter {
public:
  CodeEmitter() = default;
  CodeEmitter(const CodeEmitter &) = delete;
  CodeEmitter &operator=(const CodeEmitter &) = delete;
  CodeEmitter(CodeEmitter &&) = default;
  CodeEmitter &operator=(CodeEmitter &&) = default;

  CodeEmitter &line(const std::string &s) {
    os_ << std::string(indent_ * 2, ' ') << s << "\n";
    return *this;
  }

  // Emit a raw string without indent or newline.
  CodeEmitter &raw(const std::string &s) {
    os_ << s;
    return *this;
  }

  CodeEmitter &nl() {
    os_ << "\n";
    return *this;
  }

  // Increase / decrease indent level manually.
  CodeEmitter &inc() {
    ++indent_;
    return *this;
  }
  CodeEmitter &dec() {
    if (indent_ > 0)
      --indent_;
    return *this;
  }

  // RAII helper that increments indent on construction and decrements on
  // destruction.
  struct IndentScope {
    explicit IndentScope(CodeEmitter *e) : e_(e) { e_->inc(); }
    ~IndentScope() { e_->dec(); }
    IndentScope(const IndentScope &) = delete;
    IndentScope &operator=(const IndentScope &) = delete;
  private:
    CodeEmitter *e_;
  };

  // Scoped block: header "class Foo : public Bar" becomes
  //   class Foo : public Bar {
  //     ...
  //   }
  // Optional suffix (e.g. ";" for struct/class definitions).
  template <typename BodyFn>
  CodeEmitter &block(const std::string &header, BodyFn &&body,
                     const std::string &suffix = "") {
    line(header + " {");
    {
      IndentScope scope(this);
      body(*this);
    }
    line("}" + suffix);
    return *this;
  }

  std::string str() const { return os_.str(); }

private:
  std::ostringstream os_;
  int indent_ = 0;
};

} // namespace cps

#endif // CODE_EMITTER_H
