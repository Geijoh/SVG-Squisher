#include "svg_util.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace svg_squisher_cpp {

std::string trim(std::string value) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::vector<std::string> split(const std::string& text, char delim) {
  std::vector<std::string> parts;
  std::stringstream ss(text);
  std::string item;
  while (std::getline(ss, item, delim)) {
    parts.push_back(item);
  }
  return parts;
}

std::vector<double> parse_number_list(const std::string& text) {
  std::vector<double> values;
  std::string cleaned = text;
  std::replace(cleaned.begin(), cleaned.end(), ',', ' ');
  std::stringstream ss(cleaned);
  double value = 0.0;
  while (ss >> value) values.push_back(value);
  return values;
}

std::string fmt(double value) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(4) << value;
  std::string s = out.str();
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s.empty() || s == "-0") s = "0";
  return s;
}

double parse_double_string(const std::string& text, double fallback) {
  const std::string trimmed = trim(text);
  if (trimmed.empty()) return fallback;
  char* end = nullptr;
  const double value = std::strtod(trimmed.c_str(), &end);
  return end == trimmed.c_str() ? fallback : value;
}

double combined_opacity(const std::string& a, const std::string& b) {
  return parse_double_string(a, 1.0) * parse_double_string(b, 1.0);
}

bool paint_is_visible(const std::string& paint, double opacity) {
  return lower_copy(trim(paint)) != "none" && opacity > 1e-6;
}

}  // namespace svg_squisher_cpp
