#pragma once

#include <cstdint>
#include <functional>

#include <vsg/all.h>

#include <vsgocct/pick/FaceIdCodec.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct::pick
{
using PickCallback = std::function<void(const scene::PickResult& result)>;

class PickHandler : public vsg::Inherit<vsg::Visitor, PickHandler>
{
public:
    PickHandler(
        vsg::ref_ptr<vsg::Image> idImage,
        vsg::ref_ptr<vsg::Device> device,
        const scene::AssemblySceneData& sceneData);

    void setPickCallback(PickCallback callback);

    void apply(vsg::ButtonPressEvent& event) override;
    void apply(vsg::FrameEvent& event) override;

private:
    void performReadback();

    PickCallback _callback;
    vsg::ref_ptr<vsg::Image> _idImage;
    vsg::ref_ptr<vsg::Device> _device;
    const scene::AssemblySceneData& _sceneData;

    bool _pendingPick = false;
    int32_t _pickX = 0;
    int32_t _pickY = 0;
    uint32_t _imageWidth = 0;
    uint32_t _imageHeight = 0;
};
} // namespace vsgocct::pick
