#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>
#include <vsgocct/selection/ScenePicker.h>

#include <algorithm>

using namespace vsgocct::cad;
using namespace vsgocct::scene;
using namespace vsgocct::selection;
using namespace vsgocct::test;

namespace
{
vsg::ref_ptr<vsg::Camera> createTestCamera(const AssemblySceneData& sceneData)
{
    const auto radius = std::max(sceneData.radius, 1.0);
    auto lookAt = vsg::LookAt::create(
        sceneData.center + vsg::dvec3(0.0, -radius * 4.0, radius * 0.2),
        sceneData.center,
        vsg::dvec3(0.0, 0.0, 1.0));
    auto projection = vsg::Perspective::create(35.0, 1.0, 0.1, radius * 20.0);
    return vsg::Camera::create(projection, lookAt, vsg::ViewportState::create(VkExtent2D{400, 400}));
}
} // namespace

TEST(ScenePicker, CenterPickHitsFace)
{
    auto assembly = readStep(testDataPath("box.step"));
    auto sceneData = buildAssemblyScene(assembly);
    auto camera = createTestCamera(sceneData);

    auto pickResult = pick(*camera, sceneData, 200, 200);
    ASSERT_TRUE(pickResult.has_value());
    EXPECT_EQ(pickResult->token.partId, 0u);
    EXPECT_EQ(pickResult->token.kind, PrimitiveKind::Face);
    EXPECT_NE(pickResult->token.primitiveId, InvalidSelectionId);
}

TEST(ScenePicker, HiddenPartIsNotPickable)
{
    auto assembly = readStep(testDataPath("box.step"));
    auto sceneData = buildAssemblyScene(assembly);
    ASSERT_FALSE(sceneData.parts.empty());
    ASSERT_TRUE(sceneData.parts.front().switchNode);
    ASSERT_FALSE(sceneData.parts.front().switchNode->children.empty());

    sceneData.parts.front().switchNode->children.front().mask = vsg::MASK_OFF;

    auto camera = createTestCamera(sceneData);
    auto pickResult = pick(*camera, sceneData, 200, 200);
    EXPECT_FALSE(pickResult.has_value());
}
