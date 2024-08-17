# CMakeLists.txt
# Bootstrap with cmake .. -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
cmake_minimum_required(VERSION 3.0)
project("basic-ui-template"
        VERSION 0.1.0
        LANGUAGES C CXX)

find_package(sdl2 CONFIG REQUIRED)
find_package(glew CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)

include_directories(
    inc
    external/nanosvg/src
    external/tomlplusplus
)

add_executable(basic-ui-template WIN32
    src/main.cpp
    src/renderer.cpp
    src/util.cpp
    src/glyph-cache.cpp
    src/feed.cpp
    src/help.cpp
    src/choice.cpp
    src/utf-8.cpp
    src/config.cpp
    src/window-theming.cpp
    src/svg.cpp
    src/examples.cpp
    src/basic-scrollbox.cpp
    src/basic-textbox.cpp
    src/basic-window.cpp)

# Require c++20, this is better than setting CMAKE_CXX_STANDARD since it won't pollute other targets
# note : cxx_std_* features were added in CMake 3.8.2
target_compile_features(basic-ui-template PRIVATE cxx_std_20)

target_compile_options(basic-ui-template PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W4 /WX /permissive- /Zc:preprocessor /MP /utf-8>)

target_link_libraries(basic-ui-template PRIVATE SDL2::SDL2main)
target_link_libraries(basic-ui-template PRIVATE SDL2::SDL2)
target_link_libraries(basic-ui-template PRIVATE GLEW::GLEW)
target_link_libraries(basic-ui-template PRIVATE freetype)

file(COPY shaders DESTINATION ${PROJECT_BINARY_DIR})
file(COPY fonts DESTINATION ${PROJECT_BINARY_DIR})