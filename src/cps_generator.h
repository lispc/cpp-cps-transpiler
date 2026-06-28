#ifndef CPS_GENERATOR_H
#define CPS_GENERATOR_H

#include "cps_result.h"
#include "clang/AST/AST.h"
#include <string>

namespace cps {

// Generate iterative code for a recursive function.
// Returns the complete generated C++ code block as a string, or a structured
// error if no rule applies. If ForceRule is non-empty, only that rule is
// considered.
CpsResult GenerateCPS(const clang::FunctionDecl *FD,
                      const std::string &ForceRule = "",
                      bool ExplainSelection = false);

// Generate iterative code for a group of mutually recursive functions.
// Currently supports tail-recursive mutual recursion with identical signatures.
CpsResult GenerateMutualCPS(
    const std::vector<const clang::FunctionDecl *> &FDs);

} // namespace cps

#endif
