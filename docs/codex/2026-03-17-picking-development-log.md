# vsgOcct 拾取功能开发记录

日期：2026-03-17

## 1. 目标

本轮实现的目标不是一次性做完整 CAD 级选择系统，而是先把 `vsgOcct` 从“只能显示”推进到“可以稳定点击选中”：

- 支持在 VSG 场景中单击拾取
- 返回可用于后续业务处理的语义结果
- 在示例 viewer 中高亮被选中的 part
- 为后续扩展 `hover / area pick / edge / vertex / face isolate` 留出数据通道

## 2. 为什么不直接上 GPU ID pass

参考了 `D:\VTK` 里的两类方案：

- `vtkCellPicker` 一类：偏精确的射线命中
- `vtkHardwareSelector` 一类：偏高频 hover / 框选 / 可见元素选择

当前 `vsgOcct` 还没有稳定的渲染元素到 OCCT 语义对象的映射，因此本轮优先采用：

- `VSG LineSegmentIntersector`
- `scene` 中维护 part 级映射
- `mesh` 中补 primitive spans

这样可以先把点击选中做好，再决定后面是否需要 GPU picking pass。

## 3. 核心设计

### 3.1 mesh 层新增 primitive spans

在 `include/vsgocct/mesh/ShapeMesher.h` 中新增：

- `PointSpan`
- `LineSpan`
- `FaceSpan`

目的：

- 点命中后可以从 point index 反查 vertexId
- 线命中后可以从 segment index 反查 edgeId
- 面命中后可以从 triangle index 反查 faceId

对应实现位于：

- `src/vsgocct/mesh/ShapeMesher.cpp`

### 3.2 scene 层补 partId 与高亮通道

在 `include/vsgocct/scene/SceneBuilder.h` / `src/vsgocct/scene/SceneBuilder.cpp` 中：

- 为每个 `PartSceneNode` 分配稳定的 `partId`
- 保存该 part 的 `faceColors`
- 保存 `pointSpans / lineSpans / faceSpans`
- 增加：
  - `findPart()`
  - `setSelectedPart()`
  - `clearSelectedPart()`

高亮策略本轮采用最简单稳定的方案：

- 每个 part 的面颜色数组单独保存
- 选中时把该 part 的颜色数组改成高亮色
- 取消选中时恢复原始颜色

优点：

- 不需要重建场景
- 不需要增补额外 overlay pass
- 与现有 per-part scene graph 兼容

### 3.3 selection 模块新增可复用拾取 API

新增头文件：

- `include/vsgocct/selection/SelectionToken.h`
- `include/vsgocct/selection/ScenePicker.h`

新增实现：

- `src/vsgocct/selection/ScenePicker.cpp`

提供：

- `PrimitiveKind`
- `SelectionToken`
- `PickResult`
- `selection::pick(camera, sceneData, x, y)`

其流程是：

1. 使用 `vsg::LineSegmentIntersector`
2. 从命中 `nodePath` 中读取 `partId`
3. 读取 primitive 类型元数据
4. 用 `primitive index -> spans -> primitiveId` 反查语义对象

## 4. 示例 viewer 改造

改动文件：

- `examples/vsgqt_step_viewer/main.cpp`

新增内容：

- `SelectionClickHandler`
- 左键点击触发拾取
- 拖动阈值过滤，避免旋转相机时误选
- 状态栏显示：
  - 选中的 primitive 类型
  - primitiveId
  - part 名称
  - 命中世界坐标
- part 被隐藏时，如果正好是当前选中对象，则自动清空选择

## 5. 测试补充

新增/更新的测试：

- `tests/test_shape_mesher.cpp`
  - 新增 primitive spans 覆盖测试
- `tests/test_scene_builder.cpp`
  - 新增 stable partId 测试
  - 新增高亮颜色切换测试
- `tests/test_scene_picker.cpp`
  - 新增中心点击命中测试
  - 新增隐藏 part 不可拾取测试

并在：

- `tests/CMakeLists.txt`

中注册了新的 `test_scene_picker`

## 6. 验证结果

本轮使用以下方式验证：

1. `cmake -S . -B build`
2. `cmake --build build --config Debug --parallel 1`
3. `ctest --test-dir build -C Debug --output-on-failure`

结果：

- 32/32 测试通过

## 7. 已知限制

### 7.1 当前更偏“可用的第一版”

本轮重点是 part / face / edge / vertex 的点击语义打通，还没有做：

- hover 提示缓存
- 框选 / area pick
- 多选
- face 独立高亮覆盖层
- OCCT 级稳定持久 ID

### 7.2 并行全量构建仍有已知问题

当前仓库在 Windows 下执行：

- `cmake --build build --config Debug --parallel N`

时，测试目标的 `POST_BUILD` DLL 复制步骤仍可能互相竞争。这是现有工程构建链的已知问题，本轮没有把它作为主线改造项处理。

实际验证采用的是：

- 串行构建
- 然后运行 `ctest`

在该路径下，本轮拾取功能实现与测试结果稳定。

## 8. 下一步建议

建议后续按这个顺序继续：

1. 基于当前 `SelectionToken` 增加 hover
2. 用 `PolytopeIntersector` 做框选
3. 为 face/edge/vertex 增加更明显的覆盖式高亮
4. 引入真正稳定的 `ShapeId / FaceId / EdgeId`
5. 再评估是否值得补 GPU picking pass
