cmake_minimum_required(VERSION 3.25)

add_library(imeaura_platform STATIC
  ../platform_factory.cpp
  mac_platform.mm
)

target_include_directories(imeaura_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(imeaura_platform PUBLIC imeaura_core)
target_compile_options(imeaura_platform PRIVATE -Wall -Wextra -fobjc-arc)
target_link_libraries(imeaura_platform PRIVATE
  "-framework AppKit"
  "-framework CoreGraphics"
  "-framework QuartzCore"
  "-framework Carbon"
)
