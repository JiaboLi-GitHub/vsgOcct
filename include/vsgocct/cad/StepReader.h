#pragma once

#include <filesystem>

#include <TopoDS_Shape.hxx>

namespace vsgocct::cad
{
struct ReaderOptions
{
};

struct ShapeData
{
    TopoDS_Shape shape;
};

ShapeData readStep(const std::filesystem::path& stepFile,
                   const ReaderOptions& options = {});
} // namespace vsgocct::cad
