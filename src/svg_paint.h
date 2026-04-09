#pragma once

#include <optional>
#include <string>

namespace svg_squisher_cpp {

enum class PaintKind {
  None,
  Url,
  Value,
};

struct ParsedPaint {
  std::string raw;
  std::string normalized;
  PaintKind kind = PaintKind::None;
  std::optional<std::string> url_id;
  bool visible = false;
};

ParsedPaint parse_paint(const std::string& paint, double opacity = 1.0);

bool paint_equals(const std::string& lhs, const std::string& rhs);

std::optional<double> paint_brightness(const std::string& paint);

}  // namespace svg_squisher_cpp
