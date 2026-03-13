#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>

// TopAbs_ShapeEnum and TopoDS_Shape methods (IsNull, ShapeType, NbChildren)
// are all inline/header-only, available transitively via StepReader.h -> TopoDS_Shape.hxx.
// No explicit OCCT includes needed — avoids linking against PRIVATE OCCT libraries.

using namespace vsgocct::cad;
using namespace vsgocct::test;

TEST(StepReader, ReadValidBox)
{
    auto data = readStep(testDataPath("box.step"));
    EXPECT_FALSE(data.shape.IsNull());
    EXPECT_EQ(data.shape.ShapeType(), TopAbs_SOLID);
}

TEST(StepReader, ReadAssembly)
{
    auto data = readStep(testDataPath("assembly.step"));
    EXPECT_FALSE(data.shape.IsNull());
    EXPECT_EQ(data.shape.ShapeType(), TopAbs_COMPOUND);

    // NbChildren() is inline in TopoDS_Shape.hxx — no OCCT library linkage needed
    EXPECT_GE(data.shape.NbChildren(), 2);
}

TEST(StepReader, ReadNonExistentFile)
{
    EXPECT_THROW(readStep(testDataPath("does_not_exist.step")), std::runtime_error);
}

TEST(StepReader, ReadInvalidFile)
{
    // test_helpers.h itself is not a STEP file
    EXPECT_THROW(readStep(testDataPath("../test_helpers.h")), std::runtime_error);
}

TEST(StepReader, ReadEmptyPath)
{
    EXPECT_THROW(readStep(""), std::runtime_error);
}
