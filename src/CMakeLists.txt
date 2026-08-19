add_library(imeaura_core STATIC
  core/json.cpp
  core/settings.cpp
  core/policy.cpp
  core/i18n.cpp
  core/firefly.cpp
)
target_include_directories(imeaura_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
  add_subdirectory(platform/windows)
elseif(APPLE)
  add_subdirectory(platform/macos)
else()
  add_subdirectory(platform/linux)
endif()

add_executable(IMEAura
  app/main.cpp
  app/app.cpp
)
if(WIN32)
  target_sources(IMEAura PRIVATE
    platform/windows/imeaura.rc
  )
endif()
target_link_libraries(IMEAura PRIVATE imeaura_core imeaura_platform)
set_target_properties(IMEAura PROPERTIES OUTPUT_NAME "IMEAura")

if(WIN32)
  target_compile_definitions(IMEAura PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MSVC)
    target_compile_options(IMEAura PRIVATE /utf-8 /W4 /permissive-)
    set_property(TARGET IMEAura PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET IMEAura PROPERTY WIN32_EXECUTABLE TRUE)
    target_link_options(IMEAura PRIVATE /ENTRY:mainCRTStartup)
    set_property(TARGET imeaura_core PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_custom_command(TARGET IMEAura POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/img"
            "$<TARGET_FILE_DIR:IMEAura>/img"
    COMMENT "Copy img assets next to IMEAura.exe")
endif()
