#include <vsgocct/cad/StepReader.h>

#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>

#include <fstream>
#include <stdexcept>
#include <string>

namespace vsgocct::cad
{
namespace
{
TopoDS_Shape readStepShape(const std::filesystem::path& stepFile)
{
    std::ifstream input(stepFile, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open STEP file: " + stepFile.u8string());
    }

    STEPControl_Reader reader;
    const std::string displayName = stepFile.filename().u8string();
    const auto status = reader.ReadStream(displayName.c_str(), input);
    if (status != IFSelect_RetDone)
    {
        throw std::runtime_error("OCCT failed to read STEP data from: " + stepFile.u8string());
    }

    if (reader.TransferRoots() <= 0)
    {
        throw std::runtime_error("OCCT did not transfer any root shape from: " + stepFile.u8string());
    }

    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull())
    {
        throw std::runtime_error("Transferred STEP shape is empty: " + stepFile.u8string());
    }

    return shape;
}
} // namespace

ShapeData readStep(const std::filesystem::path& stepFile, const ReaderOptions& /*options*/)
{
    ShapeData data;
    data.shape = readStepShape(stepFile);
    return data;
}
} // namespace vsgocct::cad
