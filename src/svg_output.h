#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <pugixml.hpp>

#include "svg_squisher.h"

namespace svg_squisher {

std::string xml_escape(const std::string& value);

std::vector<std::string> collect_serialized_defs(const pugi::xml_node& svg_root,
                                                 const std::vector<PathEntry>& paths);

std::string render_path_entry(const PathEntry& entry, const std::optional<std::string>& fill_override);

std::string render_svg_document(const pugi::xml_node& svg_root,
                                const std::vector<PathEntry>& paths,
                                const std::optional<std::string>& fill_override);

std::string read_file(const std::filesystem::path& path);

void write_file(const std::filesystem::path& path, const std::string& text);

}  // namespace svg_squisher

