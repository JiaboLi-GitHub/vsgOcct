#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>

// XCAF includes for colored/named assembly test data
#include <STEPCAFControl_Writer.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopLoc_Location.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <Quantity_Color.hxx>
#include <gp_Trsf.hxx>

#include <RWStl.hxx>
#include <Poly_Triangulation.hxx>
#include <OSD_Path.hxx>
#include <fstream>

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

static void writeXdeStep(const Handle(TDocStd_Document)& doc, const std::filesystem::path& path)
{
    STEPCAFControl_Writer writer;
    writer.SetNameMode(true);
    writer.SetColorMode(true);
    if (!writer.Transfer(doc, STEPControl_AsIs))
    {
        std::cerr << "Failed to transfer XDE document for: " << path << std::endl;
        std::exit(1);
    }
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

    // box.step: 10x20x30 box (plain STEP, no XCAF metadata)
    {
        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        writeStep(makeBox.Shape(), dataDir / "box.step");
    }

    // assembly.step: compound of box + cylinder (plain STEP)
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

    // colored_box.step: single box with red surface color (XCAF)
    {
        Handle(TDocStd_Document) doc = new TDocStd_Document("XDE");
        auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        auto colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        TDF_Label boxLabel = shapeTool->AddShape(makeBox.Shape());
        TDataStd_Name::Set(boxLabel, "RedBox");
        colorTool->SetColor(boxLabel, Quantity_Color(1.0, 0.0, 0.0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);

        writeXdeStep(doc, dataDir / "colored_box.step");
    }

    // nested_assembly.step: Assembly -> SubAssembly -> (Box + Cylinder)
    // with distinct names, colors, and non-identity locations
    {
        Handle(TDocStd_Document) doc = new TDocStd_Document("XDE");
        auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        auto colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

        // Create prototype shapes
        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        TDF_Label boxProto = shapeTool->AddShape(makeBox.Shape());
        TDataStd_Name::Set(boxProto, "Box");
        colorTool->SetColor(boxProto, Quantity_Color(0.0, 0.5, 1.0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);

        BRepPrimAPI_MakeCylinder makeCyl(5.0, 15.0);
        TDF_Label cylProto = shapeTool->AddShape(makeCyl.Shape());
        TDataStd_Name::Set(cylProto, "Cylinder");
        colorTool->SetColor(cylProto, Quantity_Color(1.0, 0.5, 0.0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);

        // Create SubAssembly containing Box and Cylinder
        // Use an empty compound with makeAssembly=true to create an assembly label
        BRep_Builder bb;
        TopoDS_Compound subCompound;
        bb.MakeCompound(subCompound);
        TDF_Label subAssembly = shapeTool->AddShape(subCompound, true);
        TDataStd_Name::Set(subAssembly, "SubAssembly");

        // Add Box at origin
        TDF_Label boxComp = shapeTool->AddComponent(subAssembly, boxProto, TopLoc_Location());

        // Add Cylinder translated to (30, 0, 0)
        gp_Trsf cylTransform;
        cylTransform.SetTranslation(gp_Vec(30.0, 0.0, 0.0));
        TDF_Label cylComp = shapeTool->AddComponent(subAssembly, cylProto, TopLoc_Location(cylTransform));

        // Create root Assembly containing SubAssembly
        TopoDS_Compound rootComp;
        bb.MakeCompound(rootComp);
        TDF_Label rootAssembly = shapeTool->AddShape(rootComp, true);
        TDataStd_Name::Set(rootAssembly, "RootAssembly");

        gp_Trsf subTransform;
        subTransform.SetTranslation(gp_Vec(0.0, 50.0, 0.0));
        TDF_Label subComp = shapeTool->AddComponent(rootAssembly, subAssembly, TopLoc_Location(subTransform));

        // Suppress unused variable warnings
        (void)boxComp;
        (void)cylComp;
        (void)subComp;

        shapeTool->UpdateAssemblies();
        writeXdeStep(doc, dataDir / "nested_assembly.step");
    }

    // shared_instances.step: same box prototype used twice with different locations
    {
        Handle(TDocStd_Document) doc = new TDocStd_Document("XDE");
        auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        auto colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

        BRepPrimAPI_MakeBox makeBox(10.0, 10.0, 10.0);
        TDF_Label boxProto = shapeTool->AddShape(makeBox.Shape());
        TDataStd_Name::Set(boxProto, "SharedBox");
        colorTool->SetColor(boxProto, Quantity_Color(0.0, 1.0, 0.0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);

        BRep_Builder bb;
        TopoDS_Compound rootComp;
        bb.MakeCompound(rootComp);
        TDF_Label rootAssembly = shapeTool->AddShape(rootComp, true);
        TDataStd_Name::Set(rootAssembly, "SharedAssembly");

        // Instance 1: at origin
        shapeTool->AddComponent(rootAssembly, boxProto, TopLoc_Location());

        // Instance 2: translated to (25, 0, 0)
        gp_Trsf t2;
        t2.SetTranslation(gp_Vec(25.0, 0.0, 0.0));
        shapeTool->AddComponent(rootAssembly, boxProto, TopLoc_Location(t2));

        shapeTool->UpdateAssemblies();
        writeXdeStep(doc, dataDir / "shared_instances.step");
    }

    // cube.stl: ASCII STL cube (8 vertices, 12 triangles)
    {
        // Build a Poly_Triangulation for a unit cube manually
        // Vertices of a 10x10x10 cube at origin
        Handle(Poly_Triangulation) triangulation = new Poly_Triangulation(8, 12, false);
        triangulation->SetNode(1, gp_Pnt(0, 0, 0));
        triangulation->SetNode(2, gp_Pnt(10, 0, 0));
        triangulation->SetNode(3, gp_Pnt(10, 10, 0));
        triangulation->SetNode(4, gp_Pnt(0, 10, 0));
        triangulation->SetNode(5, gp_Pnt(0, 0, 10));
        triangulation->SetNode(6, gp_Pnt(10, 0, 10));
        triangulation->SetNode(7, gp_Pnt(10, 10, 10));
        triangulation->SetNode(8, gp_Pnt(0, 10, 10));

        // 12 triangles (2 per face, CCW winding)
        triangulation->SetTriangle(1, Poly_Triangle(1, 3, 2));  // bottom
        triangulation->SetTriangle(2, Poly_Triangle(1, 4, 3));
        triangulation->SetTriangle(3, Poly_Triangle(5, 6, 7));  // top
        triangulation->SetTriangle(4, Poly_Triangle(5, 7, 8));
        triangulation->SetTriangle(5, Poly_Triangle(1, 2, 6));  // front
        triangulation->SetTriangle(6, Poly_Triangle(1, 6, 5));
        triangulation->SetTriangle(7, Poly_Triangle(3, 4, 8));  // back
        triangulation->SetTriangle(8, Poly_Triangle(3, 8, 7));
        triangulation->SetTriangle(9, Poly_Triangle(2, 3, 7));  // right
        triangulation->SetTriangle(10, Poly_Triangle(2, 7, 6));
        triangulation->SetTriangle(11, Poly_Triangle(1, 5, 8)); // left
        triangulation->SetTriangle(12, Poly_Triangle(1, 8, 4));

        // Write ASCII
        auto asciiPath = dataDir / "cube.stl";
        OSD_Path osdAscii(asciiPath.string().c_str());
        if (!RWStl::WriteAscii(triangulation, osdAscii))
        {
            std::cerr << "Failed to write: " << asciiPath << std::endl;
            std::exit(1);
        }
        std::cout << "Written: " << asciiPath << std::endl;

        // Write binary
        auto binaryPath = dataDir / "cube_binary.stl";
        OSD_Path osdBin(binaryPath.string().c_str());
        if (!RWStl::WriteBinary(triangulation, osdBin))
        {
            std::cerr << "Failed to write: " << binaryPath << std::endl;
            std::exit(1);
        }
        std::cout << "Written: " << binaryPath << std::endl;
    }

    return 0;
}
