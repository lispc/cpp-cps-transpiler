#ifndef CPS_GENERATOR_H
#define CPS_GENERATOR_H

#include "clang/AST/AST.h"
#include <string>

namespace cps {

// Generate iterative code for a recursive function.
// Returns the complete generated C++ code block as a string.
std::string GenerateCPS(const clang::FunctionDecl *FD);

} // namespace cps

#endif
