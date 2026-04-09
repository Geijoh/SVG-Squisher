#pragma once

#include <string>

namespace svg_squisher_cpp {

std::string build_straight_stroke_outline(const std::string& d,
                                          double stroke_width,
                                          const std::string& linecap,
                                          const std::string& linejoin,
                                          double miter_limit);

std::string build_curve_fallback_outline(const std::string& d, double stroke_width);

}  // namespace svg_squisher_cpp
