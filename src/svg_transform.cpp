#include "svg_transform.h"

#include <cmath>
#include <string>

#include "svg_util.h"

namespace svg_squisher {

Matrix multiply(const Matrix& lhs, const Matrix& rhs) {
  Matrix out;
  out.a = lhs.a * rhs.a + lhs.c * rhs.b;
  out.b = lhs.b * rhs.a + lhs.d * rhs.b;
  out.c = lhs.a * rhs.c + lhs.c * rhs.d;
  out.d = lhs.b * rhs.c + lhs.d * rhs.d;
  out.e = lhs.a * rhs.e + lhs.c * rhs.f + lhs.e;
  out.f = lhs.b * rhs.e + lhs.d * rhs.f + lhs.f;
  return out;
}

Point apply_matrix(const Matrix& matrix, Point p) {
  return {
    matrix.a * p.x + matrix.c * p.y + matrix.e,
    matrix.b * p.x + matrix.d * p.y + matrix.f,
  };
}

bool matrix_is_identity(const Matrix& m, double eps) {
  return std::abs(m.a - 1.0) < eps &&
         std::abs(m.b) < eps &&
         std::abs(m.c) < eps &&
         std::abs(m.d - 1.0) < eps &&
         std::abs(m.e) < eps &&
         std::abs(m.f) < eps;
}

bool matrix_is_scale_translate_only(const Matrix& m, double eps) {
  return std::abs(m.b) < eps && std::abs(m.c) < eps;
}

Matrix parse_transform(const std::string& transform_text) {
  Matrix current;
  std::size_t pos = 0;
  while (pos < transform_text.size()) {
    while (pos < transform_text.size() && std::isspace(static_cast<unsigned char>(transform_text[pos]))) pos++;
    if (pos >= transform_text.size()) break;

    const std::size_t open = transform_text.find('(', pos);
    if (open == std::string::npos) break;
    const std::string name = trim(transform_text.substr(pos, open - pos));
    const std::size_t close = transform_text.find(')', open + 1);
    if (close == std::string::npos) break;
    const std::vector<double> args = parse_number_list(transform_text.substr(open + 1, close - open - 1));
    Matrix op;

    if (name == "matrix" && args.size() == 6) {
      op = {args[0], args[1], args[2], args[3], args[4], args[5]};
    } else if (name == "translate" && !args.empty()) {
      op.e = args[0];
      op.f = args.size() > 1 ? args[1] : 0.0;
    } else if (name == "scale" && !args.empty()) {
      op.a = args[0];
      op.d = args.size() > 1 ? args[1] : args[0];
    } else if (name == "rotate" && !args.empty()) {
      const double angle = args[0] * 3.14159265358979323846 / 180.0;
      const double cos_a = std::cos(angle);
      const double sin_a = std::sin(angle);
      Matrix rot{cos_a, sin_a, -sin_a, cos_a, 0.0, 0.0};
      if (args.size() >= 3) {
        Matrix to_origin{1, 0, 0, 1, -args[1], -args[2]};
        Matrix back{1, 0, 0, 1, args[1], args[2]};
        op = multiply(back, multiply(rot, to_origin));
      } else {
        op = rot;
      }
    } else if (name == "skewX" && !args.empty()) {
      op.c = std::tan(args[0] * 3.14159265358979323846 / 180.0);
    } else if (name == "skewY" && !args.empty()) {
      op.b = std::tan(args[0] * 3.14159265358979323846 / 180.0);
    }

    current = multiply(current, op);
    pos = close + 1;
  }
  return current;
}

std::string combine_transform(const std::string& parent, const std::string& local) {
  const std::string a = trim(parent);
  const std::string b = trim(local);
  if (a.empty()) return b;
  if (b.empty()) return a;
  return a + " " + b;
}

}  // namespace svg_squisher

