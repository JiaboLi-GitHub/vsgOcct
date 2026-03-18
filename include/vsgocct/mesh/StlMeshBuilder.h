// include/vsgocct/mesh/StlMeshBuilder.h
#pragma once

#include <Poly_Triangulation.hxx>
#include <Standard_Handle.hxx>

#include <vsgocct/mesh/ShapeMesher.h>

namespace vsgocct::mesh
{
struct StlMeshOptions
{
    double edgeAngleThreshold = 30.0;
    double weldTolerance = 0.0;
};

MeshResult buildStlMesh(const Handle(Poly_Triangulation)& triangulation,
                         const StlMeshOptions& options = {});
} // namespace vsgocct::mesh
