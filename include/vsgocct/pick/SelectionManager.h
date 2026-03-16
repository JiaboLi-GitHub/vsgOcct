#pragma once

#include <cstdint>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/core/Array.h>

#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct::pick
{
class SelectionManager
{
public:
    void selectFace(uint32_t faceId, const scene::AssemblySceneData& sceneData);
    void clearSelection();
    uint32_t selectedFaceId() const { return _selectedFaceId; }

private:
    static constexpr vsg::vec3 HIGHLIGHT_COLOR{1.0f, 0.8f, 0.2f};

    uint32_t _selectedFaceId = 0;
    std::vector<vsg::vec3> _originalColors;
    vsg::ref_ptr<vsg::vec3Array> _activeColorArray;
    uint32_t _activeVertexOffset = 0;
    uint32_t _activeVertexCount = 0;
};
} // namespace vsgocct::pick
