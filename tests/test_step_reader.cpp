#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>

using namespace vsgocct::cad;
using namespace vsgocct::test;

// --- Error handling tests (unchanged behavior) ---

TEST(StepReader, ReadNonExistentFile)
{
    EXPECT_THROW(readStep(testDataPath("does_not_exist.step")), std::runtime_error);
}

TEST(StepReader, ReadInvalidFile)
{
    EXPECT_THROW(readStep(testDataPath("../test_helpers.h")), std::runtime_error);
}

TEST(StepReader, ReadEmptyPath)
{
    EXPECT_THROW(readStep(""), std::runtime_error);
}

// --- Single part STEP (plain, non-XCAF) ---

TEST(StepReader, SinglePartBox)
{
    auto assembly = readStep(testDataPath("box.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    // Plain STEP produces a single Part node (no XCAF assembly metadata)
    EXPECT_EQ(root.type, ShapeNodeType::Part);
    EXPECT_FALSE(root.shape.IsNull());
}

// --- Plain assembly (compound, non-XCAF) ---

TEST(StepReader, PlainAssemblyBackwardCompat)
{
    auto assembly = readStep(testDataPath("assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());

    // Plain compound should still produce valid tree with at least one Part
    std::size_t partCount = 0;
    std::function<void(const ShapeNode&)> countParts = [&](const ShapeNode& n)
    {
        if (n.type == ShapeNodeType::Part)
        {
            ++partCount;
            EXPECT_FALSE(n.shape.IsNull());
        }
        for (const auto& child : n.children)
        {
            countParts(child);
        }
    };
    for (const auto& root : assembly.roots)
    {
        countParts(root);
    }
    EXPECT_GE(partCount, 1u);
}

// --- Colored XCAF STEP ---

TEST(StepReader, ColoredBoxHasColor)
{
    auto assembly = readStep(testDataPath("colored_box.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_EQ(root.type, ShapeNodeType::Part);
    EXPECT_FALSE(root.shape.IsNull());
    EXPECT_TRUE(root.color.isSet);
    // Red color: (1.0, 0.0, 0.0)
    EXPECT_NEAR(root.color.r, 1.0f, 0.01f);
    EXPECT_NEAR(root.color.g, 0.0f, 0.01f);
    EXPECT_NEAR(root.color.b, 0.0f, 0.01f);
}

TEST(StepReader, ColoredBoxHasName)
{
    auto assembly = readStep(testDataPath("colored_box.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_FALSE(root.name.empty());
    // Name was set to "RedBox" in generate_test_data
    EXPECT_NE(root.name.find("Box"), std::string::npos);
}

// --- Nested XCAF assembly ---

TEST(StepReader, NestedAssemblyStructure)
{
    auto assembly = readStep(testDataPath("nested_assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_EQ(root.type, ShapeNodeType::Assembly);
    EXPECT_FALSE(root.children.empty());

    // Root should contain SubAssembly which contains Box + Cylinder
    // Navigate to find at least 2 Part leaf nodes
    std::size_t partCount = 0;
    std::function<void(const ShapeNode&)> countParts = [&](const ShapeNode& n)
    {
        if (n.type == ShapeNodeType::Part)
        {
            ++partCount;
            EXPECT_FALSE(n.shape.IsNull());
        }
        for (const auto& child : n.children)
        {
            countParts(child);
        }
    };
    countParts(root);
    EXPECT_GE(partCount, 2u);
}

TEST(StepReader, NestedAssemblyColors)
{
    auto assembly = readStep(testDataPath("nested_assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());

    // Collect all Part node colors
    std::vector<ShapeNodeColor> partColors;
    std::function<void(const ShapeNode&)> collectColors = [&](const ShapeNode& n)
    {
        if (n.type == ShapeNodeType::Part)
        {
            partColors.push_back(n.color);
        }
        for (const auto& child : n.children)
        {
            collectColors(child);
        }
    };
    collectColors(assembly.roots.front());

    ASSERT_GE(partColors.size(), 2u);
    // At least some parts should have color set (from prototype or inheritance)
    bool anyColorSet = false;
    for (const auto& c : partColors)
    {
        if (c.isSet) anyColorSet = true;
    }
    EXPECT_TRUE(anyColorSet);
}

TEST(StepReader, NestedAssemblyHasNonIdentityLocation)
{
    auto assembly = readStep(testDataPath("nested_assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    // Root assembly's children (the SubAssembly component) should have a non-identity location
    bool foundNonIdentityLocation = false;
    std::function<void(const ShapeNode&)> checkLocations = [&](const ShapeNode& n)
    {
        if (!n.location.IsIdentity())
        {
            foundNonIdentityLocation = true;
        }
        for (const auto& child : n.children)
        {
            checkLocations(child);
        }
    };
    checkLocations(root);
    EXPECT_TRUE(foundNonIdentityLocation);
}

// --- Shared instances ---

TEST(StepReader, SharedInstancesProduceSeparateNodes)
{
    auto assembly = readStep(testDataPath("shared_instances.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_EQ(root.type, ShapeNodeType::Assembly);

    // Should have 2 Part children (same prototype, different instances)
    std::size_t partCount = 0;
    std::function<void(const ShapeNode&)> countParts = [&](const ShapeNode& n)
    {
        if (n.type == ShapeNodeType::Part) ++partCount;
        for (const auto& child : n.children) countParts(child);
    };
    countParts(root);
    EXPECT_EQ(partCount, 2u);
}
