#include "svg_paint.h"

#include "svg_util.h"

namespace svg_squisher_cpp {

ParsedPaint parse_paint(const std::string& paint, double opacity) {
  ParsedPaint parsed;
  parsed.raw = paint;
  parsed.normalized = lower_copy(trim(paint));

  if (parsed.normalized.empty() || parsed.normalized == "none" || opacity <= 1e-6) {
    parsed.kind = PaintKind::None;
    parsed.visible = false;
    return parsed;
  }

  if (parsed.normalized.size() >= 7 && parsed.normalized.rfind("url(", 0) == 0) {
    const std::size_t hash = parsed.normalized.find('#');
    const std::size_t close = parsed.normalized.rfind(')');
    if (hash != std::string::npos && close != std::string::npos && hash + 1 < close) {
      parsed.kind = PaintKind::Url;
      parsed.url_id = parsed.normalized.substr(hash + 1, close - hash - 1);
      parsed.visible = true;
      return parsed;
    }
  }

  parsed.kind = PaintKind::Value;
  parsed.visible = true;
  return parsed;
}

bool paint_equals(const std::string& lhs, const std::string& rhs) {
  return lower_copy(trim(lhs)) == lower_copy(trim(rhs));
}

std::optional<double> paint_brightness(const std::string& paint) {
  const std::string value = lower_copy(trim(paint));
  if (value == "white") return 255.0;
  if (value == "black") return 0.0;

  std::string hex;
  if (value.size() == 4 && value[0] == '#') {
    hex = {value[1], value[1], value[2], value[2], value[3], value[3]};
  } else if (value.size() == 7 && value[0] == '#') {
    hex = value.substr(1);
  } else {
    return std::nullopt;
  }

  const int r = std::stoi(hex.substr(0, 2), nullptr, 16);
  const int g = std::stoi(hex.substr(2, 2), nullptr, 16);
  const int b = std::stoi(hex.substr(4, 2), nullptr, 16);
  return 0.299 * r + 0.587 * g + 0.114 * b;
}

}  // namespace svg_squisher_cpp
