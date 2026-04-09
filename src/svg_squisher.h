#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace svg_squisher {

struct Options {
  std::optional<std::string> fill_override;
  std::optional<std::string> font_path;
};

struct CssRule {
  std::string selector;
  std::vector<std::pair<std::string, std::string>> declarations;
};

struct PathEntry {
  std::string d;
  std::string transform;
  std::string fill;
  std::string stroke;
  std::string stroke_width;
  std::string stroke_dasharray;
  std::string stroke_linecap;
  std::string stroke_linejoin;
  std::string stroke_miterlimit;
  std::string fill_rule;
  std::string opacity;
  bool emit_fill = false;
  bool emit_stroke = false;
};

class SvgSquisher {
public:
  std::string squish_string(const std::string& svg_text, const Options& options = {}) const;
  void squish_file(const std::filesystem::path& input_path,
                   const std::filesystem::path& output_path,
                   const Options& options = {}) const;
  void squish_directory(const std::filesystem::path& input_dir,
                        const std::filesystem::path& output_dir,
                        const Options& options = {}) const;
};

}  // namespace svg_squisher

