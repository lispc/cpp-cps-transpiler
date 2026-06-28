#ifndef CPS_RESULT_H
#define CPS_RESULT_H

#include <string>
#include <variant>

namespace cps {

// ============================================================
// Structured error handling for the transpiler.
// ============================================================
//
// Many parts of the pipeline historically reported failure by returning an
// empty string. This type lets rules and the generator return a concrete error
// code + message instead, so callers can decide how to surface the problem.

enum class CpsErrorCode {
  NoApplicableRule,
  UnsupportedBodyShape,
  InternalError,
};

struct CpsError {
  CpsErrorCode Code;
  std::string Message;
  std::string FunctionName;
};

using CpsResult = std::variant<std::string, CpsError>;

inline bool IsError(const CpsResult &R) {
  return std::holds_alternative<CpsError>(R);
}

inline const std::string &GetValue(const CpsResult &R) {
  return std::get<std::string>(R);
}

inline const CpsError &GetError(const CpsResult &R) {
  return std::get<CpsError>(R);
}

inline CpsResult MakeError(CpsErrorCode Code, const std::string &Message,
                           const std::string &FunctionName = "") {
  return CpsError{Code, Message, FunctionName};
}

} // namespace cps

#endif // CPS_RESULT_H
