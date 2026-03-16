#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>

#include <cmath>

using namespace vsgocct::cad;
using namespace vsgocct::scene;
using namespace vsgocct::test;

class AssemblySceneTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        assembly = readStep(testDataPath("nested_assembly.step"));
    }
    AssemblyData assembly;
};

TEST_F(AssemblySceneTest, BuildSceneNonNull)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_TRUE(sceneData.scene);
}

TEST_F(AssemblySceneTest, PartsListMatchesLeafCount)
{
    auto sceneData = buildAssemblyScene(assembly);

    // nested_assembly has 2 Part leaves (Box + Cylinder)
    EXPECT_EQ(sceneData.parts.size(), 2u);
}

TEST_F(AssemblySceneTest, PartsHaveNames)
{
    auto sceneData = buildAssemblyScene(assembly);

    for (const auto& part : sceneData.parts)
    {
        EXPECT_FALSE(part.name.empty());
    }
}

TEST_F(AssemblySceneTest, PartSwitchTogglesVisibility)
{
    auto sceneData = buildAssemblyScene(assembly);
    ASSERT_FALSE(sceneData.parts.empty());

    auto& sw = sceneData.parts.front().switchNode;
    ASSERT_TRUE(sw);
    ASSERT_FALSE(sw->children.empty());

    // Default: visible
    EXPECT_NE(sw->children.front().mask, vsg::MASK_OFF);

    // Toggle off
    sw->children.front().mask = vsg::MASK_OFF;
    EXPECT_EQ(sw->children.front().mask, vsg::MASK_OFF);

    // Toggle on
    sw->children.front().mask = vsg::MASK_ALL;
    EXPECT_NE(sw->children.front().mask, vsg::MASK_OFF);
}

TEST_F(AssemblySceneTest, SceneCenterAndRadius)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_FALSE(std::isnan(sceneData.center.x));
    EXPECT_FALSE(std::isnan(sceneData.center.y));
    EXPECT_FALSE(std::isnan(sceneData.center.z));
    EXPECT_GT(sceneData.radius, 0.0);
}

TEST_F(AssemblySceneTest, GeometryCounts)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_GT(sceneData.totalTriangleCount, 0u);
    // Lines and points may or may not exist depending on meshing
}

TEST_F(AssemblySceneTest, FaceRegistryPopulated)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_FALSE(sceneData.faceRegistry.empty());

    for (const auto& [faceId, info] : sceneData.faceRegistry)
    {
        EXPECT_GT(faceId, 0u);
        EXPECT_FALSE(info.partName.empty());
    }
}

TEST_F(AssemblySceneTest, FaceColorRefsPopulated)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_FALSE(sceneData.faceColorRefs.empty());

    for (const auto& [faceId, info] : sceneData.faceRegistry)
    {
        EXPECT_TRUE(sceneData.faceColorRefs.count(faceId) > 0)
            << "Missing color ref for faceId=" << faceId;
        auto& ref = sceneData.faceColorRefs.at(faceId);
        EXPECT_TRUE(ref.colorArray);
        EXPECT_GT(ref.vertexCount, 0u);
    }
}

TEST_F(AssemblySceneTest, PickSceneCreated)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_TRUE(sceneData.pickScene);
}

TEST(AssemblySceneSimple, EmptyAssemblyRoots)
{
    // Empty assembly should produce valid but empty scene
    AssemblyData empty;
    // buildAssemblyScene should not crash on empty input
    // Note: readStep would throw before this happens, but test the scene builder directly
    auto sceneData = buildAssemblyScene(empty);
    EXPECT_TRUE(sceneData.scene);
    EXPECT_TRUE(sceneData.parts.empty());
}

TEST(AssemblySceneSimple, SinglePartBox)
{
    auto assembly = readStep(testDataPath("box.step"));
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_TRUE(sceneData.scene);
    EXPECT_GE(sceneData.parts.size(), 1u);
    EXPECT_GT(sceneData.totalTriangleCount, 0u);
}

TEST(AssemblySceneSimple, ColoredBoxProducesScene)
{
    auto assembly = readStep(testDataPath("colored_box.step"));
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_TRUE(sceneData.scene);
    EXPECT_GE(sceneData.parts.size(), 1u);
    EXPECT_GT(sceneData.totalTriangleCount, 0u);

    // Verify color at data level: the input assembly should carry red color
    ASSERT_FALSE(assembly.roots.empty());
    const auto& root = assembly.roots.front();
    EXPECT_TRUE(root.color.isSet);
    EXPECT_NEAR(root.color.r, 1.0f, 0.01f);
    EXPECT_NEAR(root.color.g, 0.0f, 0.01f);
    EXPECT_NEAR(root.color.b, 0.0f, 0.01f);
}

TEST(AssemblySceneSimple, SinglePartBoxFaceRegistry)
{
    auto assembly = readStep(testDataPath("box.step"));
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_EQ(sceneData.faceRegistry.size(), 6u);
}
