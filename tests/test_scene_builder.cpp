#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/ShapeId.h>
#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>

#include <cmath>

using namespace vsgocct;
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

// --- M1b: SceneIndex tests ---

TEST_F(AssemblySceneTest, SceneIndexForwardLookup)
{
    auto sceneData = buildAssemblyScene(assembly);

    for (const auto& part : sceneData.parts)
    {
        auto it = sceneData.index.nodeToShape.find(part.switchNode.get());
        ASSERT_NE(it, sceneData.index.nodeToShape.end())
            << "Part '" << part.name << "' switch not in nodeToShape";

        ShapeId sid = it->second;
        EXPECT_TRUE(static_cast<bool>(sid));

        auto switchIt = sceneData.index.shapeToSwitch.find(sid);
        ASSERT_NE(switchIt, sceneData.index.shapeToSwitch.end());
        EXPECT_EQ(switchIt->second.get(), part.switchNode.get());
    }
}

TEST_F(AssemblySceneTest, SceneIndexReverseLookup)
{
    auto sceneData = buildAssemblyScene(assembly);
    ASSERT_FALSE(sceneData.parts.empty());

    for (const auto& [nodePtr, shapeId] : sceneData.index.nodeToShape)
    {
        EXPECT_TRUE(static_cast<bool>(shapeId));
        auto switchIt = sceneData.index.shapeToSwitch.find(shapeId);
        ASSERT_NE(switchIt, sceneData.index.shapeToSwitch.end());
        EXPECT_EQ(switchIt->second.get(), nodePtr);
    }
}

TEST_F(AssemblySceneTest, SceneIndexFaceCompleteness)
{
    auto sceneData = buildAssemblyScene(assembly);

    EXPECT_GT(sceneData.index.faceToTriangles.size(), 0u);
    EXPECT_EQ(sceneData.index.shapeFaces.size(), sceneData.parts.size());
}

TEST_F(AssemblySceneTest, FindFaceByTriangleValid)
{
    auto sceneData = buildAssemblyScene(assembly);
    ASSERT_FALSE(sceneData.index.shapeFaces.empty());

    for (const auto& [shapeId, faceEntries] : sceneData.index.shapeFaces)
    {
        if (faceEntries.empty()) continue;

        FaceId found = sceneData.index.findFaceByTriangle(shapeId, 0);
        EXPECT_TRUE(static_cast<bool>(found))
            << "findFaceByTriangle returned null FaceId for triangle 0";
        EXPECT_EQ(found, faceEntries.front().faceId);
        return; // one check is enough
    }
    FAIL() << "No parts with faces found";
}

TEST(AssemblySceneSimple, PickEmptyScene)
{
    AssemblyData empty;
    auto sceneData = buildAssemblyScene(empty);
    EXPECT_TRUE(sceneData.index.shapeToSwitch.empty());
    EXPECT_TRUE(sceneData.index.nodeToShape.empty());
}
