#include "enhance/merge.h"

#include "enhance/field.h"
#include "util/yaml_utils.h"

namespace clashwin::enhance {

YAML::Node use_merge(const YAML::Node& merge, const YAML::Node& config) {
  YAML::Node merged = config;
  YAML::Node lower = field::use_lowercase(merge);
  util::deep_merge(merged, lower);
  return merged;
}

}  // namespace clashwin::enhance
