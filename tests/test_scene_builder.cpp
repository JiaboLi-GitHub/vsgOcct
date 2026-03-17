#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>

#include <cmath>

using namespace vsgocct::cad;
using namespace vsgocct::scene;
using namespace vsgocct::test;

namespace
{
bool sameColor(const vsg::vec3& lhs, const vsg::vec3& rhs)
{
    return std::fabs(lhs.r - rhs.r) < 1e-6f &&
           std::fabs(lhs.g - rhs.g) < 1e-6f &&
           std::fabs(lhs.b - rhs.b) < 1e-6f;
}

void expectColorRange(const vsg::ref_ptr<vsg::vec3Array>& colors,
                      uint32_t firstIndex,
                      uint32_t count,
                      const vsg::vec3& expected)
{
    ASSERT_TRUE(colors);
    ASSERT_LE(static_cast<std::size_t>(firstIndex + count), colors->size());

    for (uint32_t index = firstIndex; index < firstIndex + count; ++index)
    {
        EXPECT_FLOAT_EQ((*colors)[index].r, expected.r);
        EXPECT_FLOAT_EQ((*colors)[index].g, expected.g);
        EXPECT_FLOAT_EQ((*colors)[index].b, expected.b);
    }
}
} // namespace

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

TEST_F(AssemblySceneTest, PartsHaveStableIds)
{
    auto sceneData = buildAssemblyScene(assembly);

    for (std::size_t index = 0; index < sceneData.parts.size(); ++index)
    {
        EXPECT_EQ(sceneData.parts[index].partId, static_cast<uint32_t>(index));
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

TEST(AssemblySceneSimple, SelectionHighlightUpdatesFaceColors)
{
    auto assembly = readStep(testDataPath("colored_box.step"));
    auto sceneData = buildAssemblyScene(assembly);
    ASSERT_FALSE(sceneData.parts.empty());

    auto& part = sceneData.parts.front();
    ASSERT_TRUE(part.faceColors);
    ASSERT_GT(part.faceColors->size(), 0u);

    const auto originalColor = (*part.faceColors)[0];

    EXPECT_TRUE(setSelectedPart(sceneData, part.partId));
    EXPECT_EQ(sceneData.selectedPartId, part.partId);
    const auto selectedColor = (*part.faceColors)[0];
    EXPECT_TRUE(selectedColor.r != originalColor.r ||
                selectedColor.g != originalColor.g ||
                selectedColor.b != originalColor.b);

    clearSelectedPart(sceneData);
    EXPECT_EQ(sceneData.selectedPartId, InvalidPartId);
    EXPECT_FLOAT_EQ((*part.faceColors)[0].r, originalColor.r);
    EXPECT_FLOAT_EQ((*part.faceColors)[0].g, originalColor.g);
    EXPECT_FLOAT_EQ((*part.faceColors)[0].b, originalColor.b);
}

TEST(AssemblySceneSimple, PrimitiveSelectionTransitionsAcrossKinds)
{
    auto assembly = readStep(testDataPath("box.step"));
    auto sceneData = buildAssemblyScene(assembly);
    ASSERT_FALSE(sceneData.parts.empty());

    auto& part = sceneData.parts.front();
    ASSERT_TRUE(part.faceColors);
    ASSERT_TRUE(part.lineColors);
    ASSERT_TRUE(part.pointColors);
    ASSERT_GE(part.faceSpans.size(), 2u);
    ASSERT_GE(part.lineSpans.size(), 2u);
    ASSERT_GE(part.pointSpans.size(), 2u);

    const auto baseFaceColor = (*part.faceColors)[0];
    const auto baseLineColor = (*part.lineColors)[0];
    const auto basePointColor = (*part.pointColors)[0];

    const auto faceSpan = part.faceSpans.front();
    const auto otherFaceSpan = part.faceSpans.back();
    vsgocct::selection::SelectionToken faceToken;
    faceToken.partId = part.partId;
    faceToken.kind = vsgocct::selection::PrimitiveKind::Face;
    faceToken.primitiveId = faceSpan.faceId;

    ASSERT_TRUE(setSelection(sceneData, faceToken));
    EXPECT_EQ(sceneData.selectedPartId, part.partId);
    EXPECT_EQ(sceneData.selectedToken.kind, vsgocct::selection::PrimitiveKind::Face);

    const auto selectedFaceColor = (*part.faceColors)[faceSpan.firstTriangle * 3u];
    EXPECT_FALSE(sameColor(selectedFaceColor, baseFaceColor));
    expectColorRange(part.faceColors, faceSpan.firstTriangle * 3u, faceSpan.triangleCount * 3u, selectedFaceColor);
    EXPECT_TRUE(sameColor((*part.faceColors)[otherFaceSpan.firstTriangle * 3u], baseFaceColor));

    const auto edgeSpan = part.lineSpans.front();
    const auto otherEdgeSpan = part.lineSpans.back();
    vsgocct::selection::SelectionToken edgeToken;
    edgeToken.partId = part.partId;
    edgeToken.kind = vsgocct::selection::PrimitiveKind::Edge;
    edgeToken.primitiveId = edgeSpan.edgeId;

    ASSERT_TRUE(setSelection(sceneData, edgeToken));
    EXPECT_EQ(sceneData.selectedToken.kind, vsgocct::selection::PrimitiveKind::Edge);
    EXPECT_TRUE(sameColor((*part.faceColors)[faceSpan.firstTriangle * 3u], baseFaceColor));

    const auto selectedEdgeColor = (*part.lineColors)[edgeSpan.firstSegment * 2u];
    EXPECT_FALSE(sameColor(selectedEdgeColor, baseLineColor));
    expectColorRange(part.lineColors, edgeSpan.firstSegment * 2u, edgeSpan.segmentCount * 2u, selectedEdgeColor);
    EXPECT_TRUE(sameColor((*part.lineColors)[otherEdgeSpan.firstSegment * 2u], baseLineColor));

    const auto pointSpan = part.pointSpans.front();
    const auto otherPointSpan = part.pointSpans.back();
    vsgocct::selection::SelectionToken pointToken;
    pointToken.partId = part.partId;
    pointToken.kind = vsgocct::selection::PrimitiveKind::Vertex;
    pointToken.primitiveId = pointSpan.vertexId;

    ASSERT_TRUE(setSelection(sceneData, pointToken));
    EXPECT_EQ(sceneData.selectedToken.kind, vsgocct::selection::PrimitiveKind::Vertex);
    EXPECT_TRUE(sameColor((*part.lineColors)[edgeSpan.firstSegment * 2u], baseLineColor));

    const auto selectedPointColor = (*part.pointColors)[pointSpan.firstPoint];
    EXPECT_FALSE(sameColor(selectedPointColor, basePointColor));
    expectColorRange(part.pointColors, pointSpan.firstPoint, pointSpan.pointCount, selectedPointColor);
    EXPECT_TRUE(sameColor((*part.pointColors)[otherPointSpan.firstPoint], basePointColor));

    clearSelection(sceneData);
    EXPECT_EQ(sceneData.selectedPartId, InvalidPartId);
    EXPECT_EQ(sceneData.selectedToken.kind, vsgocct::selection::PrimitiveKind::None);
    EXPECT_TRUE(sameColor((*part.pointColors)[pointSpan.firstPoint], basePointColor));
}
