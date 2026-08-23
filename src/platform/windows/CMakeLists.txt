cmake_minimum_required(VERSION 3.25)

add_library(imeaura_platform STATIC
  ../platform_factory.cpp
  win_platform.cpp
  win_comp_edges.cpp
  win_ime.cpp
  win_text_input.cpp
  win_tray.cpp
  win_icon.cpp
  win_assets.cpp
  win_color_dialog.cpp
  win_about.cpp
  win_settings.cpp
  win_firefly.cpp
)

target_include_directories(imeaura_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(imeaura_platform PUBLIC imeaura_core)

target_compile_definitions(imeaura_platform PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNING)
target_compile_options(imeaura_platform PRIVATE /utf-8 /W4 /permissive-)
set_property(TARGET imeaura_platform PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(imeaura_platform PRIVATE
  user32 gdi32 imm32 shell32 ole32 oleaut32 dwmapi windowsapp comctl32 shcore comdlg32
  UIAutomationCore d2d1 dwrite wtsapi32 advapi32 hid setupapi
)
