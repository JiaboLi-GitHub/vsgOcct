// tests/test_stl_mesh_builder.cpp
#include <gtest/gtest.h>
#include <vsgocct/cad/StlReader.h>
#include <vsgocct/mesh/StlMeshBuilder.h>
#include <filesystem>

static std::filesystem::path testDataDir()
{
    return std::filesystem::path(TEST_DATA_DIR);
}

TEST(StlMeshBuilder, CubeProduces12FaceSpans)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    EXPECT_EQ(mesh.triangleCount, 12u);
    EXPECT_EQ(mesh.faceSpans.size(), 12u);
    // Each triangle = 3 vertices in facePositions
    EXPECT_EQ(mesh.facePositions.size(), 36u);
    EXPECT_EQ(mesh.faceNormals.size(), 36u);
}

TEST(StlMeshBuilder, CubeHas12FeatureEdges)
{
    // A cube has 12 edges, all with 90-degree dihedral angles
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    // 12 geometric edges of a cube
    EXPECT_EQ(mesh.lineSpans.size(), 12u);
    EXPECT_GT(mesh.lineSegmentCount, 0u);
}

TEST(StlMeshBuilder, CubeHas8FeatureVertices)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    EXPECT_EQ(mesh.pointSpans.size(), 8u);
    EXPECT_EQ(mesh.pointCount, 8u);
}

TEST(StlMeshBuilder, BoundsAreValid)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    // Cube is 10x10x10 at origin
    EXPECT_NEAR(mesh.boundsMin.x, 0.0, 0.01);
    EXPECT_NEAR(mesh.boundsMin.y, 0.0, 0.01);
    EXPECT_NEAR(mesh.boundsMin.z, 0.0, 0.01);
    EXPECT_NEAR(mesh.boundsMax.x, 10.0, 0.01);
    EXPECT_NEAR(mesh.boundsMax.y, 10.0, 0.01);
    EXPECT_NEAR(mesh.boundsMax.z, 10.0, 0.01);
}

TEST(StlMeshBuilder, HasGeometryIsTrue)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    EXPECT_TRUE(mesh.hasGeometry());
}

TEST(StlMeshBuilder, HighAngleThresholdReducesEdges)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    vsgocct::mesh::StlMeshOptions options;
    options.edgeAngleThreshold = 100.0; // Higher than 90 degrees
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation, options);

    // With threshold > 90, cube edges should NOT be detected as feature edges
    // Only boundary edges remain (cube has none since it's closed)
    EXPECT_EQ(mesh.lineSpans.size(), 0u);
}
