#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>

#include <filesystem>
#include <iostream>
#include <string>

static void writeStep(const TopoDS_Shape& shape, const std::filesystem::path& path)
{
    STEPControl_Writer writer;
    writer.Transfer(shape, STEPControl_AsIs);
    if (writer.Write(path.string().c_str()) != IFSelect_RetDone)
    {
        std::cerr << "Failed to write: " << path << std::endl;
        std::exit(1);
    }
    std::cout << "Written: " << path << std::endl;
}

int main()
{
    std::filesystem::path dataDir(TEST_DATA_DIR);
    std::filesystem::create_directories(dataDir);

    // box.step: 10x20x30 box
    {
        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        writeStep(makeBox.Shape(), dataDir / "box.step");
    }

    // assembly.step: compound of box + cylinder
    {
        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        BRepPrimAPI_MakeCylinder makeCyl(gp_Ax2(gp_Pnt(30.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), 5.0, 15.0);

        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        builder.Add(compound, makeBox.Shape());
        builder.Add(compound, makeCyl.Shape());

        writeStep(compound, dataDir / "assembly.step");
    }

    return 0;
}
