#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>

#include <cmath>

using namespace vsgocct::cad;
using namespace vsgocct::mesh;
using namespace vsgocct::test;

class ShapeMesherTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        boxShape = readStep(testDataPath("box.step")).shape;
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
    auto assemblyShape = readStep(testDataPath("assembly.step")).shape;
    auto result = triangulate(assemblyShape);
    // Assembly has more geometry than a single box
    auto boxResult = triangulate(boxShape);
    EXPECT_GT(result.triangleCount, boxResult.triangleCount);
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
