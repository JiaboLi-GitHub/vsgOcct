#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>

#include <cmath>
#include <functional>

using namespace vsgocct::cad;
using namespace vsgocct::mesh;
using namespace vsgocct::test;

class ShapeMesherTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto assembly = readStep(testDataPath("box.step"));
        boxShape = assembly.roots.front().shape;
    }
    TopoDS_Shape boxShape;
};

TEST_F(ShapeMesherTest, TriangulateBox)
{
    auto result = triangulate(boxShape);
    EXPECT_GT(result.pointCount, 0u);
    EXPECT_GE(result.triangleCount, 12u); // box has 6 faces, at least 2 triangles each
}

TEST_F(ShapeMesherTest, TriangulateBoxHasNormals)
{
    auto result = triangulate(boxShape);
    EXPECT_EQ(result.faceNormals.size(), result.facePositions.size());
}

TEST_F(ShapeMesherTest, TriangulateBoxBounds)
{
    auto result = triangulate(boxShape);
    auto size = result.boundsMax - result.boundsMin;
    // box.step is 10x20x30, allow small tolerance
    EXPECT_NEAR(size.x, 10.0, 0.1);
    EXPECT_NEAR(size.y, 20.0, 0.1);
    EXPECT_NEAR(size.z, 30.0, 0.1);
}

TEST_F(ShapeMesherTest, TriangulateBoxEdges)
{
    auto result = triangulate(boxShape);
    EXPECT_GT(result.lineSegmentCount, 0u);
}

TEST_F(ShapeMesherTest, TriangulateAssembly)
{
    auto assembly = readStep(testDataPath("assembly.step"));
    // Collect all Part shapes and triangulate them individually
    std::size_t totalTriangles = 0;
    std::function<void(const ShapeNode&)> visitParts = [&](const ShapeNode& node)
    {
        if (node.type == ShapeNodeType::Part && !node.shape.IsNull())
        {
            auto result = triangulate(node.shape);
            totalTriangles += result.triangleCount;
        }
        for (const auto& child : node.children)
        {
            visitParts(child);
        }
    };
    for (const auto& root : assembly.roots)
    {
        visitParts(root);
    }
    // Assembly has more geometry than a single box
    auto boxResult = triangulate(boxShape);
    EXPECT_GT(totalTriangles, boxResult.triangleCount);
}

TEST_F(ShapeMesherTest, CustomMeshOptions)
{
    MeshOptions coarse;
    coarse.linearDeflection = 2.0;
    auto coarseResult = triangulate(boxShape, coarse);

    MeshOptions fine;
    fine.linearDeflection = 0.1;
    auto fineResult = triangulate(boxShape, fine);

    // Finer deflection should produce >= as many triangles
    EXPECT_GE(fineResult.triangleCount, coarseResult.triangleCount);
}

TEST_F(ShapeMesherTest, EmptyShape)
{
    TopoDS_Shape empty;
    EXPECT_ANY_THROW(triangulate(empty));
}

TEST_F(ShapeMesherTest, FaceRangesCountMatchesBRepFaces)
{
    auto result = triangulate(boxShape);
    // A box has 6 B-Rep faces
    EXPECT_EQ(result.faceRanges.size(), 6u);
}

TEST_F(ShapeMesherTest, FaceRangesTriangleSumConsistent)
{
    auto result = triangulate(boxShape);
    std::uint32_t sum = 0;
    for (const auto& range : result.faceRanges)
    {
        sum += range.triangleCount;
    }
    EXPECT_EQ(sum, static_cast<std::uint32_t>(result.triangleCount));
}
