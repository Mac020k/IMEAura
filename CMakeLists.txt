cmake_minimum_required(VERSION 3.25)
project(IMEAura VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IMEAURA_BUILD_TESTS "Build unit tests" ON)

include(GNUInstallDirs)

add_subdirectory(src)

if(IMEAURA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Copy compile_commands.json to repo root for clangd (Ninja/Make generators only)
file(WRITE ${CMAKE_BINARY_DIR}/copy_compile_commands.cmake "
if(EXISTS \"${CMAKE_BINARY_DIR}/compile_commands.json\")
  file(COPY \"${CMAKE_BINARY_DIR}/compile_commands.json\" DESTINATION \"${CMAKE_SOURCE_DIR}\")
endif()
")
add_custom_target(imeaura_compile_commands ALL
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/copy_compile_commands.cmake
  COMMENT "Copy compile_commands.json to project root when available"
)
