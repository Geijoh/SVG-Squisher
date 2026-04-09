#pragma once

#include <string>
#include <vector>

namespace svg_squisher_cpp {

std::string trim(std::string value);
std::string lower_copy(std::string value);
std::vector<std::string> split(const std::string& text, char delim);
std::vector<double> parse_number_list(const std::string& text);
std::string fmt(double value);
double parse_double_string(const std::string& text, double fallback = 0.0);
double combined_opacity(const std::string& a, const std::string& b);
bool paint_is_visible(const std::string& paint, double opacity);

}  // namespace svg_squisher_cpp
