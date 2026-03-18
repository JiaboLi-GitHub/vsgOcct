// include/vsgocct/cad/StlReader.h
#pragma once

#include <filesystem>
#include <string>

#include <Poly_Triangulation.hxx>
#include <Standard_Handle.hxx>

namespace vsgocct::cad
{
struct StlData
{
    Handle(Poly_Triangulation) triangulation;
    std::string name;
};

StlData readStl(const std::filesystem::path& stlFile);
} // namespace vsgocct::cad
