#ifndef TRANSFORMATION_RULES_H
#define TRANSFORMATION_RULES_H

#include "transformation_rule.h"
#include <memory>
#include <vector>

namespace cps {

// Create the default ordered list of transformation rules.
// Rules are tried in order; the first one that applies wins.
std::vector<std::unique_ptr<TransformationRule>> CreateDefaultRules();

} // namespace cps

#endif // TRANSFORMATION_RULES_H
