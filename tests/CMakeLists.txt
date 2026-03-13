include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# Prevent GoogleTest from overriding parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Helper function to reduce boilerplate
function(vsgocct_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE GTest::gtest_main vsgocct)
    target_compile_definitions(${name} PRIVATE
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    gtest_discover_tests(${name})
endfunction()

vsgocct_add_test(test_step_reader)
vsgocct_add_test(test_shape_mesher)
vsgocct_add_test(test_scene_builder)

# --- generate_test_data (utility, not a test) ---
# Links OCCT directly because it calls BRepPrimAPI and STEPControl_Writer.
# vsgocct already provides OCCT include dirs as PUBLIC.
# If TKDESTEP doesn't provide STEPControl_Writer, try adding TKXDESTEP or TKSTEP.
add_executable(generate_test_data generate_test_data.cpp)
target_link_libraries(generate_test_data PRIVATE vsgocct TKDESTEP TKLCAF TKXCAF TKPrim TKTopAlgo)
target_compile_definitions(generate_test_data PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
