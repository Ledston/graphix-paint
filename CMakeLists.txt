cmake_minimum_required(VERSION 3.21)
project(GraphiXPaint VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent)

file(GLOB_RECURSE GX_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/src/*.cpp
  ${CMAKE_SOURCE_DIR}/src/*.h)
list(FILTER GX_SOURCES EXCLUDE REGEX ".*/src/main\\.cpp$")

add_library(gxcore STATIC ${GX_SOURCES} resources/resources.qrc)
target_include_directories(gxcore PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(gxcore PUBLIC
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Concurrent)
target_compile_definitions(gxcore PUBLIC GX_VERSION="${PROJECT_VERSION}")
# private Qt GUI API: switching the Wintab tablet interface on and off at run time
if(TARGET Qt6::GuiPrivate)
  target_link_libraries(gxcore PUBLIC Qt6::GuiPrivate)
endif()

if(MSVC)
  target_compile_options(gxcore PUBLIC /W3 /MP /utf-8 /permissive- /Zc:__cplusplus)
  target_compile_definitions(gxcore PUBLIC NOMINMAX _USE_MATH_DEFINES)
else()
  target_compile_options(gxcore PUBLIC -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(GraphiX src/main.cpp)
if(WIN32)
  target_sources(GraphiX PRIVATE resources/app.rc)
  set_target_properties(GraphiX PROPERTIES WIN32_EXECUTABLE ON)
endif()
target_link_libraries(GraphiX PRIVATE gxcore)

option(GX_BUILD_TESTS "Build unit tests" ON)
if(GX_BUILD_TESTS)
  enable_testing()
  add_executable(gx_tests tests/test_main.cpp)
  target_link_libraries(gx_tests PRIVATE gxcore)
  add_test(NAME gx_tests COMMAND gx_tests)
  add_executable(gx_uismoke tests/ui_smoke.cpp)
  target_link_libraries(gx_uismoke PRIVATE gxcore)
  add_executable(gx_uiscenarios tests/ui_scenarios.cpp)
  target_link_libraries(gx_uiscenarios PRIVATE gxcore)
  add_executable(gx_fxsheet tests/fx_sheet.cpp)
  target_link_libraries(gx_fxsheet PRIVATE gxcore)
  add_executable(gx_brushsheet tests/brush_sheet.cpp)
  target_link_libraries(gx_brushsheet PRIVATE gxcore)
endif()

install(TARGETS GraphiX RUNTIME DESTINATION .)
