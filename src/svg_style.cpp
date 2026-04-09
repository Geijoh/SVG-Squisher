#include "svg_style.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "svg_util.h"

namespace svg_squisher {
namespace {

std::unordered_map<std::string, std::string> parse_style_map(const std::string& style_text) {
  std::unordered_map<std::string, std::string> result;
  for (const std::string& decl : split(style_text, ';')) {
    const std::size_t colon = decl.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = trim(decl.substr(0, colon));
    const std::string value = trim(decl.substr(colon + 1));
    if (!key.empty() && !value.empty()) {
      result[key] = value;
    }
  }
  return result;
}

bool selector_matches(const CssRule& rule, const pugi::xml_node& node) {
  const std::string selector = trim(rule.selector);
  if (selector.empty()) return false;

  if (!selector.empty() && selector[0] == '.') {
    const std::string class_name = selector.substr(1);
    const std::string classes = node.attribute("class").as_string();
    for (const std::string& token : split(classes, ' ')) {
      if (trim(token) == class_name) return true;
    }
    return false;
  }

  return selector == node.name();
}

void apply_decl(StyleState& style, const std::string& key, const std::string& value) {
  if (key == "fill") style.fill = value;
  else if (key == "fill-opacity") style.fill_opacity = value;
  else if (key == "stroke") style.stroke = value;
  else if (key == "stroke-opacity") style.stroke_opacity = value;
  else if (key == "stroke-width") style.stroke_width = value;
  else if (key == "stroke-dasharray") style.stroke_dasharray = value;
  else if (key == "stroke-linecap") style.stroke_linecap = value;
  else if (key == "stroke-linejoin") style.stroke_linejoin = value;
  else if (key == "stroke-miterlimit") style.stroke_miterlimit = value;
  else if (key == "fill-rule") style.fill_rule = value;
  else if (key == "opacity") style.opacity = value;
  else if (key == "font-size") style.font_size = value;
  else if (key == "font-family") style.font_family = value;
  else if (key == "font-weight") style.font_weight = value;
  else if (key == "font-style") style.font_style = value;
  else if (key == "text-anchor") style.text_anchor = value;
  else if (key == "letter-spacing") style.letter_spacing = value;
}

}  // namespace

std::vector<CssRule> parse_css_rules(const pugi::xml_node& svg_node) {
  std::vector<CssRule> rules;
  for (const pugi::xpath_node& style_node : svg_node.select_nodes(".//style")) {
    const pugi::xml_node style = style_node.node();
    const std::string css = style.text().as_string();
    std::size_t cursor = 0;
    while (cursor < css.size()) {
      const std::size_t open = css.find('{', cursor);
      if (open == std::string::npos) break;
      const std::size_t close = css.find('}', open + 1);
      if (close == std::string::npos) break;

      const std::string selector_text = trim(css.substr(cursor, open - cursor));
      const auto declarations_map = parse_style_map(css.substr(open + 1, close - open - 1));
      for (const std::string& selector_raw : split(selector_text, ',')) {
        CssRule rule;
        rule.selector = trim(selector_raw);
        for (const auto& [key, value] : declarations_map) {
          rule.declarations.push_back({key, value});
        }
        if (!rule.selector.empty()) {
          rules.push_back(std::move(rule));
        }
      }
      cursor = close + 1;
    }
  }
  return rules;
}

bool node_has_local_property(const pugi::xml_node& node,
                             const std::vector<CssRule>& rules,
                             const std::string& property) {
  if (node.attribute(property.c_str())) return true;
  if (node.attribute("style")) {
    const auto local_style = parse_style_map(node.attribute("style").as_string());
    if (local_style.find(property) != local_style.end()) return true;
  }

  for (const CssRule& rule : rules) {
    if (selector_matches(rule, node)) {
      for (const auto& [key, value] : rule.declarations) {
        (void)value;
        if (key == property) return true;
      }
    }
  }
  return false;
}

StyleState resolve_style(const pugi::xml_node& node,
                         const std::vector<CssRule>& rules,
                         const StyleState& inherited) {
  StyleState style = inherited;

  for (const CssRule& rule : rules) {
    if (selector_matches(rule, node)) {
      for (const auto& [key, value] : rule.declarations) {
        apply_decl(style, key, value);
      }
    }
  }

  if (node.attribute("style")) {
    for (const auto& [key, value] : parse_style_map(node.attribute("style").as_string())) {
      apply_decl(style, key, value);
    }
  }

  for (const char* attr_name : {"fill", "fill-opacity", "stroke", "stroke-opacity", "stroke-width", "stroke-dasharray", "stroke-linecap",
                                "stroke-linejoin", "stroke-miterlimit", "fill-rule", "opacity",
                                "font-size", "font-family", "font-weight", "font-style",
                                "text-anchor", "letter-spacing"}) {
    if (node.attribute(attr_name)) {
      apply_decl(style, attr_name, node.attribute(attr_name).as_string());
    }
  }

  return style;
}

}  // namespace svg_squisher

