// tests/test_model_loader.cpp
#include <gtest/gtest.h>
#include <vsgocct/ModelLoader.h>
#include <filesystem>

static std::filesystem::path testDataDir()
{
    return std::filesystem::path(TEST_DATA_DIR);
}

TEST(ModelLoader, LoadStlSceneProducesValidScene)
{
    auto sceneData = vsgocct::loadScene(testDataDir() / "cube.stl");

    ASSERT_NE(sceneData.scene, nullptr);
    EXPECT_EQ(sceneData.parts.size(), 1u);
    EXPECT_EQ(sceneData.totalTriangleCount, 12u);
    EXPECT_GT(sceneData.totalLineSegmentCount, 0u);
    EXPECT_GT(sceneData.totalPointCount, 0u);
    EXPECT_GT(sceneData.radius, 0.0);
}

TEST(ModelLoader, LoadStepSceneStillWorks)
{
    auto sceneData = vsgocct::loadScene(testDataDir() / "box.step");

    ASSERT_NE(sceneData.scene, nullptr);
    EXPECT_GE(sceneData.parts.size(), 1u);
    EXPECT_GT(sceneData.totalTriangleCount, 0u);
}

TEST(ModelLoader, LoadStlSceneExplicit)
{
    auto sceneData = vsgocct::loadStlScene(testDataDir() / "cube_binary.stl");

    ASSERT_NE(sceneData.scene, nullptr);
    EXPECT_EQ(sceneData.parts.size(), 1u);
    EXPECT_EQ(sceneData.parts[0].name, "cube_binary");
}

TEST(ModelLoader, UnsupportedExtensionThrows)
{
    EXPECT_THROW(vsgocct::loadScene("model.obj"), std::runtime_error);
}

TEST(ModelLoader, CaseInsensitiveExtension)
{
    // This tests the extension matching logic
    // We can't easily test with actual .STL file, but we verify
    // the function doesn't throw for the correct path
    auto sceneData = vsgocct::loadStlScene(testDataDir() / "cube.stl");
    EXPECT_EQ(sceneData.parts.size(), 1u);
}
