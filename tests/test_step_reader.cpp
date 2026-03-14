#include <gtest/gtest.h>

#include "test_helpers.h"

#include <set>
#include <vsgocct/ShapeId.h>
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

// --- ShapeId stability and index tests ---

TEST(StepReaderIds, IdCrossSessionStability)
{
    auto assembly1 = readStep(testDataPath("nested_assembly.step"));
    auto assembly2 = readStep(testDataPath("nested_assembly.step"));

    ASSERT_FALSE(assembly1.roots.empty());
    ASSERT_FALSE(assembly2.roots.empty());

    // Same file loaded twice must produce identical ShapeIds
    EXPECT_EQ(assembly1.roots.front().id, assembly2.roots.front().id);

    // Compare all Part IDs
    std::vector<vsgocct::ShapeId> ids1, ids2;
    std::function<void(const ShapeNode&, std::vector<vsgocct::ShapeId>&)> collectIds =
        [&](const ShapeNode& n, std::vector<vsgocct::ShapeId>& ids)
    {
        ids.push_back(n.id);
        for (const auto& child : n.children)
        {
            collectIds(child, ids);
        }
    };
    collectIds(assembly1.roots.front(), ids1);
    collectIds(assembly2.roots.front(), ids2);
    ASSERT_EQ(ids1.size(), ids2.size());
    for (std::size_t i = 0; i < ids1.size(); ++i)
    {
        EXPECT_EQ(ids1[i], ids2[i]);
    }
}

TEST(StepReaderIds, AssemblyPathFormat)
{
    auto assembly = readStep(testDataPath("nested_assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    // Root path starts with /
    EXPECT_EQ(root.assemblyPath[0], '/');
    // Root path has no double slashes
    EXPECT_EQ(root.assemblyPath.find("//"), std::string::npos);

    // All nodes have non-empty assemblyPath
    std::function<void(const ShapeNode&)> checkPaths = [&](const ShapeNode& n)
    {
        EXPECT_FALSE(n.assemblyPath.empty()) << "Node '" << n.name << "' has empty path";
        EXPECT_EQ(n.assemblyPath[0], '/') << "Path doesn't start with /: " << n.assemblyPath;
        for (const auto& child : n.children)
        {
            checkPaths(child);
            // Child path should start with parent path
            EXPECT_EQ(child.assemblyPath.substr(0, n.assemblyPath.size()), n.assemblyPath)
                << "Child path '" << child.assemblyPath
                << "' does not start with parent '" << n.assemblyPath << "'";
        }
    };
    checkPaths(root);
}

TEST(StepReaderIds, SiblingNameDisambiguation)
{
    auto assembly = readStep(testDataPath("shared_instances.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_EQ(root.type, ShapeNodeType::Assembly);

    // Shared instances: two children with same prototype name
    // They should get different paths (one plain, one with :2 suffix)
    std::vector<std::string> childPaths;
    for (const auto& child : root.children)
    {
        childPaths.push_back(child.assemblyPath);
    }

    // All paths must be unique
    std::set<std::string> uniquePaths(childPaths.begin(), childPaths.end());
    EXPECT_EQ(uniquePaths.size(), childPaths.size())
        << "Sibling paths are not unique";

    // All IDs must be unique
    std::set<std::uint64_t> uniqueIds;
    for (const auto& child : root.children)
    {
        uniqueIds.insert(child.id.value);
    }
    EXPECT_EQ(uniqueIds.size(), root.children.size())
        << "Sibling IDs are not unique";
}

TEST(StepReaderIds, ShapeIndexCompleteness)
{
    auto assembly = readStep(testDataPath("nested_assembly.step"));

    // Count all nodes in tree
    std::size_t nodeCount = 0;
    std::function<void(const ShapeNode&)> countNodes = [&](const ShapeNode& n)
    {
        ++nodeCount;
        for (const auto& child : n.children)
        {
            countNodes(child);
        }
    };
    for (const auto& root : assembly.roots)
    {
        countNodes(root);
    }

    // shapeIndex should contain all nodes
    EXPECT_EQ(assembly.shapeIndex.size(), nodeCount);

    // All pointers should be valid (dereferenceable)
    for (const auto& [id, ptr] : assembly.shapeIndex)
    {
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(ptr->id, id) << "shapeIndex entry points to wrong node";
    }
}
