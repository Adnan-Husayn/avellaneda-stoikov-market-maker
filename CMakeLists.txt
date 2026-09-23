cmake_minimum_required(VERSION 3.16)
project(market_maker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(engine STATIC engine/market_maker.cpp)
target_include_directories(engine PUBLIC engine)

add_executable(mm_cli cli/main.cpp)
target_link_libraries(mm_cli PRIVATE engine)

add_executable(mm_web_runner gui_web/web_runner.cpp)
target_link_libraries(mm_web_runner PRIVATE engine)

# Native GUI is optional: only built if GLFW + OpenGL are available.
find_package(OpenGL QUIET)
find_package(glfw3 QUIET)

if(OpenGL_FOUND AND glfw3_FOUND)
  file(GLOB IMGUI_SOURCES
    gui_native/third_party/imgui/*.cpp
    gui_native/third_party/imgui/backends/imgui_impl_glfw.cpp
    gui_native/third_party/imgui/backends/imgui_impl_opengl3.cpp
    gui_native/third_party/implot/*.cpp
  )

  add_executable(mm_gui gui_native/main.cpp ${IMGUI_SOURCES})
  target_include_directories(mm_gui PRIVATE
    gui_native/third_party/imgui
    gui_native/third_party/imgui/backends
    gui_native/third_party/implot
  )
  target_link_libraries(mm_gui PRIVATE engine glfw OpenGL::GL)

  message(STATUS "GLFW + OpenGL found: building mm_gui (native GUI)")
else()
  message(STATUS "GLFW or OpenGL not found: skipping mm_gui. Run 'brew install glfw' to enable it.")
endif()
