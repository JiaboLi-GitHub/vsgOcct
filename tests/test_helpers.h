#pragma once

#include <filesystem>
#include <string>

// TEST_DATA_DIR is injected by CMake via target_compile_definitions
#ifndef TEST_DATA_DIR
#error "TEST_DATA_DIR must be defined by CMake"
#endif

namespace vsgocct::test
{
inline std::filesystem::path testDataPath(const std::string& filename)
{
    return std::filesystem::path(TEST_DATA_DIR) / filename;
}
} // namespace vsgocct::test
