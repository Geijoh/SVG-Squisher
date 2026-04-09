#include "svg_stroke.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "svg_geometry.h"
#include "svg_util.h"

namespace svg_squisher_cpp {
namespace {

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

std::string build_curve_fallback_outline(const std::string& d, double stroke_width) {
  const auto subpaths = flatten_path_subpaths(d);
  if (!subpaths) return "";

  const double half_width = stroke_width / 2.0;
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
