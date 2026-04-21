#ifndef CODE_EMITTER_H
#define CODE_EMITTER_H

#include <sstream>
#include <string>
#include <functional>

namespace cps {

// Lightweight RAII code emitter with indentation management.
class CodeEmitter {
public:
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
    --indent_;
    return *this;
  }

  // Scoped block: header "class Foo : public Bar" becomes
  //   class Foo : public Bar {
  //     ...
  //   }
  // Optional suffix (e.g. ";" for struct/class definitions).
  CodeEmitter &block(const std::string &header,
                     std::function<void(CodeEmitter &)> body,
                     const std::string &suffix = "") {
    line(header + " {");
    ++indent_;
    body(*this);
    --indent_;
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
