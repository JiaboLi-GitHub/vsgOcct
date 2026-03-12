#pragma once

#include <cstddef>
#include <filesystem>

#include <vsg/all.h>

namespace vsgocct
{
// STEP 场景加载完成后返回给上层调用者的聚合数据。
// scene: 已经转换成 VSG 可直接渲染的场景节点。
// center/radius: 模型包围球信息，供相机初始定位时快速对焦。
// triangleCount: 三角形总数，便于调试、统计和在界面中显示模型复杂度。
struct StepSceneData
{
    vsg::ref_ptr<vsg::Node> scene;
    vsg::dvec3 center;
    double radius = 1.0;
    std::size_t triangleCount = 0;
};

// 读取 STEP 文件并完成以下工作：
// 1. 调用 OpenCASCADE 解析几何拓扑；
// 2. 将拓扑面离散为三角网格；
// 3. 转换为 VSG 使用的顶点/法线缓冲；
// 4. 计算模型的中心点、半径和三角形数量。
// 如果任意阶段失败，会抛出 std::runtime_error。
StepSceneData loadStepScene(const std::filesystem::path& stepFile);
} // namespace vsgocct
