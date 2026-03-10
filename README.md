# vsgOcct

`vsgOcct` is a small bridge library that loads STEP geometry with Open CASCADE Technology (OCCT), triangulates it, and builds a VulkanSceneGraph (VSG) scene for rendering.

当前仓库包含一个最小可用原型：

- `vsgocct` 静态库，提供 `vsgocct::loadStepScene(...)`
- 基于 `vsgQt` 的 STEP 查看器示例 `vsgqt_step_viewer`
- 面向后续 CAD/CAM 场景扩展的项目骨架

## 当前能力

- 读取 `.step` / `.stp` 文件
- 使用 OCCT 对 B-Rep 模型进行三角化
- 生成可直接用于 VSG 渲染的场景节点
- 计算模型包围盒中心、半径和三角形数量
- 通过 Qt + `vsgQt` 打开一个桌面查看器窗口

当前实现仍然偏原型，重点在于完成 STEP -> mesh -> VSG scene 这条最短链路。

## 目录结构

```text
include/                 Public headers
src/                     Library implementation
examples/                Example applications
CMakeLists.txt           Top-level build entry
ROADMAP.md               Long-term architecture and roadmap notes
```

## 依赖

当前 `CMakeLists.txt` 依赖以下组件：

- CMake 3.20+
- C++17 toolchain
- VulkanSceneGraph `vsg` 1.1.2+
- `vsgQt`
- Open CASCADE Technology (OCCT)
- Qt5 或 Qt6

当前工程默认面向 Windows，并且在示例构建时会直接引用本机路径：

- `D:/vsgQt`
- `D:/OCCT/build/OpenCASCADETargets.cmake`
- `D:/OCCT/build/inc`

如果这些路径在你的机器上不同，需要先调整根目录 `CMakeLists.txt`。

## 构建

示例命令：

```powershell
cmake -S D:\vsgOcct -B D:\vsgOcct\build `
  -DQT_PACKAGE_NAME=Qt6 `
  -DQt6_DIR="C:/Qt/6.10.1/msvc2022_64/lib/cmake/Qt6" `
  -Dvsg_DIR="C:/Program Files (x86)/vsg/lib/cmake/vsg" `
  -DvsgXchange_DIR="C:/Program Files (x86)/vsgXchange/lib/cmake/vsgXchange" `
  -DCMAKE_PREFIX_PATH="C:/Program Files (x86);C:/VulkanSDK/1.4.328.1"

cmake --build D:\vsgOcct\build --config Debug
```

如果只想构建库、不构建示例，可以关闭：

```powershell
cmake -S D:\vsgOcct -B D:\vsgOcct\build -DVSGOCCT_BUILD_EXAMPLES=OFF
```

## 运行示例

构建完成后运行 `vsgqt_step_viewer`：

```powershell
D:\vsgOcct\build\examples\vsgqt_step_viewer\Debug\vsgqt_step_viewer.exe
```

你可以：

- 直接在命令行后面传入 STEP 文件路径
- 或启动后从文件选择对话框中打开 STEP 文件

示例启动后会：

- 加载 STEP 模型
- 输出三角形数量
- 在 Qt 窗口中显示 VSG 渲染结果

## 公开接口

当前公开 API 很小，只有一个入口：

```cpp
vsgocct::StepSceneData loadStepScene(const std::filesystem::path& stepFile);
```

返回的 `StepSceneData` 包含：

- `scene`：VSG 场景节点
- `center`：模型中心
- `radius`：用于相机初始化的包围半径
- `triangleCount`：三角形数量

声明位于 `include/vsgocct/StepModelLoader.h`。

## 当前限制

- 仅覆盖 STEP 导入链路
- 还没有装配树、元数据和稳定 ID 系统
- 还没有缓存、测试和安装导出规则
- 示例工程仍带有本机路径假设
- 还不是一个完整的 CAD/CAM 引擎

## 路线图

项目的长期方向记录在 `ROADMAP.md`，目标是逐步从 STEP 查看原型演进为可嵌入 CAD/CAM 场景的底层引擎。
