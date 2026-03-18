// src/vsgocct/cad/StlReader.cpp
#include <vsgocct/cad/StlReader.h>

#include <RWStl.hxx>

#include <fstream>
#include <stdexcept>

namespace vsgocct::cad
{
namespace
{
std::string extractAsciiSolidName(const std::filesystem::path& stlFile)
{
    std::ifstream file(stlFile);
    if (!file)
    {
        return {};
    }

    std::string line;
    if (std::getline(file, line))
    {
        // ASCII STL starts with "solid <name>"
        const std::string prefix = "solid ";
        if (line.size() > prefix.size() &&
            line.compare(0, prefix.size(), prefix) == 0)
        {
            auto name = line.substr(prefix.size());
            // Trim trailing whitespace
            while (!name.empty() && (name.back() == ' ' || name.back() == '\r' || name.back() == '\n'))
            {
                name.pop_back();
            }
            if (!name.empty())
            {
                return name;
            }
        }
    }
    return {};
}
} // namespace

StlData readStl(const std::filesystem::path& stlFile)
{
    if (!std::filesystem::exists(stlFile))
    {
        throw std::runtime_error("STL file not found: " + stlFile.u8string());
    }

    // Use const char* overload which merges coincident vertices (M_PI/2 default)
    Handle(Poly_Triangulation) triangulation = RWStl::ReadFile(stlFile.string().c_str());
    if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
    {
        throw std::runtime_error("Failed to read STL file or file is empty: " + stlFile.u8string());
    }

    StlData data;
    data.triangulation = triangulation;

    // Try to extract name from ASCII header
    data.name = extractAsciiSolidName(stlFile);
    if (data.name.empty())
    {
        data.name = stlFile.stem().u8string();
    }

    return data;
}
} // namespace vsgocct::cad
