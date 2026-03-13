#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/scene/SceneBuilder.h>

#include <cmath>

using namespace vsgocct::cad;
using namespace vsgocct::mesh;
using namespace vsgocct::scene;
using namespace vsgocct::test;

class SceneBuilderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto shape = readStep(testDataPath("box.step")).shape;
        meshResult = triangulate(shape);
    }
    MeshResult meshResult;
};

TEST_F(SceneBuilderTest, BuildSceneFromBox)
{
    auto sceneData = buildScene(meshResult);
    EXPECT_TRUE(sceneData.scene);
}

TEST_F(SceneBuilderTest, SwitchNodesExist)
{
    auto sceneData = buildScene(meshResult);
    EXPECT_TRUE(sceneData.pointSwitch);
    EXPECT_TRUE(sceneData.lineSwitch);
    EXPECT_TRUE(sceneData.faceSwitch);
}

TEST_F(SceneBuilderTest, ToggleVisibility)
{
    auto sceneData = buildScene(meshResult);

    // Default: all visible
    EXPECT_TRUE(sceneData.pointsVisible());
    EXPECT_TRUE(sceneData.linesVisible());
    EXPECT_TRUE(sceneData.facesVisible());

    // Toggle off
    sceneData.setPointsVisible(false);
    EXPECT_FALSE(sceneData.pointsVisible());

    sceneData.setLinesVisible(false);
    EXPECT_FALSE(sceneData.linesVisible());

    sceneData.setFacesVisible(false);
    EXPECT_FALSE(sceneData.facesVisible());

    // Toggle back on
    sceneData.setPointsVisible(true);
    EXPECT_TRUE(sceneData.pointsVisible());
}

TEST_F(SceneBuilderTest, SceneCenterAndRadius)
{
    auto sceneData = buildScene(meshResult);
    EXPECT_FALSE(std::isnan(sceneData.center.x));
    EXPECT_FALSE(std::isnan(sceneData.center.y));
    EXPECT_FALSE(std::isnan(sceneData.center.z));
    EXPECT_GT(sceneData.radius, 0.0);
}

TEST_F(SceneBuilderTest, EmptyMeshResult)
{
    MeshResult empty;
    // buildScene should handle empty input without crashing
    auto sceneData = buildScene(empty);
    EXPECT_TRUE(sceneData.scene);
}
