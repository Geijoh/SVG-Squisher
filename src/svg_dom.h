#pragma once

#include <optional>
#include <string>

#include <pugixml.hpp>

namespace svg_squisher_cpp {

bool should_skip_tag(const std::string& name);

std::optional<pugi::xml_node> find_by_id(const pugi::xml_node& node, const std::string& id);

}  // namespace svg_squisher_cpp
