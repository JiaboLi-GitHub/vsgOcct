#include <vsgocct/pick/SelectionManager.h>

namespace vsgocct::pick
{
void SelectionManager::selectFace(uint32_t faceId, const scene::AssemblySceneData& sceneData)
{
    if (faceId == _selectedFaceId)
    {
        return;
    }

    clearSelection();

    if (faceId == 0)
    {
        return;
    }

    auto refIt = sceneData.faceColorRefs.find(faceId);
    if (refIt == sceneData.faceColorRefs.end() || !refIt->second.colorArray)
    {
        return;
    }

    const auto& colorRef = refIt->second;
    _activeColorArray = colorRef.colorArray;
    _activeVertexOffset = colorRef.vertexOffset;
    _activeVertexCount = colorRef.vertexCount;

    // Backup original colors and apply highlight
    _originalColors.resize(_activeVertexCount);
    for (uint32_t i = 0; i < _activeVertexCount; ++i)
    {
        _originalColors[i] = (*_activeColorArray)[_activeVertexOffset + i];
        (*_activeColorArray)[_activeVertexOffset + i] = HIGHLIGHT_COLOR;
    }
    _activeColorArray->dirty();

    _selectedFaceId = faceId;
}

void SelectionManager::clearSelection()
{
    if (_selectedFaceId == 0 || !_activeColorArray)
    {
        return;
    }

    for (uint32_t i = 0; i < _activeVertexCount; ++i)
    {
        (*_activeColorArray)[_activeVertexOffset + i] = _originalColors[i];
    }
    _activeColorArray->dirty();

    _selectedFaceId = 0;
    _originalColors.clear();
    _activeColorArray = {};
    _activeVertexOffset = 0;
    _activeVertexCount = 0;
}
} // namespace vsgocct::pick
