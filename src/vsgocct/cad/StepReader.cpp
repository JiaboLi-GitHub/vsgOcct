#include <vsgocct/cad/StepReader.h>

#include <STEPCAFControl_Reader.hxx>
#include <NCollection_Sequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <Quantity_Color.hxx>
#include <TCollection_AsciiString.hxx>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace vsgocct::cad
{
namespace
{
    std::string readLabelName(const TDF_Label& label)
    {
        Handle(TDataStd_Name) nameAttr;
        if (!label.FindAttribute(TDataStd_Name::GetID(), nameAttr))
            return {};

        const auto& ext = nameAttr->Get();
        std::string utf8(static_cast<size_t>(ext.LengthOfCString()) + 1, '\0');
        Standard_PCharacter out = utf8.data();
        const int len = ext.ToUTF8CString(out);
        utf8.resize(static_cast<size_t>(len));
        return utf8;
    }


ShapeNodeColor readLabelColor(const TDF_Label& label,
                              const Handle(XCAFDoc_ColorTool)& colorTool)
{
    Quantity_Color qc;
    if (colorTool->GetColor(label, XCAFDoc_ColorSurf, qc))
    {
        return {static_cast<float>(qc.Red()),
                static_cast<float>(qc.Green()),
                static_cast<float>(qc.Blue()),
                true};
    }
    return {};
}

ShapeNode buildShapeNode(const TDF_Label& label,
                         const Handle(XCAFDoc_ShapeTool)& shapeTool,
                         const Handle(XCAFDoc_ColorTool)& colorTool,
                         const ShapeNodeColor& parentColor)
{
    ShapeNode node;
    node.name = readLabelName(label);
    node.color = readLabelColor(label, colorTool);

    // Color inheritance: if not set on this label, inherit from parent
    if (!node.color.isSet && parentColor.isSet)
    {
        node.color = parentColor;
    }

    // Resolve references: extract location and follow to prototype
    TDF_Label resolvedLabel = label;
    if (shapeTool->IsReference(label))
    {
        // Extract transform from the component label
        node.location = shapeTool->GetLocation(label);

        TDF_Label refLabel;
        shapeTool->GetReferredShape(label, refLabel);
        resolvedLabel = refLabel;

        // Re-read name/color from prototype if not set on component
        if (node.name.empty())
        {
            node.name = readLabelName(resolvedLabel);
        }
        if (!node.color.isSet)
        {
            node.color = readLabelColor(resolvedLabel, colorTool);
            if (!node.color.isSet && parentColor.isSet)
            {
                node.color = parentColor;
            }
        }
    }

    // Process the resolved label (prototype)
    if (shapeTool->IsAssembly(resolvedLabel))
    {
        node.type = ShapeNodeType::Assembly;
        NCollection_Sequence<TDF_Label> components;
        shapeTool->GetComponents(resolvedLabel, components);
        for (Standard_Integer i = 1; i <= components.Length(); ++i)
        {
            try
            {
                node.children.push_back(
                    buildShapeNode(components.Value(i), shapeTool, colorTool, node.color));
            }
            catch (const std::exception& ex)
            {
                std::cerr << "Warning: skipping component " << i
                          << ": " << ex.what() << std::endl;
            }
        }
    }
    else
    {
        // Simple shape (leaf Part)
        node.type = ShapeNodeType::Part;
        node.shape = shapeTool->GetShape(resolvedLabel);
        if (node.shape.IsNull())
        {
            throw std::runtime_error("Null shape at label: " + node.name);
        }
    }

    return node;
}

std::size_t countParts(const ShapeNode& node)
{
    if (node.type == ShapeNodeType::Part)
    {
        return 1;
    }
    std::size_t count = 0;
    for (const auto& child : node.children)
    {
        count += countParts(child);
    }
    return count;
}
} // namespace

AssemblyData readStep(const std::filesystem::path& stepFile, const ReaderOptions& /*options*/)
{
    std::ifstream input(stepFile, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open STEP file: " + stepFile.u8string());
    }

    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    reader.SetColorMode(true);

    const std::string displayName = stepFile.filename().u8string();
    const auto status = reader.ReadStream(displayName.c_str(), input);
    if (status != IFSelect_RetDone)
    {
        throw std::runtime_error("OCCT failed to read STEP data from: " + stepFile.u8string());
    }

    Handle(TDocStd_Document) doc = new TDocStd_Document("XDE");
    if (!reader.Transfer(doc))
    {
        throw std::runtime_error("OCCT failed to transfer STEP document: " + stepFile.u8string());
    }

    auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    auto colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    NCollection_Sequence<TDF_Label> freeLabels;
    shapeTool->GetFreeShapes(freeLabels);

    AssemblyData assembly;
    ShapeNodeColor noParentColor;
    for (Standard_Integer i = 1; i <= freeLabels.Length(); ++i)
    {
        try
        {
            assembly.roots.push_back(
                buildShapeNode(freeLabels.Value(i), shapeTool, colorTool, noParentColor));
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Warning: skipping free shape " << i
                      << ": " << ex.what() << std::endl;
        }
    }

    // Verify at least one valid part exists
    std::size_t totalParts = 0;
    for (const auto& root : assembly.roots)
    {
        totalParts += countParts(root);
    }
    if (totalParts == 0)
    {
        throw std::runtime_error("No valid parts found in STEP file: " + stepFile.u8string());
    }

    return assembly;
}
} // namespace vsgocct::cad
