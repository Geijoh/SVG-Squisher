#include "svg_dom.h"

#include <algorithm>
#include <string>
#include <vector>

namespace svg_squisher {

bool should_skip_tag(const std::string& name) {
  static const std::vector<std::string> skipped = {
    "defs", "style", "script", "title", "desc", "metadata", "clipPath", "mask",
    "filter", "linearGradient", "radialGradient", "pattern", "symbol", "image",
    "foreignObject"
  };
  return std::find(skipped.begin(), skipped.end(), name) != skipped.end();
}

std::optional<pugi::xml_node> find_by_id(const pugi::xml_node& node, const std::string& id) {
  if (!node) return std::nullopt;
  if (node.attribute("id") && id == node.attribute("id").as_string()) {
    return node;
  }
  for (pugi::xml_node child : node.children()) {
    if (auto found = find_by_id(child, id)) return found;
  }
  return std::nullopt;
}

}  // namespace svg_squisher

