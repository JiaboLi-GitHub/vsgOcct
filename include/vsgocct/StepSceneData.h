#pragma once

#include <cstddef>

#include <vsg/all.h>

namespace vsgocct
{
struct StepSceneData
{
    vsg::ref_ptr<vsg::Node> scene;

    vsg::ref_ptr<vsg::Switch> pointSwitch;
    vsg::ref_ptr<vsg::Switch> lineSwitch;
    vsg::ref_ptr<vsg::Switch> faceSwitch;

    vsg::dvec3 center;
    double radius = 1.0;

    std::size_t pointCount = 0;
    std::size_t lineSegmentCount = 0;
    std::size_t triangleCount = 0;

    void setPointsVisible(bool visible) const { setSwitchVisible(pointSwitch, visible); }
    void setLinesVisible(bool visible) const { setSwitchVisible(lineSwitch, visible); }
    void setFacesVisible(bool visible) const { setSwitchVisible(faceSwitch, visible); }

    bool pointsVisible() const { return isSwitchVisible(pointSwitch); }
    bool linesVisible() const { return isSwitchVisible(lineSwitch); }
    bool facesVisible() const { return isSwitchVisible(faceSwitch); }

private:
    static void setSwitchVisible(const vsg::ref_ptr<vsg::Switch>& switchNode, bool visible)
    {
        if (switchNode && !switchNode->children.empty())
        {
            switchNode->children.front().mask = vsg::boolToMask(visible);
        }
    }

    static bool isSwitchVisible(const vsg::ref_ptr<vsg::Switch>& switchNode)
    {
        return switchNode && !switchNode->children.empty() && switchNode->children.front().mask != vsg::MASK_OFF;
    }
};
} // namespace vsgocct
