cmake_minimum_required(VERSION 3.25)

find_package(X11 REQUIRED)
find_library(XTST_LIB Xtst REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)

add_library(imeaura_platform STATIC
  ../platform_factory.cpp
  ../firefly_host.cpp
  linux_platform.cpp
  linux_ime.cpp
  linux_firefly.cpp
  linux_x11_edges.cpp
  linux_settings.cpp
)

target_include_directories(imeaura_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(imeaura_platform PUBLIC imeaura_core)
target_compile_options(imeaura_platform PRIVATE -Wall -Wextra)
target_link_libraries(imeaura_platform PRIVATE X11::X11 ${XTST_LIB} PkgConfig::GTK4)
