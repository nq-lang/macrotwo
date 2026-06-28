cmake_minimum_required(VERSION 3.28)

project(MacroTerminal
    VERSION 0.3.0
    DESCRIPTION "Institutional Macro Intelligence Terminal — C++23"
    LANGUAGES CXX
)

# ── C++23 ─────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Output dirs ───────────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── vcpkg dependencies ────────────────────────────────────────────────────────
find_package(glfw3         CONFIG REQUIRED)
find_package(glad          CONFIG REQUIRED)
find_package(imgui         CONFIG REQUIRED)
find_package(implot        CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Eigen3        CONFIG REQUIRED)
find_package(CURL          CONFIG REQUIRED)
find_package(spdlog        CONFIG REQUIRED)
find_package(Catch2        CONFIG REQUIRED)

# OSG (OpenSceneGraph) — optional components
find_package(OpenSceneGraph QUIET
    COMPONENTS osgDB osgGA osgViewer osgUtil osgText)
if (OpenSceneGraph_FOUND)
    message(STATUS "OpenSceneGraph found")
else()
    message(WARNING "OpenSceneGraph NOT found — globe will use stub renderer only")
endif()

# osgEarth — optional, installed separately
find_package(osgEarth QUIET)
if (osgEarth_FOUND)
    message(STATUS "osgEarth found — full WGS84 globe enabled")
    add_compile_definitions(MACRO_HAVE_OSGEARTH)
else()
    message(WARNING "osgEarth NOT found — stub globe renderer active")
endif()

# ── Compiler flags ────────────────────────────────────────────────────────────
if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor /utf-8)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
    # Enable C++23 std::generator / std::flat_map / std::expected / std::print
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fcoroutines)
    endif()
endif()

# ── Subdirectories ────────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(tests)
