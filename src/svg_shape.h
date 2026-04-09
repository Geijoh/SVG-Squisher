#pragma once

#include <string>

#include <pugixml.hpp>

namespace svg_squisher_cpp {

double attr_double(const pugi::xml_node& node, const char* name, double fallback = 0.0);
std::string rect_to_path(const pugi::xml_node& node);
std::string circle_to_path(const pugi::xml_node& node);
std::string ellipse_to_path(const pugi::xml_node& node);
std::string circle_stroke_to_ring(const pugi::xml_node& node, double stroke_width);
std::string ellipse_stroke_to_ring(const pugi::xml_node& node, double stroke_width);
std::string line_to_path(const pugi::xml_node& node);
std::string points_to_path(const std::string& points, bool close);
std::string node_to_path(const pugi::xml_node& node);

}  // namespace svg_squisher_cpp
