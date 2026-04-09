cmake_minimum_required(VERSION 3.20)

project(svg_squisher VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
  pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG v1.15
)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  freetype
  GIT_REPOSITORY https://gitlab.freedesktop.org/freetype/freetype.git
  GIT_TAG VER-2-13-3
)

FetchContent_MakeAvailable(pugixml)
FetchContent_MakeAvailable(freetype)

add_executable(svg_squisher
  src/main.cpp
  src/svg_computed_style.cpp
  src/svg_dom.cpp
  src/svg_entry.cpp
  src/svg_geometry.cpp
  src/svg_output.cpp
  src/svg_paint.cpp
  src/svg_path.cpp
  src/svg_postprocess.cpp
  src/svg_shape.cpp
  src/svg_style.cpp
  src/svg_stroke.cpp
  src/svg_text.cpp
  src/svg_transform.cpp
  src/svg_traversal.cpp
  src/svg_util.cpp
  src/svg_squisher.cpp
)

target_include_directories(svg_squisher PRIVATE src)
target_include_directories(svg_squisher PRIVATE
  ${freetype_SOURCE_DIR}/include
)
target_link_libraries(svg_squisher PRIVATE pugixml freetype)

if(MSVC)
  target_compile_options(svg_squisher PRIVATE /W4 /permissive-)
else()
  target_compile_options(svg_squisher PRIVATE -Wall -Wextra -Wpedantic)
endif()
