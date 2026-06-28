# src/CMakeLists.txt — Macro Intelligence Terminal v0.3
# ─────────────────────────────────────────────────────────────────────────────

set(MACRO_SOURCES
    main.cpp
    app/GeoSelectionContext.cpp
    ui_common/Theme.cpp
)

add_executable(macro-terminal ${MACRO_SOURCES})

target_compile_features(macro-terminal PUBLIC cxx_std_23)

target_include_directories(macro-terminal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── Core always-required link deps ────────────────────────────────────────────
target_link_libraries(macro-terminal PRIVATE
    glfw
    glad::glad
    imgui::imgui
    implot::implot
    nlohmann_json::nlohmann_json
    Eigen3::Eigen
    CURL::libcurl
    spdlog::spdlog
)

# ── OpenSceneGraph (optional) ─────────────────────────────────────────────────
if (OpenSceneGraph_FOUND)
    target_include_directories(macro-terminal PRIVATE ${OPENSCENEGRAPH_INCLUDE_DIRS})
    target_link_libraries(macro-terminal PRIVATE ${OPENSCENEGRAPH_LIBRARIES})
endif()

# ── osgEarth (optional) ───────────────────────────────────────────────────────
if (osgEarth_FOUND)
    target_link_libraries(macro-terminal PRIVATE osgEarth::osgEarth)
    target_compile_definitions(macro-terminal PRIVATE MACRO_HAVE_OSGEARTH)
endif()

# ── Platform ──────────────────────────────────────────────────────────────────
if (WIN32)
    target_compile_definitions(macro-terminal PRIVATE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries(macro-terminal PRIVATE dl pthread)
endif()

# ── Copy data/ to build dir so fonts/earth files are accessible at runtime ────
add_custom_command(TARGET macro-terminal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:macro-terminal>/data
    COMMENT "Copying data/ to build output directory"
)
