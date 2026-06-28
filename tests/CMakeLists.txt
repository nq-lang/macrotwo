find_package(Catch2 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)

add_executable(macro-tests
    test_factor_model.cpp
    test_geo_context.cpp
    test_gmsi.cpp
    test_providers.cpp
    test_section2.cpp
)

target_compile_features(macro-tests PUBLIC cxx_std_23)

target_include_directories(macro-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(macro-tests PRIVATE
    Catch2::Catch2WithMain
    nlohmann_json::nlohmann_json
    Eigen3::Eigen
)

if (UNIX AND NOT APPLE)
    target_link_libraries(macro-tests PRIVATE pthread)
endif()

include(CTest)
include(Catch)
catch_discover_tests(macro-tests
    TEST_SPEC "*"
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
    REPORTER console
)
