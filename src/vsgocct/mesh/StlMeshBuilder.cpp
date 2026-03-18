// src/vsgocct/mesh/StlMeshBuilder.cpp
#include <vsgocct/mesh/StlMeshBuilder.h>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace vsgocct::mesh
{
namespace
{
constexpr double PI = 3.14159265358979323846;

struct Vec3Hash
{
    std::size_t operator()(const std::array<int64_t, 3>& v) const
    {
        std::size_t h = 0;
        for (auto val : v)
        {
            h ^= std::hash<int64_t>{}(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

using EdgeKey = std::pair<uint32_t, uint32_t>;

struct EdgeInfo
{
    uint32_t face1 = UINT32_MAX;
    uint32_t face2 = UINT32_MAX;
    // Original vertex indices for the edge endpoints (from face1)
    int origV0 = 0;
    int origV1 = 0;
};

std::vector<uint32_t> weldVertices(
    const Handle(Poly_Triangulation)& tri,
    double tolerance,
    vsg::dvec3& boundsMin,
    vsg::dvec3& boundsMax)
{
    const int nbNodes = tri->NbNodes();
    std::vector<uint32_t> remap(nbNodes + 1); // 1-indexed

    // First pass: compute bounds
    boundsMin = vsg::dvec3(
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max());
    boundsMax = vsg::dvec3(
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest());

    for (int i = 1; i <= nbNodes; ++i)
    {
        const gp_Pnt& p = tri->Node(i);
        boundsMin.x = std::min(boundsMin.x, p.X());
        boundsMin.y = std::min(boundsMin.y, p.Y());
        boundsMin.z = std::min(boundsMin.z, p.Z());
        boundsMax.x = std::max(boundsMax.x, p.X());
        boundsMax.y = std::max(boundsMax.y, p.Y());
        boundsMax.z = std::max(boundsMax.z, p.Z());
    }

    // Auto tolerance
    if (tolerance <= 0.0)
    {
        const double dx = boundsMax.x - boundsMin.x;
        const double dy = boundsMax.y - boundsMin.y;
        const double dz = boundsMax.z - boundsMin.z;
        const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
        tolerance = diagonal * 1.0e-6;
        if (tolerance <= 0.0)
        {
            tolerance = 1.0e-10;
        }
    }

    const double invTol = 1.0 / tolerance;

    // Spatial hash: quantize positions
    std::unordered_map<std::array<int64_t, 3>, uint32_t, Vec3Hash> grid;
    uint32_t nextWeldedId = 0;

    for (int i = 1; i <= nbNodes; ++i)
    {
        const gp_Pnt& p = tri->Node(i);
        std::array<int64_t, 3> key{
            static_cast<int64_t>(std::round(p.X() * invTol)),
            static_cast<int64_t>(std::round(p.Y() * invTol)),
            static_cast<int64_t>(std::round(p.Z() * invTol))};

        auto it = grid.find(key);
        if (it == grid.end())
        {
            grid[key] = nextWeldedId;
            remap[i] = nextWeldedId;
            ++nextWeldedId;
        }
        else
        {
            remap[i] = it->second;
        }
    }

    return remap;
}

EdgeKey makeEdgeKey(uint32_t v0, uint32_t v1)
{
    return v0 < v1 ? EdgeKey{v0, v1} : EdgeKey{v1, v0};
}

gp_Vec computeFaceNormal(const Handle(Poly_Triangulation)& tri, int triIndex)
{
    int n1 = 0, n2 = 0, n3 = 0;
    tri->Triangle(triIndex).Get(n1, n2, n3);
    const gp_Pnt& p1 = tri->Node(n1);
    const gp_Pnt& p2 = tri->Node(n2);
    const gp_Pnt& p3 = tri->Node(n3);
    gp_Vec v1(p1, p2);
    gp_Vec v2(p1, p3);
    gp_Vec normal = v1.Crossed(v2);
    if (normal.SquareMagnitude() > 1.0e-24)
    {
        normal.Normalize();
    }
    return normal;
}

} // namespace

MeshResult buildStlMesh(const Handle(Poly_Triangulation)& triangulation,
                         const StlMeshOptions& options)
{
    if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
    {
        throw std::runtime_error("Empty triangulation passed to buildStlMesh");
    }

    const int nbTriangles = triangulation->NbTriangles();

    // Step 0: Weld vertices
    vsg::dvec3 boundsMin, boundsMax;
    auto weldRemap = weldVertices(triangulation, options.weldTolerance, boundsMin, boundsMax);

    // Step 1: Build half-edge map and extract face data simultaneously
    std::map<EdgeKey, EdgeInfo> edgeMap;

    MeshResult result;
    result.boundsMin = boundsMin;
    result.boundsMax = boundsMax;

    for (int triIdx = 1; triIdx <= nbTriangles; ++triIdx)
    {
        int n1 = 0, n2 = 0, n3 = 0;
        triangulation->Triangle(triIdx).Get(n1, n2, n3);

        const gp_Pnt& p1 = triangulation->Node(n1);
        const gp_Pnt& p2 = triangulation->Node(n2);
        const gp_Pnt& p3 = triangulation->Node(n3);

        // Compute face normal
        gp_Vec faceNormal = gp_Vec(p1, p2).Crossed(gp_Vec(p1, p3));
        if (faceNormal.SquareMagnitude() > 1.0e-24)
        {
            faceNormal.Normalize();
        }

        vsg::vec3 normal(
            static_cast<float>(faceNormal.X()),
            static_cast<float>(faceNormal.Y()),
            static_cast<float>(faceNormal.Z()));

        // Add face positions and normals (3 vertices per triangle, flat normal)
        const uint32_t firstTriangle = static_cast<uint32_t>(result.triangleCount);
        result.facePositions.push_back(vsg::vec3(
            static_cast<float>(p1.X()), static_cast<float>(p1.Y()), static_cast<float>(p1.Z())));
        result.facePositions.push_back(vsg::vec3(
            static_cast<float>(p2.X()), static_cast<float>(p2.Y()), static_cast<float>(p2.Z())));
        result.facePositions.push_back(vsg::vec3(
            static_cast<float>(p3.X()), static_cast<float>(p3.Y()), static_cast<float>(p3.Z())));
        result.faceNormals.push_back(normal);
        result.faceNormals.push_back(normal);
        result.faceNormals.push_back(normal);
        ++result.triangleCount;

        result.faceSpans.push_back(FaceSpan{
            static_cast<uint32_t>(triIdx - 1),
            firstTriangle,
            1u});

        // Register edges in half-edge map using welded indices
        const uint32_t faceIndex = static_cast<uint32_t>(triIdx - 1);
        const uint32_t wv1 = weldRemap[n1];
        const uint32_t wv2 = weldRemap[n2];
        const uint32_t wv3 = weldRemap[n3];

        std::array<std::pair<uint32_t, uint32_t>, 3> edges = {{
            {wv1, wv2}, {wv2, wv3}, {wv3, wv1}}};
        // Original (unwelded) vertex indices for edge endpoint positions
        std::array<std::pair<int, int>, 3> origEdges = {{
            {n1, n2}, {n2, n3}, {n3, n1}}};

        for (int e = 0; e < 3; ++e)
        {
            if (edges[e].first == edges[e].second)
            {
                continue; // Degenerate edge after welding
            }

            auto key = makeEdgeKey(edges[e].first, edges[e].second);
            auto it = edgeMap.find(key);
            if (it == edgeMap.end())
            {
                EdgeInfo info;
                info.face1 = faceIndex;
                info.origV0 = origEdges[e].first;
                info.origV1 = origEdges[e].second;
                edgeMap[key] = info;
            }
            else if (it->second.face2 == UINT32_MAX)
            {
                it->second.face2 = faceIndex;
            }
            // else: non-manifold edge (shared by > 2 faces), ignore
        }
    }

    // Step 2: Classify edges and extract feature edges
    const double angleThresholdRad = options.edgeAngleThreshold * PI / 180.0;
    std::set<uint32_t> featureVertexSet;
    uint32_t edgeId = 0;

    for (const auto& [key, info] : edgeMap)
    {
        bool isFeature = false;

        if (info.face2 == UINT32_MAX)
        {
            // Boundary edge
            isFeature = true;
        }
        else
        {
            // Compute dihedral angle between the two faces
            gp_Vec n1 = computeFaceNormal(triangulation, static_cast<int>(info.face1) + 1);
            gp_Vec n2 = computeFaceNormal(triangulation, static_cast<int>(info.face2) + 1);

            double dotProduct = n1.Dot(n2);
            dotProduct = std::clamp(dotProduct, -1.0, 1.0);
            double angle = std::acos(dotProduct);

            if (angle > angleThresholdRad)
            {
                isFeature = true;
            }
        }

        if (isFeature)
        {
            const gp_Pnt& pA = triangulation->Node(info.origV0);
            const gp_Pnt& pB = triangulation->Node(info.origV1);

            const uint32_t firstSegment = static_cast<uint32_t>(result.lineSegmentCount);
            result.linePositions.push_back(vsg::vec3(
                static_cast<float>(pA.X()), static_cast<float>(pA.Y()), static_cast<float>(pA.Z())));
            result.linePositions.push_back(vsg::vec3(
                static_cast<float>(pB.X()), static_cast<float>(pB.Y()), static_cast<float>(pB.Z())));
            ++result.lineSegmentCount;

            result.lineSpans.push_back(LineSpan{edgeId, firstSegment, 1u});
            ++edgeId;

            featureVertexSet.insert(key.first);
            featureVertexSet.insert(key.second);
        }
    }

    // Step 3: Extract feature vertices
    // Need to map welded vertex IDs back to positions
    // Build a map of weldedId -> position (use first occurrence)
    std::unordered_map<uint32_t, gp_Pnt> weldedPositions;
    for (int i = 1; i <= triangulation->NbNodes(); ++i)
    {
        uint32_t wid = weldRemap[i];
        if (weldedPositions.find(wid) == weldedPositions.end())
        {
            weldedPositions[wid] = triangulation->Node(i);
        }
    }

    uint32_t vertexId = 0;
    for (uint32_t weldedIdx : featureVertexSet)
    {
        auto it = weldedPositions.find(weldedIdx);
        if (it == weldedPositions.end())
        {
            continue;
        }

        const gp_Pnt& p = it->second;
        const uint32_t firstPoint = static_cast<uint32_t>(result.pointCount);
        result.pointPositions.push_back(vsg::vec3(
            static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z())));
        ++result.pointCount;

        result.pointSpans.push_back(PointSpan{vertexId, firstPoint, 1u});
        ++vertexId;
    }

    return result;
}
} // namespace vsgocct::mesh
