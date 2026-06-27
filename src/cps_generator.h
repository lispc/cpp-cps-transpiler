#ifndef CPS_GENERATOR_H
#define CPS_GENERATOR_H

#include "clang/AST/AST.h"
#include <string>

namespace cps {

// Generate iterative code for a recursive function.
// Returns the complete generated C++ code block as a string.
// If ForceRule is non-empty, only that rule is considered.
std::string GenerateCPS(const clang::FunctionDecl *FD,
                        const std::string &ForceRule = "",
                        bool ExplainSelection = false);

// Generate iterative code for a group of mutually recursive functions.
// Currently supports tail-recursive mutual recursion with identical signatures.
std::string GenerateMutualCPS(
    const std::vector<const clang::FunctionDecl *> &FDs);

} // namespace cps

#endif
