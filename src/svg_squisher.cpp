#include "svg_squisher.h"

#include <optional>
#include <stdexcept>
#include <string>

#include <pugixml.hpp>

#include "svg_output.h"
#include "svg_postprocess.h"
#include "svg_style.h"
#include "svg_text.h"
#include "svg_traversal.h"
#include "svg_util.h"

namespace fs = std::filesystem;

namespace svg_squisher_cpp {

std::string SvgSquisher::squish_string(const std::string& svg_text, const Options& options) const {
  pugi::xml_document doc;
  const pugi::xml_parse_result parse_result = doc.load_string(svg_text.c_str());
  if (!parse_result) {
    throw std::runtime_error("Failed to parse SVG XML");
  }

  const pugi::xml_node svg_node = doc.child("svg");
  if (!svg_node) {
    throw std::runtime_error("No <svg> root element found");
  }

  const auto css_rules = parse_css_rules(svg_node);
  std::vector<PathEntry> paths;
  const StyleState root_style = resolve_style(svg_node, css_rules, StyleState{});
  const std::optional<std::string> font_path =
    options.font_path.has_value() ? options.font_path : discover_default_font();

  collect_paths_from_svg(svg_node, css_rules, root_style, font_path, paths);

  const std::vector<PathEntry> final_paths = prepare_output_paths(svg_node, paths, options.fill_override);
  return render_svg_document(svg_node, final_paths, options.fill_override);
}

void SvgSquisher::squish_file(const fs::path& input_path,
                              const fs::path& output_path,
                              const Options& options) const {
  write_file(output_path, squish_string(read_file(input_path), options));
}

void SvgSquisher::squish_directory(const fs::path& input_dir,
                                   const fs::path& output_dir,
                                   const Options& options) const {
  fs::create_directories(output_dir);
  for (const fs::directory_entry& entry : fs::directory_iterator(input_dir)) {
    if (!entry.is_regular_file()) continue;
    if (lower_copy(entry.path().extension().string()) != ".svg") continue;
    squish_file(entry.path(), output_dir / entry.path().filename(), options);
  }
}

}  // namespace svg_squisher_cpp
