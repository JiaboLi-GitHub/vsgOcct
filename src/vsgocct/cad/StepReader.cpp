#include <vsgocct/cad/StepReader.h>

#include <STEPCAFControl_Reader.hxx>
#include <NCollection_Sequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <XCAFDoc_VisMaterial.hxx>
#include <XCAFDoc_VisMaterialTool.hxx>

#include <Quantity_Color.hxx>
#include <Quantity_ColorRGBA.hxx>
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
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr))
    {
        // Convert from TCollection_ExtendedString (UTF-16) to ASCII.
        // TCollection_AsciiString handles Latin characters; non-Latin chars
        // become '?' but this is acceptable for M1a scope.
        TCollection_AsciiString ascii(nameAttr->Get());
        return std::string(ascii.ToCString());
    }
    return {};
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

ShapeVisualMaterial makeColorFallbackMaterial(const ShapeNodeColor& color)
{
    ShapeVisualMaterial material;
    if (!color.isSet)
    {
        return material;
    }

    material.baseColorFactor = {color.r, color.g, color.b, 1.0f};
    material.source = ShapeVisualMaterialSource::ColorFallback;
    return material;
}

ShapeVisualMaterial readLabelVisualMaterial(const TDF_Label& label)
{
    auto visMaterial = XCAFDoc_VisMaterialTool::GetShapeMaterial(label);
    if (visMaterial.IsNull() || visMaterial->IsEmpty())
    {
        return {};
    }

    ShapeVisualMaterial material;
    const Quantity_ColorRGBA baseColor = visMaterial->BaseColor();
    material.baseColorFactor = {
        static_cast<float>(baseColor.GetRGB().Red()),
        static_cast<float>(baseColor.GetRGB().Green()),
        static_cast<float>(baseColor.GetRGB().Blue()),
        static_cast<float>(baseColor.Alpha())};

    const auto alphaMode = visMaterial->AlphaMode();
    material.alphaMask = alphaMode == Graphic3d_AlphaMode_Mask ||
                         alphaMode == Graphic3d_AlphaMode_MaskBlend;
    material.alphaCutoff = visMaterial->AlphaCutOff();
    material.doubleSided = visMaterial->FaceCulling() != Graphic3d_TypeOfBackfacingModel_BackCulled &&
                           visMaterial->FaceCulling() != Graphic3d_TypeOfBackfacingModel_FrontCulled;

    const auto pbr = visMaterial->HasPbrMaterial()
                         ? visMaterial->PbrMaterial()
                         : visMaterial->ConvertToPbrMaterial();
    if (pbr.IsDefined)
    {
        material.baseColorFactor = {
            static_cast<float>(pbr.BaseColor.GetRGB().Red()),
            static_cast<float>(pbr.BaseColor.GetRGB().Green()),
            static_cast<float>(pbr.BaseColor.GetRGB().Blue()),
            static_cast<float>(pbr.BaseColor.Alpha())};
        material.emissiveFactor = {
            static_cast<float>(pbr.EmissiveFactor.r()),
            static_cast<float>(pbr.EmissiveFactor.g()),
            static_cast<float>(pbr.EmissiveFactor.b())};
        material.metallicFactor = static_cast<float>(pbr.Metallic);
        material.roughnessFactor = static_cast<float>(pbr.Roughness);
        material.hasPbr = visMaterial->HasPbrMaterial();
    }

    material.source = ShapeVisualMaterialSource::Pbr;
    return material;
}

void finalizeVisualMaterial(ShapeNode& node, const ShapeVisualMaterial& parentMaterial)
{
    if (node.visualMaterial.source == ShapeVisualMaterialSource::Default &&
        parentMaterial.source != ShapeVisualMaterialSource::Default)
    {
        node.visualMaterial = parentMaterial;
    }

    if (node.visualMaterial.source == ShapeVisualMaterialSource::Default &&
        node.color.isSet)
    {
        node.visualMaterial = makeColorFallbackMaterial(node.color);
    }
}

ShapeNode buildShapeNode(const TDF_Label& label,
                         const Handle(XCAFDoc_ShapeTool)& shapeTool,
                         const Handle(XCAFDoc_ColorTool)& colorTool,
                         const ShapeNodeColor& parentColor,
                         const ShapeVisualMaterial& parentMaterial)
{
    ShapeNode node;
    node.name = readLabelName(label);
    node.color = readLabelColor(label, colorTool);
    node.visualMaterial = readLabelVisualMaterial(label);

    // Color inheritance: if not set on this label, inherit from parent
    if (!node.color.isSet && parentColor.isSet)
    {
        node.color = parentColor;
    }
    finalizeVisualMaterial(node, parentMaterial);

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

        if (node.visualMaterial.source == ShapeVisualMaterialSource::Default)
        {
            node.visualMaterial = readLabelVisualMaterial(resolvedLabel);
            finalizeVisualMaterial(node, parentMaterial);
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
                    buildShapeNode(
                        components.Value(i),
                        shapeTool,
                        colorTool,
                        node.color,
                        node.visualMaterial));
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
    reader.SetMatMode(true);

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
    static_cast<void>(XCAFDoc_DocumentTool::VisMaterialTool(doc->Main()));

    NCollection_Sequence<TDF_Label> freeLabels;
    shapeTool->GetFreeShapes(freeLabels);

    AssemblyData assembly;
    ShapeNodeColor noParentColor;
    ShapeVisualMaterial noParentMaterial;
    for (Standard_Integer i = 1; i <= freeLabels.Length(); ++i)
    {
        try
        {
            assembly.roots.push_back(
                buildShapeNode(
                    freeLabels.Value(i),
                    shapeTool,
                    colorTool,
                    noParentColor,
                    noParentMaterial));
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
