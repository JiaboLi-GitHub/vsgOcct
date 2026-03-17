#include <vsgocct/mesh/ShapeMesher.h>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace vsgocct::mesh
{
namespace
{
using IndexedShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

struct BoundsAccumulator
{
    vsg::dvec3 min = {
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    vsg::dvec3 max = {
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()};
};

struct PointBuffers
{
    std::vector<vsg::vec3> positions;
    std::size_t pointCount = 0;
};

struct LineBuffers
{
    std::vector<vsg::vec3> positions;
    std::size_t segmentCount = 0;
};

struct FaceBuffers
{
    std::vector<vsg::vec3> positions;
    std::vector<vsg::vec3> normals;
    std::size_t triangleCount = 0;
};

struct SceneBuffers
{
    PointBuffers points;
    LineBuffers lines;
    FaceBuffers faces;
    std::vector<PointSpan> pointSpans;
    std::vector<LineSpan> lineSpans;
    std::vector<FaceSpan> faceSpans;
    BoundsAccumulator bounds;

    bool hasGeometry() const
    {
        return pointCount() > 0 || lineSegmentCount() > 0 || triangleCount() > 0;
    }

    std::size_t pointCount() const { return points.pointCount; }
    std::size_t lineSegmentCount() const { return lines.segmentCount; }
    std::size_t triangleCount() const { return faces.triangleCount; }
};

vsg::vec3 toVec3(const gp_Pnt& point)
{
    return vsg::vec3(static_cast<float>(point.X()), static_cast<float>(point.Y()), static_cast<float>(point.Z()));
}

vsg::vec3 toVec3(const gp_Vec& vector)
{
    return vsg::vec3(static_cast<float>(vector.X()), static_cast<float>(vector.Y()), static_cast<float>(vector.Z()));
}

void updateBounds(BoundsAccumulator& bounds, const gp_Pnt& point)
{
    bounds.min.x = std::min(bounds.min.x, point.X());
    bounds.min.y = std::min(bounds.min.y, point.Y());
    bounds.min.z = std::min(bounds.min.z, point.Z());
    bounds.max.x = std::max(bounds.max.x, point.X());
    bounds.max.y = std::max(bounds.max.y, point.Y());
    bounds.max.z = std::max(bounds.max.z, point.Z());
}

void appendPoint(PointBuffers& buffers, BoundsAccumulator& bounds, const gp_Pnt& point)
{
    buffers.positions.push_back(toVec3(point));
    updateBounds(bounds, point);
    ++buffers.pointCount;
}

void appendLineSegment(LineBuffers& buffers, BoundsAccumulator& bounds, const gp_Pnt& start, const gp_Pnt& end)
{
    if (gp_Vec(start, end).SquareMagnitude() <= 1.0e-24)
    {
        return;
    }

    buffers.positions.push_back(toVec3(start));
    buffers.positions.push_back(toVec3(end));
    updateBounds(bounds, start);
    updateBounds(bounds, end);
    ++buffers.segmentCount;
}

template<class PointAccessor>
bool appendPolyline(LineBuffers& buffers, BoundsAccumulator& bounds, int pointCount, PointAccessor&& pointAccessor)
{
    if (pointCount < 2)
    {
        return false;
    }

    gp_Pnt previous = pointAccessor(1);
    bool appended = false;
    for (int pointIndex = 2; pointIndex <= pointCount; ++pointIndex)
    {
        const gp_Pnt current = pointAccessor(pointIndex);
        const std::size_t segmentCountBefore = buffers.segmentCount;
        appendLineSegment(buffers, bounds, previous, current);
        appended = appended || buffers.segmentCount != segmentCountBefore;
        previous = current;
    }

    return appended;
}

double computeLinearDeflection(const TopoDS_Shape& shape)
{
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds, false);
    if (bounds.IsVoid())
    {
        return 0.5;
    }

    double xmin = 0.0;
    double ymin = 0.0;
    double zmin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    double zmax = 0.0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    const double dx = xmax - xmin;
    const double dy = ymax - ymin;
    const double dz = zmax - zmin;
    const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (diagonal <= 0.0)
    {
        return 0.1;
    }

    return std::clamp(diagonal * 0.002, 0.01, 10.0);
}

void extractPoints(const TopoDS_Shape& shape, SceneBuffers& buffers)
{
    IndexedShapeMap vertexMap;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);

    for (int vertexIndex = 1; vertexIndex <= vertexMap.Extent(); ++vertexIndex)
    {
        const std::size_t pointsBefore = buffers.points.pointCount;
        const TopoDS_Vertex vertex = TopoDS::Vertex(vertexMap.FindKey(vertexIndex));
        appendPoint(buffers.points, buffers.bounds, BRep_Tool::Pnt(vertex));

        const std::size_t pointCount = buffers.points.pointCount - pointsBefore;
        if (pointCount > 0)
        {
            buffers.pointSpans.push_back(PointSpan{
                static_cast<uint32_t>(vertexIndex - 1),
                static_cast<uint32_t>(pointsBefore),
                static_cast<uint32_t>(pointCount)});
        }
    }
}

bool appendEdgeFromPolygon3D(const TopoDS_Edge& edge, SceneBuffers& buffers)
{
    TopLoc_Location location;
    const auto polygon = BRep_Tool::Polygon3D(edge, location);
    if (polygon.IsNull() || polygon->NbNodes() < 2)
    {
        return false;
    }

    const gp_Trsf transform = location.Transformation();
    return appendPolyline(
        buffers.lines,
        buffers.bounds,
        polygon->NbNodes(),
        [&](int pointIndex)
        {
            return polygon->Nodes().Value(pointIndex).Transformed(transform);
        });
}

bool appendEdgeFromPolygonOnTriangulation(const TopoDS_Edge& edge, SceneBuffers& buffers)
{
    occ::handle<Poly_PolygonOnTriangulation> polygon;
    occ::handle<Poly_Triangulation> triangulation;
    TopLoc_Location location;
    BRep_Tool::PolygonOnTriangulation(edge, polygon, triangulation, location);
    if (polygon.IsNull() || triangulation.IsNull() || polygon->NbNodes() < 2)
    {
        return false;
    }

    const gp_Trsf transform = location.Transformation();
    return appendPolyline(
        buffers.lines,
        buffers.bounds,
        polygon->NbNodes(),
        [&](int pointIndex)
        {
            return triangulation->Node(polygon->Node(pointIndex)).Transformed(transform);
        });
}

bool appendEdgeFromCurve(const TopoDS_Edge& edge, double linearDeflection, SceneBuffers& buffers)
{
    try
    {
        BRepAdaptor_Curve curve(edge);
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        if (!std::isfinite(first) || !std::isfinite(last) || std::abs(last - first) <= 1.0e-12)
        {
            return false;
        }

        GCPnts_QuasiUniformDeflection sampler(curve, linearDeflection, first, last);
        if (sampler.IsDone() && sampler.NbPoints() >= 2)
        {
            return appendPolyline(
                buffers.lines,
                buffers.bounds,
                sampler.NbPoints(),
                [&](int pointIndex)
                {
                    return sampler.Value(pointIndex);
                });
        }

        appendLineSegment(buffers.lines, buffers.bounds, curve.Value(first), curve.Value(last));
        return true;
    }
    catch (const Standard_Failure&)
    {
        return false;
    }
}

void extractLines(const TopoDS_Shape& shape, double linearDeflection, SceneBuffers& buffers)
{
    IndexedShapeMap edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);

    for (int edgeIndex = 1; edgeIndex <= edgeMap.Extent(); ++edgeIndex)
    {
        const std::size_t segmentCountBefore = buffers.lines.segmentCount;
        const TopoDS_Edge edge = TopoDS::Edge(edgeMap.FindKey(edgeIndex));
        if (BRep_Tool::Degenerated(edge))
        {
            continue;
        }

        if (appendEdgeFromPolygon3D(edge, buffers))
        {
            const std::size_t segmentCount = buffers.lines.segmentCount - segmentCountBefore;
            if (segmentCount > 0)
            {
                buffers.lineSpans.push_back(LineSpan{
                    static_cast<uint32_t>(edgeIndex - 1),
                    static_cast<uint32_t>(segmentCountBefore),
                    static_cast<uint32_t>(segmentCount)});
            }
            continue;
        }

        if (appendEdgeFromPolygonOnTriangulation(edge, buffers))
        {
            const std::size_t segmentCount = buffers.lines.segmentCount - segmentCountBefore;
            if (segmentCount > 0)
            {
                buffers.lineSpans.push_back(LineSpan{
                    static_cast<uint32_t>(edgeIndex - 1),
                    static_cast<uint32_t>(segmentCountBefore),
                    static_cast<uint32_t>(segmentCount)});
            }
            continue;
        }

        (void)appendEdgeFromCurve(edge, linearDeflection, buffers);
        const std::size_t segmentCount = buffers.lines.segmentCount - segmentCountBefore;
        if (segmentCount > 0)
        {
            buffers.lineSpans.push_back(LineSpan{
                static_cast<uint32_t>(edgeIndex - 1),
                static_cast<uint32_t>(segmentCountBefore),
                static_cast<uint32_t>(segmentCount)});
        }
    }
}

void extractFaces(const TopoDS_Shape& shape, SceneBuffers& buffers)
{
    IndexedShapeMap faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);

    for (int faceIndex = 1; faceIndex <= faceMap.Extent(); ++faceIndex)
    {
        const std::size_t triangleCountBefore = buffers.faces.triangleCount;
        const TopoDS_Face face = TopoDS::Face(faceMap.FindKey(faceIndex));

        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
        {
            continue;
        }

        const gp_Trsf transform = location.Transformation();
        const bool isReversed = face.Orientation() == TopAbs_REVERSED;

        for (int triangleIndex = 1; triangleIndex <= triangulation->NbTriangles(); ++triangleIndex)
        {
            int index1 = 0;
            int index2 = 0;
            int index3 = 0;
            triangulation->Triangle(triangleIndex).Get(index1, index2, index3);

            const std::array<int, 3> nodeIndices = isReversed
                                                       ? std::array<int, 3>{index1, index3, index2}
                                                       : std::array<int, 3>{index1, index2, index3};

            std::array<gp_Pnt, 3> points = {
                triangulation->Node(nodeIndices[0]).Transformed(transform),
                triangulation->Node(nodeIndices[1]).Transformed(transform),
                triangulation->Node(nodeIndices[2]).Transformed(transform)};

            gp_Vec faceNormal = gp_Vec(points[0], points[1]).Crossed(gp_Vec(points[0], points[2]));
            if (faceNormal.SquareMagnitude() <= 1.0e-24)
            {
                continue;
            }
            faceNormal.Normalize();

            ++buffers.faces.triangleCount;

            for (std::size_t vertexIndex = 0; vertexIndex < points.size(); ++vertexIndex)
            {
                buffers.faces.positions.push_back(toVec3(points[vertexIndex]));
                updateBounds(buffers.bounds, points[vertexIndex]);

                gp_Vec normal = faceNormal;
                if (triangulation->HasNormals())
                {
                    normal = gp_Vec(triangulation->Normal(nodeIndices[vertexIndex]));
                    normal.Transform(transform);
                    if (isReversed)
                    {
                        normal.Reverse();
                    }

                    if (normal.SquareMagnitude() <= 1.0e-24)
                    {
                        normal = faceNormal;
                    }
                    else
                    {
                        normal.Normalize();
                    }
                }

                buffers.faces.normals.push_back(toVec3(normal));
            }
        }

        const std::size_t triangleCount = buffers.faces.triangleCount - triangleCountBefore;
        if (triangleCount > 0)
        {
            buffers.faceSpans.push_back(FaceSpan{
                static_cast<uint32_t>(faceIndex - 1),
                static_cast<uint32_t>(triangleCountBefore),
                static_cast<uint32_t>(triangleCount)});
        }
    }
}
} // namespace

MeshResult triangulate(const TopoDS_Shape& shape, const MeshOptions& options)
{
    const double linearDeflection = options.linearDeflection > 0.0
                                        ? options.linearDeflection
                                        : computeLinearDeflection(shape);
    BRepMesh_IncrementalMesh meshGenerator(shape, linearDeflection, options.relative, options.angularDeflection, true);
    (void)meshGenerator;

    SceneBuffers buffers;
    extractPoints(shape, buffers);
    extractLines(shape, linearDeflection, buffers);
    extractFaces(shape, buffers);

    if (!buffers.hasGeometry())
    {
        throw std::runtime_error("No renderable points, lines, or faces were produced from the STEP model.");
    }

    MeshResult result;
    result.pointPositions = std::move(buffers.points.positions);
    result.pointSpans = std::move(buffers.pointSpans);
    result.pointCount = buffers.points.pointCount;
    result.linePositions = std::move(buffers.lines.positions);
    result.lineSpans = std::move(buffers.lineSpans);
    result.lineSegmentCount = buffers.lines.segmentCount;
    result.facePositions = std::move(buffers.faces.positions);
    result.faceNormals = std::move(buffers.faces.normals);
    result.faceSpans = std::move(buffers.faceSpans);
    result.triangleCount = buffers.faces.triangleCount;
    result.boundsMin = buffers.bounds.min;
    result.boundsMax = buffers.bounds.max;
    return result;
}
} // namespace vsgocct::mesh
