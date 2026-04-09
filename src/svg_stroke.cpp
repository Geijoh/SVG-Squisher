#include "svg_stroke.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "svg_geometry.h"
#include "svg_util.h"

namespace svg_squisher_cpp {
namespace {

bool is_command_char(char ch) {
  switch (ch) {
    case 'M': case 'm': case 'L': case 'l': case 'H': case 'h': case 'V': case 'v':
    case 'C': case 'c': case 'S': case 's': case 'Q': case 'q': case 'T': case 't':
    case 'A': case 'a': case 'Z': case 'z':
      return true;
    default:
      return false;
  }
}

void skip_separators(const std::string& d, std::size_t& pos) {
  while (pos < d.size()) {
    const char ch = d[pos];
    if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',') {
      ++pos;
    } else {
      break;
    }
  }
}

bool parse_number_token(const std::string& d, std::size_t& pos, double& out) {
  skip_separators(d, pos);
  if (pos >= d.size()) return false;
  const char* start = d.c_str() + pos;
  char* end = nullptr;
  out = std::strtod(start, &end);
  if (end == start) return false;
  pos = static_cast<std::size_t>(end - d.c_str());
  return true;
}

struct OutlineStep {
  Point point;
  bool arc = false;
  int sweep = 0;
};

struct StrokeSegment {
  Point start;
  Point end;
  Point dir;
  Point normal;
};

std::string append_line_or_arc(std::string d, const Point& from, const Point& to, bool arc, int sweep, double radius) {
  (void)from;
  if (arc) {
    d += "A" + fmt(radius) + "," + fmt(radius) + " 0 0 " + std::to_string(sweep) + " " + fmt(to.x) + "," + fmt(to.y);
  } else {
    d += "L" + fmt(to.x) + "," + fmt(to.y);
  }
  return d;
}

std::vector<StrokeSegment> build_segments(const std::vector<Point>& points, double half_width, bool closed) {
  std::vector<StrokeSegment> segments;
  const std::size_t count = points.size();
  if (count < 2) return segments;
  const std::size_t limit = closed ? count : count - 1;
  for (std::size_t i = 0; i < limit; ++i) {
    const Point start = points[i];
    const Point end = points[(i + 1) % count];
    const Point delta = end - start;
    const double len = point_length(delta);
    if (len <= 1e-9) continue;
    const Point dir{delta.x / len, delta.y / len};
    const Point normal{(-delta.y / len) * half_width, (delta.x / len) * half_width};
    segments.push_back({start, end, dir, normal});
  }
  return segments;
}

std::string build_fallback_polyline_outline(const std::vector<Point>& points, double half_width, bool closed) {
  if (points.size() < 2) return "";

  std::vector<Point> left;
  std::vector<Point> right;
  left.reserve(points.size());
  right.reserve(points.size());

  auto segment_normal = [&](std::size_t from, std::size_t to) {
    const Point delta = points[to] - points[from];
    const double len = point_length(delta);
    if (len <= 1e-9) return Point{0.0, 0.0};
    return Point{(-delta.y / len) * half_width, (delta.x / len) * half_width};
  };

  for (std::size_t i = 0; i < points.size(); ++i) {
    Point normal{0.0, 0.0};
    if (i == 0) {
      normal = segment_normal(0, 1);
    } else if (i == points.size() - 1) {
      normal = segment_normal(points.size() - 2, points.size() - 1);
    } else {
      const Point n1 = segment_normal(i - 1, i);
      const Point n2 = segment_normal(i, i + 1);
      normal = {(n1.x + n2.x) / 2.0, (n1.y + n2.y) / 2.0};
      const double len = point_length(normal);
      if (len > 1e-9) {
        normal = {normal.x * (half_width / len), normal.y * (half_width / len)};
      } else {
        normal = n2;
      }
    }
    left.push_back(points[i] + normal);
    right.push_back(points[i] - normal);
  }

  std::string d = "M" + fmt(left.front().x) + "," + fmt(left.front().y);
  for (std::size_t i = 1; i < left.size(); ++i) {
    d += "L" + fmt(left[i].x) + "," + fmt(left[i].y);
  }

  if (closed) {
    for (std::size_t i = right.size(); i-- > 0;) {
      d += "L" + fmt(right[i].x) + "," + fmt(right[i].y);
    }
    d += "Z";
    return d;
  }

  d += "L" + fmt(right.back().x) + "," + fmt(right.back().y);
  for (std::size_t i = right.size() - 1; i-- > 0;) {
    d += "L" + fmt(right[i].x) + "," + fmt(right[i].y);
  }
  d += "Z";
  return d;
}

std::optional<Point> line_intersection(Point p1, Point d1, Point p2, Point d2) {
  const double det = d1.x * (-d2.y) - d1.y * (-d2.x);
  if (std::abs(det) <= 1e-10) return std::nullopt;
  const Point delta = p2 - p1;
  const double t = (delta.x * (-d2.y) - delta.y * (-d2.x)) / det;
  return p1 + d1 * t;
}

std::vector<OutlineStep> compute_join_steps(const std::vector<StrokeSegment>& segments,
                                            int sign,
                                            const std::string& linejoin,
                                            double half_width,
                                            double miter_limit) {
  std::vector<OutlineStep> steps;
  const std::size_t count = segments.size();
  for (std::size_t i = 0; i < count; ++i) {
    const StrokeSegment& s1 = segments[i];
    const StrokeSegment& s2 = segments[(i + 1) % count];
    const double cross = s1.dir.x * s2.dir.y - s1.dir.y * s2.dir.x;

    const Point p1{s1.end.x + sign * s1.normal.x, s1.end.y + sign * s1.normal.y};
    const Point p2{s2.start.x + sign * s2.normal.x, s2.start.y + sign * s2.normal.y};
    const bool is_outer = (sign > 0 && cross < 0) || (sign < 0 && cross > 0);

    if (is_outer && linejoin == "round") {
      steps.push_back({p1, false, 0});
      steps.push_back({p2, true, cross > 0 ? 1 : 0});
      continue;
    }

    if (is_outer && linejoin == "bevel") {
      steps.push_back({p1, false, 0});
      steps.push_back({p2, false, 0});
      continue;
    }

    if (const auto intersection = line_intersection(p1, s1.dir, p2, s2.dir)) {
      if (is_outer) {
        const Point offset = *intersection - s1.end;
        const double miter_distance = point_length(offset);
        if (miter_distance <= miter_limit * half_width) {
          steps.push_back({*intersection, false, 0});
        } else {
          steps.push_back({p1, false, 0});
          steps.push_back({p2, false, 0});
        }
      } else {
        steps.push_back({*intersection, false, 0});
      }
    } else {
      steps.push_back({p1, false, 0});
    }
  }
  return steps;
}

std::string build_closed_stroke_outline(const std::vector<Point>& points,
                                        double half_width,
                                        const std::string& linejoin,
                                        double miter_limit) {
  std::vector<Point> clean_points = points;
  if (clean_points.size() >= 2) {
    const Point first = clean_points.front();
    const Point last = clean_points.back();
    if (std::abs(first.x - last.x) < 0.01 && std::abs(first.y - last.y) < 0.01) {
      clean_points.pop_back();
    }
  }
  if (clean_points.size() < 3) return "";

  const std::vector<StrokeSegment> segments = build_segments(clean_points, half_width, true);
  if (segments.size() < 2) return "";

  const std::vector<OutlineStep> outer = compute_join_steps(segments, 1, linejoin, half_width, miter_limit);
  const std::vector<OutlineStep> inner = compute_join_steps(segments, -1, linejoin, half_width, miter_limit);
  if (outer.empty() || inner.empty()) return "";

  std::string d = "M" + fmt(outer.front().point.x) + "," + fmt(outer.front().point.y);
  for (std::size_t i = 1; i < outer.size(); ++i) {
    d = append_line_or_arc(d, outer[i - 1].point, outer[i].point, outer[i].arc, outer[i].sweep, half_width);
  }
  d += "Z";

  d += " M" + fmt(inner.back().point.x) + "," + fmt(inner.back().point.y);
  for (std::size_t i = inner.size() - 1; i-- > 0;) {
    const OutlineStep& from = inner[i + 1];
    const OutlineStep& to = inner[i];
    const bool arc = from.arc;
    const int sweep = from.sweep == 1 ? 0 : 1;
    d = append_line_or_arc(d, from.point, to.point, arc, sweep, half_width);
  }
  if (inner.front().arc) {
    d += "A" + fmt(half_width) + "," + fmt(half_width) + " 0 0 " +
         std::to_string(inner.front().sweep == 1 ? 0 : 1) + " " +
         fmt(inner.back().point.x) + "," + fmt(inner.back().point.y);
  }
  d += "Z";
  return d;
}

std::string build_open_stroke_outline(const std::vector<Point>& points,
                                      double half_width,
                                      const std::string& linejoin,
                                      const std::string& linecap,
                                      double miter_limit) {
  const std::vector<StrokeSegment> segments = build_segments(points, half_width, false);
  if (segments.empty()) return "";

  std::vector<OutlineStep> left;
  std::vector<OutlineStep> right;
  const StrokeSegment& first = segments.front();
  const StrokeSegment& last = segments.back();

  if (linecap == "square") {
    const Point extension = first.dir * (-half_width);
    left.push_back({first.start + extension + first.normal, false, 0});
    right.push_back({first.start + extension - first.normal, false, 0});
  } else {
    left.push_back({first.start + first.normal, false, 0});
    right.push_back({first.start - first.normal, false, 0});
  }

  for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
    const StrokeSegment& s1 = segments[i];
    const StrokeSegment& s2 = segments[i + 1];
    const double cross = s1.dir.x * s2.dir.y - s1.dir.y * s2.dir.x;

    for (int side_index = 0; side_index < 2; ++side_index) {
      const bool is_left = side_index == 0;
      const int sign = is_left ? 1 : -1;
      std::vector<OutlineStep>& steps = is_left ? left : right;
      const Point p1{s1.end.x + sign * s1.normal.x, s1.end.y + sign * s1.normal.y};
      const Point p2{s2.start.x + sign * s2.normal.x, s2.start.y + sign * s2.normal.y};
      const bool is_outer = (is_left && cross < 0) || (!is_left && cross > 0);

      if (is_outer && linejoin == "round") {
        steps.push_back({p1, false, 0});
        steps.push_back({p2, true, cross > 0 ? 1 : 0});
        continue;
      }

      if (is_outer && linejoin == "bevel") {
        steps.push_back({p1, false, 0});
        steps.push_back({p2, false, 0});
        continue;
      }

      if (const auto intersection = line_intersection(p1, s1.dir, p2, s2.dir)) {
        if (is_outer) {
          const Point offset = *intersection - s1.end;
          const double miter_distance = point_length(offset);
          if (miter_distance <= miter_limit * half_width) {
            steps.push_back({*intersection, false, 0});
          } else {
            steps.push_back({p1, false, 0});
            steps.push_back({p2, false, 0});
          }
        } else {
          steps.push_back({*intersection, false, 0});
        }
      } else {
        steps.push_back({p1, false, 0});
      }
    }
  }

  if (linecap == "square") {
    const Point extension = last.dir * half_width;
    left.push_back({last.end + extension + last.normal, false, 0});
    right.push_back({last.end + extension - last.normal, false, 0});
  } else {
    left.push_back({last.end + last.normal, false, 0});
    right.push_back({last.end - last.normal, false, 0});
  }

  std::string d;
  if (linecap == "round") {
    d = "M" + fmt(right.front().point.x) + "," + fmt(right.front().point.y);
    d += "A" + fmt(half_width) + "," + fmt(half_width) + " 0 0 0 " +
         fmt(left.front().point.x) + "," + fmt(left.front().point.y);
  } else {
    d = "M" + fmt(left.front().point.x) + "," + fmt(left.front().point.y);
  }

  for (std::size_t i = 1; i < left.size(); ++i) {
    d = append_line_or_arc(d, left[i - 1].point, left[i].point, left[i].arc, left[i].sweep, half_width);
  }

  if (linecap == "round") {
    d += "A" + fmt(half_width) + "," + fmt(half_width) + " 0 0 0 " +
         fmt(right.back().point.x) + "," + fmt(right.back().point.y);
  } else {
    d += "L" + fmt(right.back().point.x) + "," + fmt(right.back().point.y);
  }

  for (std::size_t i = right.size() - 1; i-- > 0;) {
    const OutlineStep& from = right[i + 1];
    const OutlineStep& to = right[i];
    d = append_line_or_arc(d, from.point, to.point, from.arc, from.sweep == 1 ? 0 : 1, half_width);
  }
  d += "Z";
  return d;
}

enum class CurveKind {
  Line,
  Quadratic,
  Cubic,
};

struct CurveSegment {
  CurveKind kind = CurveKind::Line;
  Point p0{};
  Point p1{};
  Point p2{};
  Point p3{};
};

struct CurveSubpath {
  std::vector<CurveSegment> segments;
  bool closed = false;
};

struct OffsetCurveSegment {
  CurveKind kind = CurveKind::Line;
  Point p0{};
  Point p1{};
  Point p2{};
  Point p3{};
};

Point lerp(Point a, Point b, double t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

Point normalize_or_zero(Point p) {
  const double len = point_length(p);
  if (len <= 1e-9) return {0.0, 0.0};
  return {p.x / len, p.y / len};
}

Point left_normal(Point tangent, double offset) {
  return {-tangent.y * offset, tangent.x * offset};
}

double point_line_distance(Point p, Point a, Point b) {
  const Point ab = b - a;
  const double len = point_length(ab);
  if (len <= 1e-9) return point_length(p - a);
  return std::abs((p.x - a.x) * ab.y - (p.y - a.y) * ab.x) / len;
}

Point curve_start_tangent(const CurveSegment& segment) {
  switch (segment.kind) {
    case CurveKind::Line:
      return normalize_or_zero(segment.p1 - segment.p0);
    case CurveKind::Quadratic: {
      Point tangent = normalize_or_zero(segment.p1 - segment.p0);
      if (point_length(tangent) <= 1e-9) tangent = normalize_or_zero(segment.p2 - segment.p0);
      return tangent;
    }
    case CurveKind::Cubic: {
      Point tangent = normalize_or_zero(segment.p1 - segment.p0);
      if (point_length(tangent) <= 1e-9) tangent = normalize_or_zero(segment.p2 - segment.p0);
      if (point_length(tangent) <= 1e-9) tangent = normalize_or_zero(segment.p3 - segment.p0);
      return tangent;
    }
  }
  return {0.0, 0.0};
}

Point curve_end_tangent(const CurveSegment& segment) {
  switch (segment.kind) {
    case CurveKind::Line:
      return normalize_or_zero(segment.p1 - segment.p0);
    case CurveKind::Quadratic: {
      Point tangent = normalize_or_zero(segment.p2 - segment.p1);
      if (point_length(tangent) <= 1e-9) tangent = normalize_or_zero(segment.p2 - segment.p0);
      return tangent;
    }
    case CurveKind::Cubic: {
      Point tangent = normalize_or_zero(segment.p3 - segment.p2);
      if (point_length(tangent) <= 1e-9) tangent = normalize_or_zero(segment.p3 - segment.p1);
      if (point_length(tangent) <= 1e-9) tangent = normalize_or_zero(segment.p3 - segment.p0);
      return tangent;
    }
  }
  return {0.0, 0.0};
}

double curve_flatness(const CurveSegment& segment) {
  switch (segment.kind) {
    case CurveKind::Line:
      return 0.0;
    case CurveKind::Quadratic:
      return point_line_distance(segment.p1, segment.p0, segment.p2);
    case CurveKind::Cubic:
      return std::max(point_line_distance(segment.p1, segment.p0, segment.p3),
                      point_line_distance(segment.p2, segment.p0, segment.p3));
  }
  return 0.0;
}

double tangent_turn(const CurveSegment& segment) {
  const Point start = curve_start_tangent(segment);
  const Point end = curve_end_tangent(segment);
  const double dot = std::clamp(start.x * end.x + start.y * end.y, -1.0, 1.0);
  return std::acos(dot);
}

bool should_subdivide_curve(const CurveSegment& segment, double half_width, int depth) {
  if (segment.kind == CurveKind::Line) return false;
  if (depth >= 8) return false;
  const double flatness_limit = std::max(0.1, half_width * 0.2);
  return curve_flatness(segment) > flatness_limit || tangent_turn(segment) > 0.35;
}

std::pair<CurveSegment, CurveSegment> split_quadratic(const CurveSegment& segment) {
  const Point p01 = lerp(segment.p0, segment.p1, 0.5);
  const Point p12 = lerp(segment.p1, segment.p2, 0.5);
  const Point p0112 = lerp(p01, p12, 0.5);
  return {
    CurveSegment{CurveKind::Quadratic, segment.p0, p01, p0112, {}},
    CurveSegment{CurveKind::Quadratic, p0112, p12, segment.p2, {}}
  };
}

std::pair<CurveSegment, CurveSegment> split_cubic(const CurveSegment& segment) {
  const Point p01 = lerp(segment.p0, segment.p1, 0.5);
  const Point p12 = lerp(segment.p1, segment.p2, 0.5);
  const Point p23 = lerp(segment.p2, segment.p3, 0.5);
  const Point p0112 = lerp(p01, p12, 0.5);
  const Point p1223 = lerp(p12, p23, 0.5);
  const Point mid = lerp(p0112, p1223, 0.5);
  return {
    CurveSegment{CurveKind::Cubic, segment.p0, p01, p0112, mid},
    CurveSegment{CurveKind::Cubic, mid, p1223, p23, segment.p3}
  };
}

Point offset_polygon_intersection(Point a0, Point a1, Point b0, Point b1, Point fallback) {
  const Point da = a1 - a0;
  const Point db = b1 - b0;
  if (const auto hit = line_intersection(a0, da, b0, db)) return *hit;
  return fallback;
}

OffsetCurveSegment offset_curve_segment_once(const CurveSegment& segment, double half_width, int side) {
  const double offset = half_width * static_cast<double>(side);
  if (segment.kind == CurveKind::Line) {
    const Point tangent = curve_start_tangent(segment);
    const Point normal = left_normal(tangent, offset);
    return {CurveKind::Line, segment.p0 + normal, segment.p1 + normal, {}, {}};
  }

  if (segment.kind == CurveKind::Quadratic) {
    const Point n01 = left_normal(normalize_or_zero(segment.p1 - segment.p0), offset);
    const Point n12 = left_normal(normalize_or_zero(segment.p2 - segment.p1), offset);
    const Point q0 = segment.p0 + n01;
    const Point q2 = segment.p2 + n12;
    const Point q1 = offset_polygon_intersection(
      segment.p0 + n01,
      segment.p1 + n01,
      segment.p1 + n12,
      segment.p2 + n12,
      segment.p1 + Point{(n01.x + n12.x) * 0.5, (n01.y + n12.y) * 0.5});
    return {CurveKind::Quadratic, q0, q1, q2, {}};
  }

  const Point n01 = left_normal(normalize_or_zero(segment.p1 - segment.p0), offset);
  const Point n12 = left_normal(normalize_or_zero(segment.p2 - segment.p1), offset);
  const Point n23 = left_normal(normalize_or_zero(segment.p3 - segment.p2), offset);
  const Point q0 = segment.p0 + n01;
  const Point q3 = segment.p3 + n23;
  const Point q1 = offset_polygon_intersection(
    segment.p0 + n01,
    segment.p1 + n01,
    segment.p1 + n12,
    segment.p2 + n12,
    segment.p1 + Point{(n01.x + n12.x) * 0.5, (n01.y + n12.y) * 0.5});
  const Point q2 = offset_polygon_intersection(
    segment.p1 + n12,
    segment.p2 + n12,
    segment.p2 + n23,
    segment.p3 + n23,
    segment.p2 + Point{(n12.x + n23.x) * 0.5, (n12.y + n23.y) * 0.5});
  return {CurveKind::Cubic, q0, q1, q2, q3};
}

void append_offset_curve_segments(const CurveSegment& segment,
                                  double half_width,
                                  int side,
                                  int depth,
                                  std::vector<OffsetCurveSegment>& out_segments) {
  if (should_subdivide_curve(segment, half_width, depth)) {
    if (segment.kind == CurveKind::Quadratic) {
      const auto [left, right] = split_quadratic(segment);
      append_offset_curve_segments(left, half_width, side, depth + 1, out_segments);
      append_offset_curve_segments(right, half_width, side, depth + 1, out_segments);
      return;
    }
    if (segment.kind == CurveKind::Cubic) {
      const auto [left, right] = split_cubic(segment);
      append_offset_curve_segments(left, half_width, side, depth + 1, out_segments);
      append_offset_curve_segments(right, half_width, side, depth + 1, out_segments);
      return;
    }
  }
  out_segments.push_back(offset_curve_segment_once(segment, half_width, side));
}

std::optional<std::vector<CurveSubpath>> parse_curve_subpaths(const std::string& d) {
  std::vector<CurveSubpath> subpaths;
  std::size_t pos = 0;
  char cmd = 0;
  char prev_cmd = 0;
  Point current{0.0, 0.0};
  Point subpath_start{0.0, 0.0};
  Point last_cubic_ctrl{0.0, 0.0};
  Point last_quad_ctrl{0.0, 0.0};
  bool has_last_cubic = false;
  bool has_last_quad = false;
  CurveSubpath active;

  auto flush_active = [&]() {
    if (!active.segments.empty()) {
      subpaths.push_back(active);
      active = CurveSubpath{};
    }
  };

  auto next_is_number = [&](std::size_t cursor) {
    skip_separators(d, cursor);
    if (cursor >= d.size()) return false;
    const char ch = d[cursor];
    return std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.';
  };

  while (true) {
    skip_separators(d, pos);
    if (pos >= d.size()) break;

    if (is_command_char(d[pos])) {
      cmd = d[pos++];
    } else if (cmd == 0) {
      return std::nullopt;
    }

    const bool relative = std::islower(static_cast<unsigned char>(cmd)) != 0;
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));

    if (upper == 'Z') {
      active.closed = true;
      current = subpath_start;
      flush_active();
      has_last_cubic = false;
      has_last_quad = false;
      prev_cmd = 'Z';
      continue;
    }

    while (next_is_number(pos)) {
      if (upper == 'M') {
        double x = 0.0, y = 0.0;
        if (!parse_number_token(d, pos, x) || !parse_number_token(d, pos, y)) return std::nullopt;
        if (relative) { x += current.x; y += current.y; }
        flush_active();
        current = {x, y};
        subpath_start = current;
        cmd = relative ? 'l' : 'L';
        has_last_cubic = false;
        has_last_quad = false;
        prev_cmd = 'M';
      } else if (upper == 'L') {
        double x = 0.0, y = 0.0;
        if (!parse_number_token(d, pos, x) || !parse_number_token(d, pos, y)) return std::nullopt;
        if (relative) { x += current.x; y += current.y; }
        const Point next{x, y};
        active.segments.push_back({CurveKind::Line, current, next, {}, {}});
        current = next;
        has_last_cubic = false;
        has_last_quad = false;
        prev_cmd = 'L';
      } else if (upper == 'H') {
        double x = 0.0;
        if (!parse_number_token(d, pos, x)) return std::nullopt;
        if (relative) x += current.x;
        const Point next{x, current.y};
        active.segments.push_back({CurveKind::Line, current, next, {}, {}});
        current = next;
        has_last_cubic = false;
        has_last_quad = false;
        prev_cmd = 'H';
      } else if (upper == 'V') {
        double y = 0.0;
        if (!parse_number_token(d, pos, y)) return std::nullopt;
        if (relative) y += current.y;
        const Point next{current.x, y};
        active.segments.push_back({CurveKind::Line, current, next, {}, {}});
        current = next;
        has_last_cubic = false;
        has_last_quad = false;
        prev_cmd = 'V';
      } else if (upper == 'Q') {
        double x1=0,y1=0,x=0,y=0;
        if (!parse_number_token(d,pos,x1) || !parse_number_token(d,pos,y1) ||
            !parse_number_token(d,pos,x) || !parse_number_token(d,pos,y)) return std::nullopt;
        if (relative) { x1 += current.x; y1 += current.y; x += current.x; y += current.y; }
        const Point control{x1, y1};
        const Point next{x, y};
        active.segments.push_back({CurveKind::Quadratic, current, control, next, {}});
        current = next;
        last_quad_ctrl = control;
        has_last_quad = true;
        has_last_cubic = false;
        prev_cmd = 'Q';
      } else if (upper == 'T') {
        double x=0,y=0;
        if (!parse_number_token(d,pos,x) || !parse_number_token(d,pos,y)) return std::nullopt;
        Point control = current;
        if (has_last_quad && (prev_cmd == 'Q' || prev_cmd == 'T')) {
          control = {2 * current.x - last_quad_ctrl.x, 2 * current.y - last_quad_ctrl.y};
        }
        if (relative) { x += current.x; y += current.y; }
        const Point next{x, y};
        active.segments.push_back({CurveKind::Quadratic, current, control, next, {}});
        current = next;
        last_quad_ctrl = control;
        has_last_quad = true;
        has_last_cubic = false;
        prev_cmd = 'T';
      } else if (upper == 'C') {
        double x1=0,y1=0,x2=0,y2=0,x=0,y=0;
        if (!parse_number_token(d,pos,x1) || !parse_number_token(d,pos,y1) ||
            !parse_number_token(d,pos,x2) || !parse_number_token(d,pos,y2) ||
            !parse_number_token(d,pos,x) || !parse_number_token(d,pos,y)) return std::nullopt;
        if (relative) { x1 += current.x; y1 += current.y; x2 += current.x; y2 += current.y; x += current.x; y += current.y; }
        const Point c1{x1, y1};
        const Point c2{x2, y2};
        const Point next{x, y};
        active.segments.push_back({CurveKind::Cubic, current, c1, c2, next});
        current = next;
        last_cubic_ctrl = c2;
        has_last_cubic = true;
        has_last_quad = false;
        prev_cmd = 'C';
      } else if (upper == 'S') {
        double x2=0,y2=0,x=0,y=0;
        if (!parse_number_token(d,pos,x2) || !parse_number_token(d,pos,y2) ||
            !parse_number_token(d,pos,x) || !parse_number_token(d,pos,y)) return std::nullopt;
        Point c1 = current;
        if (has_last_cubic && (prev_cmd == 'C' || prev_cmd == 'S')) {
          c1 = {2 * current.x - last_cubic_ctrl.x, 2 * current.y - last_cubic_ctrl.y};
        }
        if (relative) { x2 += current.x; y2 += current.y; x += current.x; y += current.y; }
        const Point c2{x2, y2};
        const Point next{x, y};
        active.segments.push_back({CurveKind::Cubic, current, c1, c2, next});
        current = next;
        last_cubic_ctrl = c2;
        has_last_cubic = true;
        has_last_quad = false;
        prev_cmd = 'S';
      } else if (upper == 'A') {
        return std::nullopt;
      } else {
        return std::nullopt;
      }

      skip_separators(d, pos);
      if (pos < d.size() && is_command_char(d[pos])) break;
    }
  }

  flush_active();
  return subpaths;
}

Point offset_segment_start(const OffsetCurveSegment& segment) { return segment.p0; }

Point offset_segment_end(const OffsetCurveSegment& segment) {
  switch (segment.kind) {
    case CurveKind::Line: return segment.p1;
    case CurveKind::Quadratic: return segment.p2;
    case CurveKind::Cubic: return segment.p3;
  }
  return segment.p0;
}

void append_offset_segment_forward(std::string& d, const OffsetCurveSegment& segment) {
  switch (segment.kind) {
    case CurveKind::Line:
      d += "L" + fmt(segment.p1.x) + "," + fmt(segment.p1.y);
      break;
    case CurveKind::Quadratic:
      d += "Q" + fmt(segment.p1.x) + "," + fmt(segment.p1.y) + " " +
           fmt(segment.p2.x) + "," + fmt(segment.p2.y);
      break;
    case CurveKind::Cubic:
      d += "C" + fmt(segment.p1.x) + "," + fmt(segment.p1.y) + " " +
           fmt(segment.p2.x) + "," + fmt(segment.p2.y) + " " +
           fmt(segment.p3.x) + "," + fmt(segment.p3.y);
      break;
  }
}

void append_offset_segment_reverse(std::string& d, const OffsetCurveSegment& segment) {
  switch (segment.kind) {
    case CurveKind::Line:
      d += "L" + fmt(segment.p0.x) + "," + fmt(segment.p0.y);
      break;
    case CurveKind::Quadratic:
      d += "Q" + fmt(segment.p1.x) + "," + fmt(segment.p1.y) + " " +
           fmt(segment.p0.x) + "," + fmt(segment.p0.y);
      break;
    case CurveKind::Cubic:
      d += "C" + fmt(segment.p2.x) + "," + fmt(segment.p2.y) + " " +
           fmt(segment.p1.x) + "," + fmt(segment.p1.y) + " " +
           fmt(segment.p0.x) + "," + fmt(segment.p0.y);
      break;
  }
}

void append_join(std::string& d,
                 Point from,
                 Point to,
                 Point join_point,
                 Point prev_tangent,
                 Point next_tangent,
                 int side,
                 const std::string& linejoin,
                 double half_width,
                 double miter_limit) {
  if (point_length(to - from) <= 1e-5) return;

  const double cross = prev_tangent.x * next_tangent.y - prev_tangent.y * next_tangent.x;
  const bool is_outer = (side > 0 && cross < 0) || (side < 0 && cross > 0);

  if (is_outer && linejoin == "round") {
    d = append_line_or_arc(d, from, to, true, cross > 0 ? 1 : 0, half_width);
    return;
  }

  if (is_outer && linejoin == "miter") {
    if (const auto intersection = line_intersection(from, prev_tangent, to, next_tangent)) {
      if (point_length(*intersection - join_point) <= miter_limit * half_width) {
        if (point_length(*intersection - from) > 1e-5) {
          d += "L" + fmt(intersection->x) + "," + fmt(intersection->y);
        }
      }
    }
  }

  d += "L" + fmt(to.x) + "," + fmt(to.y);
}

std::string build_open_curve_outline(const CurveSubpath& subpath,
                                     double half_width,
                                     const std::string& linecap,
                                     const std::string& linejoin,
                                     double miter_limit) {
  if (subpath.segments.empty()) return "";

  std::vector<OffsetCurveSegment> left_segments;
  std::vector<OffsetCurveSegment> right_segments;
  for (const CurveSegment& segment : subpath.segments) {
    append_offset_curve_segments(segment, half_width, 1, 0, left_segments);
    append_offset_curve_segments(segment, half_width, -1, 0, right_segments);
  }
  if (left_segments.empty() || right_segments.empty()) return "";

  const Point start_tangent = curve_start_tangent(subpath.segments.front());
  const Point end_tangent = curve_end_tangent(subpath.segments.back());
  Point left_start = offset_segment_start(left_segments.front());
  Point right_start = offset_segment_start(right_segments.front());
  Point left_end = offset_segment_end(left_segments.back());
  Point right_end = offset_segment_end(right_segments.back());

  if (linecap == "square") {
    const Point start_extension = start_tangent * (-half_width);
    const Point end_extension = end_tangent * half_width;
    left_start = left_start + start_extension;
    right_start = right_start + start_extension;
    left_end = left_end + end_extension;
    right_end = right_end + end_extension;
  }

  std::string d;
  if (linecap == "round") {
    d = "M" + fmt(right_start.x) + "," + fmt(right_start.y);
    d += "A" + fmt(half_width) + "," + fmt(half_width) + " 0 0 0 " +
         fmt(left_start.x) + "," + fmt(left_start.y);
  } else {
    d = "M" + fmt(left_start.x) + "," + fmt(left_start.y);
    if (point_length(offset_segment_start(left_segments.front()) - left_start) > 1e-5) {
      d += "L" + fmt(offset_segment_start(left_segments.front()).x) + "," + fmt(offset_segment_start(left_segments.front()).y);
    }
  }

  append_offset_segment_forward(d, left_segments.front());
  for (std::size_t i = 1; i < left_segments.size(); ++i) {
    append_join(
      d,
      offset_segment_end(left_segments[i - 1]),
      offset_segment_start(left_segments[i]),
      subpath.segments[i].p0,
      curve_end_tangent(subpath.segments[i - 1]),
      curve_start_tangent(subpath.segments[i]),
      1,
      linejoin,
      half_width,
      miter_limit);
    append_offset_segment_forward(d, left_segments[i]);
  }

  if (linecap == "round") {
    d += "A" + fmt(half_width) + "," + fmt(half_width) + " 0 0 0 " +
         fmt(right_end.x) + "," + fmt(right_end.y);
  } else {
    if (point_length(left_end - offset_segment_end(left_segments.back())) > 1e-5) {
      d += "L" + fmt(left_end.x) + "," + fmt(left_end.y);
    }
    d += "L" + fmt(right_end.x) + "," + fmt(right_end.y);
  }

  for (std::size_t i = right_segments.size(); i-- > 0;) {
    if (i + 1 < right_segments.size()) {
      append_join(
        d,
        offset_segment_start(right_segments[i + 1]),
        offset_segment_end(right_segments[i]),
        subpath.segments[i + 1].p0,
        curve_start_tangent(subpath.segments[i + 1]),
        curve_end_tangent(subpath.segments[i]),
        -1,
        linejoin,
        half_width,
        miter_limit);
    }
    append_offset_segment_reverse(d, right_segments[i]);
  }

  if (linecap != "round" && point_length(right_start - offset_segment_start(right_segments.front())) > 1e-5) {
    d += "L" + fmt(right_start.x) + "," + fmt(right_start.y);
  }
  d += "Z";
  return d;
}

}  // namespace

std::string build_straight_stroke_outline(const std::string& d,
                                          double stroke_width,
                                          const std::string& linecap,
                                          const std::string& linejoin,
                                          double miter_limit) {
  auto subpaths = parse_straight_subpaths(d);
  if (!subpaths) {
    subpaths = flatten_path_subpaths(d);
  }
  if (!subpaths) return "";

  const double half_width = stroke_width / 2.0;
  std::string combined;
  for (const StrokeSubpath& subpath : *subpaths) {
    if (subpath.points.size() < 2) continue;
    std::string part = subpath.closed
      ? build_closed_stroke_outline(subpath.points, half_width, linejoin, miter_limit)
      : build_open_stroke_outline(subpath.points, half_width, linejoin, linecap, miter_limit);
    if (part.empty()) {
      part = build_fallback_polyline_outline(subpath.points, half_width, subpath.closed);
    }
    if (!part.empty()) {
      if (!combined.empty()) combined += " ";
      combined += part;
    }
  }
  return combined;
}

std::string build_curve_fallback_outline(const std::string& d,
                                         double stroke_width,
                                         const std::string& linecap,
                                         const std::string& linejoin,
                                         double miter_limit) {
  const double half_width = stroke_width / 2.0;

  if (const auto curve_subpaths = parse_curve_subpaths(d)) {
    std::string combined;
    for (const CurveSubpath& subpath : *curve_subpaths) {
      if (subpath.segments.empty() || subpath.closed) {
        combined.clear();
        break;
      }
      const std::string part = build_open_curve_outline(subpath, half_width, linecap, linejoin, miter_limit);
      if (part.empty()) {
        combined.clear();
        break;
      }
      if (!combined.empty()) combined += " ";
      combined += part;
    }
    if (!combined.empty()) return combined;
  }

  const auto subpaths = flatten_path_subpaths(d);
  if (!subpaths) return "";

  std::string combined;
  for (const StrokeSubpath& subpath : *subpaths) {
    if (subpath.points.size() < 2) continue;
    const std::string part = build_fallback_polyline_outline(subpath.points, half_width, subpath.closed);
    if (!part.empty()) {
      if (!combined.empty()) combined += " ";
      combined += part;
    }
  }
  return combined;
}

}  // namespace svg_squisher_cpp
