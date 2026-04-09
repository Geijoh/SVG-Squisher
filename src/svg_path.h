#pragma once

#include <optional>
#include <string>
#include <vector>

#include "svg_squisher.h"
#include "svg_style.h"
#include "svg_transform.h"

namespace svg_squisher {

std::optional<std::string> bake_path_transform(const std::string& d, const Matrix& matrix);

bool path_is_closed(const std::string& d);

bool path_has_curve_segments(const std::string& d);

void append_path_entry(std::vector<PathEntry>& out_paths,
                       const std::string& d,
                       const std::string& transform,
                       const std::string& fill,
                       const std::string& stroke,
                       const StyleState& style,
                       bool emit_fill,
                       bool emit_stroke);

}  // namespace svg_squisher

