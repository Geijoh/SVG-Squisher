#include "svg_squisher.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using svg_squisher::Options;
using svg_squisher::SvgSquisher;

namespace {

void print_usage() {
  std::cout
      << "SVG Squisher C++\n\n"
      << "Usage:\n"
      << "  svg_squisher <input.svg> <output.svg> [--fill <color>] [--font <path>]\n"
      << "  svg_squisher <input-dir> <output-dir> [--fill <color>] [--font <path>]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      print_usage();
      return 1;
    }

    Options options;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--fill") {
        if (i + 1 >= argc) {
          throw std::runtime_error("--fill requires a value");
        }
        options.fill_override = std::string(argv[++i]);
      } else if (arg == "--font") {
        if (i + 1 >= argc) {
          throw std::runtime_error("--font requires a value");
        }
        options.font_path = std::string(argv[++i]);
      } else if (arg == "--help" || arg == "-h") {
        print_usage();
        return 0;
      } else {
        positional.push_back(arg);
      }
    }

    if (positional.size() < 2) {
      print_usage();
      return 1;
    }

    const fs::path input = positional[0];
    const fs::path output = positional[1];

    SvgSquisher squisher;
    if (fs::is_directory(input)) {
      squisher.squish_directory(input, output, options);
      std::cout << "Converted directory: " << input << " -> " << output << "\n";
    } else {
      squisher.squish_file(input, output, options);
      std::cout << "Converted file: " << input << " -> " << output << "\n";
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }
}

