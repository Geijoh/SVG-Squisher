#include "svg_traversal.h"

#include <optional>
#include <string>
#include <vector>

#include "svg_computed_style.h"
#include "svg_dom.h"
#include "svg_path.h"
#include "svg_shape.h"
#include "svg_stroke.h"
#include "svg_text.h"
#include "svg_transform.h"
#include "svg_util.h"

namespace svg_squisher {
namespace {

void collect_text_node(const pugi::xml_node& node,
                       const pugi::xml_node& svg_root,
                       const std::vector<CssRule>& rules,
                       const StyleState& inherited,
                       const std::string& parent_transform,
                       const std::optional<std::string>& fallback_font_path,
                       std::vector<PathEntry>& out_paths,
                       TextCursor& cursor) {
  if (node.type() != pugi::node_element) return;

  const StyleState style = resolve_style(node, rules, inherited);
  const ComputedStyle computed = compute_style(style);
  const std::string transform = combine_transform(parent_transform, node.attribute("transform").as_string());

  double x = cursor.has_x ? cursor.x : first_coord_value(node, "x", 0.0);
  double y = cursor.has_y ? cursor.y : first_coord_value(node, "y", 0.0);
  if (node.attribute("x")) x = first_coord_value(node, "x", x);
  if (node.attribute("y")) y = first_coord_value(node, "y", y);

  x += first_coord_value(node, "dx", 0.0);
  y += first_coord_value(node, "dy", 0.0);

  cursor.x = x;
  cursor.y = y;
  cursor.has_x = true;
  cursor.has_y = true;

  const std::string text = collect_direct_text(node);
  const std::optional<std::string> text_font_path = resolve_text_font_path(style, fallback_font_path);
  if (!text.empty() && text_font_path.has_value()) {
    const double font_size = computed.font_size;
    const double letter_spacing = computed.letter_spacing;
    std::vector<double> x_list = coord_values(node, "x");
    std::vector<double> y_list = coord_values(node, "y");
    const std::vector<double> dx_list = coord_values(node, "dx");
    const std::vector<double> dy_list = coord_values(node, "dy");
    double text_x = x;
    if (computed.text_anchor == TextAnchorMode::Middle || computed.text_anchor == TextAnchorMode::End) {
      const double advance = measure_text_advance(text, font_size, *text_font_path, letter_spacing);
      if (computed.text_anchor == TextAnchorMode::Middle) {
        text_x -= advance / 2.0;
      } else {
        text_x -= advance;
      }
    }
    if (!x_list.empty()) {
      x_list.front() = text_x;
    }
    const TextLayoutResult text_layout = text_to_path(
      text,
      text_x,
      y,
      font_size,
      *text_font_path,
      letter_spacing,
      x_list,
      y_list,
      dx_list,
      dy_list);
    if (!text_layout.d.empty()) {
      append_path_entry(
        out_paths,
        text_layout.d,
        transform,
        style.fill,
        style.stroke,
        style,
        computed.has_fill,
        false);
    }
    cursor.x = text_layout.end_x;
    cursor.y = text_layout.end_y;
  }

  for (pugi::xml_node child : node.children()) {
    if (child.type() == pugi::node_element) {
      const std::string child_name = child.name();
      if (child_name == "tspan" || child_name == "textPath") {
        collect_text_node(child, svg_root, rules, style, transform, fallback_font_path, out_paths, cursor);
      }
    }
  }
}

void collect_paths(const pugi::xml_node& node,
                   const pugi::xml_node& svg_root,
                   const std::vector<CssRule>& rules,
                   const StyleState& inherited,
                   const std::string& parent_transform,
                   const std::optional<std::string>& font_path,
                   std::vector<PathEntry>& out_paths) {
  if (node.type() != pugi::node_element) return;

  const std::string name = node.name();
  if (should_skip_tag(name)) return;

  const StyleState style = resolve_style(node, rules, inherited);
  const ComputedStyle computed = compute_style(style);
  const std::string transform = combine_transform(parent_transform, node.attribute("transform").as_string());

  if (name == "use") {
    std::string href = node.attribute("href").as_string();
    if (href.empty()) href = node.attribute("xlink:href").as_string();
    if (!href.empty() && href[0] == '#') {
      const auto target = find_by_id(svg_root, href.substr(1));
      if (target) {
        std::string use_transform = transform;
        const double x = attr_double(node, "x", 0.0);
        const double y = attr_double(node, "y", 0.0);
        if (x != 0.0 || y != 0.0) {
          use_transform = combine_transform(use_transform, "translate(" + fmt(x) + " " + fmt(y) + ")");
        }
        collect_paths(*target, svg_root, rules, style, use_transform, font_path, out_paths);
      }
    }
    return;
  }

  if (name == "text" || name == "tspan") {
    TextCursor cursor;
    cursor.x = first_coord_value(node, "x", parse_double_string(inherited_attr(node, "x").value_or("0"), 0.0));
    cursor.y = first_coord_value(node, "y", parse_double_string(inherited_attr(node, "y").value_or("0"), 0.0));
    cursor.has_x = node.attribute("x") || inherited_attr(node, "x").has_value();
    cursor.has_y = node.attribute("y") || inherited_attr(node, "y").has_value();
    collect_text_node(node, svg_root, rules, inherited, parent_transform, font_path, out_paths, cursor);
    return;
  }

  if ((name == "circle" || name == "ellipse") && computed.has_stroke && computed.stroke_width > 0.0) {
    if (computed.has_fill) {
      append_path_entry(
        out_paths,
        name == "circle" ? circle_to_path(node) : ellipse_to_path(node),
        transform,
        style.fill,
        style.stroke,
        style,
        true,
        false);
    }

    append_path_entry(
      out_paths,
      name == "circle" ? circle_stroke_to_ring(node, computed.stroke_width)
                       : ellipse_stroke_to_ring(node, computed.stroke_width),
      transform,
      style.stroke,
      style.stroke,
      style,
      true,
      false);
  } else {
    const std::string d = node_to_path(node);

    if (!d.empty()) {
      const bool local_fill_specified = node_has_local_property(node, rules, "fill");
      const bool has_curve_segments = path_has_curve_segments(d);
      const bool keep_live_roundcap_stroke =
        computed.has_stroke &&
        !path_is_closed(d) &&
        computed.stroke_linecap == StrokeLineCap::Round;
      const bool keep_live_dashed_stroke = computed.has_stroke && computed.has_dash_pattern;
      bool emit_fill = computed.has_fill;
      bool emit_stroke = computed.has_stroke;
      if (name == "line") {
        emit_fill = false;
      }
      if (emit_fill && emit_stroke && !path_is_closed(d) && !local_fill_specified) {
        emit_fill = false;
      }
      const bool should_outline_stroke =
        emit_stroke && !keep_live_roundcap_stroke && !keep_live_dashed_stroke;
      const std::string final_stroke_outline =
        should_outline_stroke
          ? (has_curve_segments
              ? build_curve_fallback_outline(
                  d,
                  computed.stroke_width,
                  to_string(computed.stroke_linecap),
                  to_string(computed.stroke_linejoin),
                  computed.stroke_miterlimit)
              : build_straight_stroke_outline(
                  d,
                  computed.stroke_width,
                  to_string(computed.stroke_linecap),
                  to_string(computed.stroke_linejoin),
                  computed.stroke_miterlimit))
          : "";

      append_path_entry(out_paths, d, transform, style.fill, style.stroke, style, emit_fill, final_stroke_outline.empty() ? emit_stroke : false);
      if (!final_stroke_outline.empty()) {
        append_path_entry(out_paths, final_stroke_outline, transform, style.stroke, style.stroke, style, true, false);
      }
    }
  }

  for (pugi::xml_node child : node.children()) {
    collect_paths(child, svg_root, rules, style, transform, font_path, out_paths);
  }
}

}  // namespace

void collect_paths_from_svg(const pugi::xml_node& svg_node,
                            const std::vector<CssRule>& rules,
                            const StyleState& root_style,
                            const std::optional<std::string>& font_path,
                            std::vector<PathEntry>& out_paths) {
  for (pugi::xml_node child : svg_node.children()) {
    collect_paths(child, svg_node, rules, root_style, "", font_path, out_paths);
  }
}

}  // namespace svg_squisher

