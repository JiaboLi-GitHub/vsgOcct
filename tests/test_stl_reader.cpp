// tests/test_stl_reader.cpp
#include <gtest/gtest.h>
#include <vsgocct/cad/StlReader.h>
#include <filesystem>

static std::filesystem::path testDataDir()
{
    return std::filesystem::path(TEST_DATA_DIR);
}

TEST(StlReader, ReadsAsciiStlFile)
{
    auto data = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    ASSERT_FALSE(data.triangulation.IsNull());
    EXPECT_EQ(data.triangulation->NbTriangles(), 12);
    EXPECT_EQ(data.triangulation->NbNodes(), 8);
    EXPECT_FALSE(data.name.empty());
}

TEST(StlReader, ReadsBinaryStlFile)
{
    auto data = vsgocct::cad::readStl(testDataDir() / "cube_binary.stl");
    ASSERT_FALSE(data.triangulation.IsNull());
    EXPECT_EQ(data.triangulation->NbTriangles(), 12);
    EXPECT_EQ(data.triangulation->NbNodes(), 8);
}

TEST(StlReader, ThrowsOnInvalidPath)
{
    EXPECT_THROW(vsgocct::cad::readStl("nonexistent.stl"), std::runtime_error);
}

TEST(StlReader, UsesFilenameStemForBinaryName)
{
    auto data = vsgocct::cad::readStl(testDataDir() / "cube_binary.stl");
    EXPECT_EQ(data.name, "cube_binary");
}
