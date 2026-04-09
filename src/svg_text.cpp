#include "svg_text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include "svg_util.h"

namespace fs = std::filesystem;

namespace svg_squisher {
namespace {

struct FontLibrary {
  FT_Library library = nullptr;
  std::map<std::string, FT_Face> faces;

  FontLibrary() {
    if (FT_Init_FreeType(&library) != 0) {
      throw std::runtime_error("Failed to initialize FreeType");
    }
  }

  ~FontLibrary() {
    for (auto& [path, face] : faces) {
      (void)path;
      if (face) FT_Done_Face(face);
    }
    if (library) FT_Done_FreeType(library);
  }
};

std::string strip_matching_quotes(std::string value) {
  value = trim(value);
  if (value.size() >= 2) {
    const char first = value.front();
    const char last = value.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
      value = value.substr(1, value.size() - 2);
    }
  }
  return trim(value);
}

std::string normalize_font_token(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (unsigned char ch : value) {
    if (std::isalnum(ch)) {
      out.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return out;
}

bool font_weight_is_boldish(const std::string& font_weight) {
  const std::string lowered = lower_copy(trim(font_weight));
  if (lowered == "bold" || lowered == "bolder" || lowered == "semibold" || lowered == "demibold") {
    return true;
  }
  const double numeric = parse_double_string(lowered, 400.0);
  return numeric >= 600.0;
}

bool font_style_is_italicish(const std::string& font_style) {
  const std::string lowered = lower_copy(trim(font_style));
  return lowered == "italic" || lowered == "oblique";
}

std::optional<std::string> discover_font_for_family(const std::string& font_family,
                                                    const std::string& font_weight,
                                                    const std::string& font_style) {
  const std::vector<std::string> families = split(font_family, ',');
  const bool want_bold = font_weight_is_boldish(font_weight);
  const bool want_italic = font_style_is_italicish(font_style);
  static const std::unordered_map<std::string, std::vector<std::string>> known_fonts = {
    {"arial", {"C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/arialmt.ttf"}},
    {"arial_bold", {"C:/Windows/Fonts/arialbd.ttf"}},
    {"arial_italic", {"C:/Windows/Fonts/ariali.ttf"}},
    {"arial_bolditalic", {"C:/Windows/Fonts/arialbi.ttf"}},
    {"segoeui", {"C:/Windows/Fonts/segoeui.ttf"}},
    {"segoeui_bold", {"C:/Windows/Fonts/seguib.ttf"}},
    {"segoeui_italic", {"C:/Windows/Fonts/segoeuii.ttf"}},
    {"segoeui_bolditalic", {"C:/Windows/Fonts/seguisbi.ttf", "C:/Windows/Fonts/seguibli.ttf"}},
    {"calibri", {"C:/Windows/Fonts/calibri.ttf"}},
    {"calibri_bold", {"C:/Windows/Fonts/calibrib.ttf"}},
    {"calibri_italic", {"C:/Windows/Fonts/calibrii.ttf"}},
    {"calibri_bolditalic", {"C:/Windows/Fonts/calibriz.ttf"}},
    {"tahoma", {"C:/Windows/Fonts/tahoma.ttf"}},
    {"tahoma_bold", {"C:/Windows/Fonts/tahomabd.ttf"}},
    {"verdana", {"C:/Windows/Fonts/verdana.ttf"}},
    {"verdana_bold", {"C:/Windows/Fonts/verdanab.ttf"}},
    {"verdana_italic", {"C:/Windows/Fonts/verdanai.ttf"}},
    {"verdana_bolditalic", {"C:/Windows/Fonts/verdanaz.ttf"}},
    {"consolas", {"C:/Windows/Fonts/consola.ttf"}},
    {"consolas_bold", {"C:/Windows/Fonts/consolab.ttf"}},
    {"consolas_italic", {"C:/Windows/Fonts/consolai.ttf"}},
    {"consolas_bolditalic", {"C:/Windows/Fonts/consolaz.ttf"}},
    {"couriernew", {"C:/Windows/Fonts/cour.ttf"}},
    {"couriernew_bold", {"C:/Windows/Fonts/courbd.ttf"}},
    {"couriernew_italic", {"C:/Windows/Fonts/couri.ttf"}},
    {"couriernew_bolditalic", {"C:/Windows/Fonts/courbi.ttf"}},
    {"timesnewroman", {"C:/Windows/Fonts/times.ttf"}},
    {"timesnewroman_bold", {"C:/Windows/Fonts/timesbd.ttf"}},
    {"timesnewroman_italic", {"C:/Windows/Fonts/timesi.ttf"}},
    {"timesnewroman_bolditalic", {"C:/Windows/Fonts/timesbi.ttf"}},
    {"dejavusans", {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"}},
    {"dejavusans_bold", {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"}},
    {"dejavusans_italic", {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf", "/usr/share/fonts/TTF/DejaVuSans-Oblique.ttf"}},
    {"dejavusans_bolditalic", {"/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf", "/usr/share/fonts/TTF/DejaVuSans-BoldOblique.ttf"}},
    {"helvetica", {"/System/Library/Fonts/Helvetica.ttc"}},
  };

  std::vector<std::string> normalized_families;
  normalized_families.reserve(families.size());
  for (const std::string& raw_family : families) {
    const std::string family = normalize_font_token(strip_matching_quotes(raw_family));
    if (!family.empty()) normalized_families.push_back(family);
  }

  for (const std::string& family : normalized_families) {
    std::vector<std::string> lookup_keys;
    if (want_bold && want_italic) lookup_keys.push_back(family + "_bolditalic");
    if (want_bold) lookup_keys.push_back(family + "_bold");
    if (want_italic) lookup_keys.push_back(family + "_italic");
    lookup_keys.push_back(family);

    for (const std::string& lookup_key : lookup_keys) {
      const auto it = known_fonts.find(lookup_key);
      if (it == known_fonts.end()) continue;
      for (const std::string& candidate : it->second) {
        if (fs::exists(candidate)) return candidate;
      }
    }
  }

  const std::vector<fs::path> search_roots = {
    fs::path("C:/Windows/Fonts"),
    fs::path("/usr/share/fonts/truetype"),
    fs::path("/usr/share/fonts/TTF"),
    fs::path("/System/Library/Fonts"),
  };

  for (const fs::path& root : search_roots) {
    if (!fs::exists(root)) continue;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
      if (!it->is_regular_file()) continue;
      const std::string normalized_name = normalize_font_token(it->path().stem().string());
      for (const std::string& family : normalized_families) {
        if (family.empty() || normalized_name.find(family) == std::string::npos) continue;
        if (want_bold && normalized_name.find("bold") == std::string::npos &&
            normalized_name.find("semibold") == std::string::npos &&
            normalized_name.find("demibold") == std::string::npos) {
          continue;
        }
        if (want_italic && normalized_name.find("italic") == std::string::npos &&
            normalized_name.find("oblique") == std::string::npos) {
          continue;
        }
        if (!want_italic && normalized_name.find("italic") != std::string::npos) continue;
        if (!want_bold && normalized_name.find("bold") != std::string::npos) continue;
        return it->path().string();
      }
    }
  }

  return std::nullopt;
}

FontLibrary& font_library() {
  static FontLibrary library;
  return library;
}

FT_Face load_font_face(const std::string& font_path) {
  FontLibrary& library = font_library();
  auto it = library.faces.find(font_path);
  if (it != library.faces.end()) return it->second;

  FT_Face face = nullptr;
  if (FT_New_Face(library.library, font_path.c_str(), 0, &face) != 0) {
    throw std::runtime_error("Failed to load font: " + font_path);
  }
  library.faces[font_path] = face;
  return face;
}

struct OutlineBuilder {
  std::string d;
  double offset_x = 0.0;
  double offset_y = 0.0;
  bool contour_open = false;
};

int ft_move_to_cb(const FT_Vector* to, void* user) {
  auto* builder = static_cast<OutlineBuilder*>(user);
  if (builder->contour_open) {
    builder->d += "Z";
    builder->contour_open = false;
  }
  builder->d += "M" + fmt(builder->offset_x + to->x / 64.0) + "," + fmt(builder->offset_y - to->y / 64.0);
  builder->contour_open = true;
  return 0;
}

int ft_line_to_cb(const FT_Vector* to, void* user) {
  auto* builder = static_cast<OutlineBuilder*>(user);
  builder->d += "L" + fmt(builder->offset_x + to->x / 64.0) + "," + fmt(builder->offset_y - to->y / 64.0);
  builder->contour_open = true;
  return 0;
}

int ft_conic_to_cb(const FT_Vector* control, const FT_Vector* to, void* user) {
  auto* builder = static_cast<OutlineBuilder*>(user);
  builder->d += "Q" +
    fmt(builder->offset_x + control->x / 64.0) + "," + fmt(builder->offset_y - control->y / 64.0) + " " +
    fmt(builder->offset_x + to->x / 64.0) + "," + fmt(builder->offset_y - to->y / 64.0);
  builder->contour_open = true;
  return 0;
}

int ft_cubic_to_cb(const FT_Vector* control1, const FT_Vector* control2, const FT_Vector* to, void* user) {
  auto* builder = static_cast<OutlineBuilder*>(user);
  builder->d += "C" +
    fmt(builder->offset_x + control1->x / 64.0) + "," + fmt(builder->offset_y - control1->y / 64.0) + " " +
    fmt(builder->offset_x + control2->x / 64.0) + "," + fmt(builder->offset_y - control2->y / 64.0) + " " +
    fmt(builder->offset_x + to->x / 64.0) + "," + fmt(builder->offset_y - to->y / 64.0);
  builder->contour_open = true;
  return 0;
}

}  // namespace

std::string collect_direct_text(const pugi::xml_node& node) {
  std::string text;
  for (pugi::xml_node child : node.children()) {
    if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
      text += child.value();
    }
  }
  return trim(text);
}

std::optional<std::string> inherited_attr(const pugi::xml_node& node, const char* name) {
  for (pugi::xml_node current = node; current; current = current.parent()) {
    if (current.attribute(name)) {
      return std::string(current.attribute(name).as_string());
    }
  }
  return std::nullopt;
}

std::optional<std::string> discover_default_font() {
  static const std::vector<std::string> candidates = {
    "C:/Windows/Fonts/arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
    "C:/Windows/Fonts/calibri.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/SFNSText.ttf",
  };

  for (const std::string& candidate : candidates) {
    if (fs::exists(candidate)) return candidate;
  }
  return std::nullopt;
}

double parse_svg_length(const std::string& value, double fallback) {
  const std::string trimmed = trim(value);
  if (trimmed.empty()) return fallback;
  std::size_t consumed = 0;
  try {
    return std::stod(trimmed, &consumed);
  } catch (...) {
    return fallback;
  }
}

TextLayoutResult text_to_path(const std::string& text,
                              double x,
                              double y,
                              double font_size,
                              const std::string& font_path,
                              double letter_spacing,
                              const std::vector<double>& x_values,
                              const std::vector<double>& y_values,
                              const std::vector<double>& dx_values,
                              const std::vector<double>& dy_values) {
  TextLayoutResult result;
  result.end_x = x;
  result.end_y = y;
  if (text.empty()) return result;

  FT_Face face = load_font_face(font_path);
  if (FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(std::llround(font_size * 64.0)), 72, 72) != 0) {
    throw std::runtime_error("Failed to set font size for: " + font_path);
  }

  OutlineBuilder builder;
  FT_Outline_Funcs funcs{};
  funcs.move_to = ft_move_to_cb;
  funcs.line_to = ft_line_to_cb;
  funcs.conic_to = ft_conic_to_cb;
  funcs.cubic_to = ft_cubic_to_cb;
  funcs.shift = 0;
  funcs.delta = 0;

  double pen_x = x;
  double pen_y = y;
  FT_UInt previous_glyph = 0;

  for (std::size_t i = 0; i < text.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    const FT_UInt glyph_index = FT_Get_Char_Index(face, ch);

    if (i < x_values.size()) pen_x = x_values[i];
    if (i < y_values.size()) pen_y = y_values[i];
    if (i < dx_values.size()) pen_x += dx_values[i];
    if (i < dy_values.size()) pen_y += dy_values[i];

    if (previous_glyph != 0 && glyph_index != 0 && FT_HAS_KERNING(face) &&
        (i >= x_values.size() || !std::isfinite(x_values[i]))) {
      FT_Vector kerning{};
      if (FT_Get_Kerning(face, previous_glyph, glyph_index, FT_KERNING_DEFAULT, &kerning) == 0) {
        pen_x += kerning.x / 64.0;
      }
    }

    if (FT_Load_Glyph(face, glyph_index, FT_LOAD_NO_BITMAP) == 0 &&
        face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
      builder.offset_x = pen_x;
      builder.offset_y = pen_y;
      FT_Outline_Decompose(&face->glyph->outline, &funcs, &builder);
      if (builder.contour_open) {
        builder.d += "Z";
        builder.contour_open = false;
      }
    }

    if (FT_Load_Char(face, ch, FT_LOAD_NO_BITMAP) == 0) {
      pen_x += face->glyph->advance.x / 64.0;
      if (i + 1 < text.size()) pen_x += letter_spacing;
      result.end_y = pen_y;
    }
    previous_glyph = glyph_index;
  }

  result.d = builder.d;
  result.end_x = pen_x;
  return result;
}

std::string text_to_path(const std::string& text,
                         double x,
                         double y,
                         double font_size,
                         const std::string& font_path) {
  return text_to_path(text, x, y, font_size, font_path, 0.0, {}, {}, {}, {}).d;
}

std::string text_to_path(const std::string& text,
                         double x,
                         double y,
                         double font_size,
                         const std::string& font_path,
                         double letter_spacing) {
  return text_to_path(text, x, y, font_size, font_path, letter_spacing, {}, {}, {}, {}).d;
}

double measure_text_advance(const std::string& text, double font_size, const std::string& font_path) {
  return measure_text_advance(text, font_size, font_path, 0.0);
}

double measure_text_advance(const std::string& text,
                            double font_size,
                            const std::string& font_path,
                            double letter_spacing) {
  if (text.empty()) return 0.0;
  return text_to_path(text, 0.0, 0.0, font_size, font_path, letter_spacing, {}, {}, {}, {}).end_x;
}

std::optional<std::string> resolve_text_font_path(const StyleState& style,
                                                  const std::optional<std::string>& fallback_font_path) {
  if (!style.font_family.empty()) {
    if (const auto family_font = discover_font_for_family(style.font_family, style.font_weight, style.font_style)) {
      return family_font;
    }
  }
  return fallback_font_path;
}

double first_coord_value(const pugi::xml_node& node, const char* attr_name, double fallback) {
  if (!node.attribute(attr_name)) return fallback;
  const std::vector<double> values = parse_number_list(node.attribute(attr_name).as_string());
  if (!values.empty()) return values.front();
  return fallback;
}

std::vector<double> coord_values(const pugi::xml_node& node, const char* attr_name) {
  if (!node.attribute(attr_name)) return {};
  return parse_number_list(node.attribute(attr_name).as_string());
}

}  // namespace svg_squisher

