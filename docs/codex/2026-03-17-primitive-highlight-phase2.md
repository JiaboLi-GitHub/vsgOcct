# vsgOcct 第二阶段细粒度高亮开发记录
日期：2026-03-17

## 1. 本轮目标

在第一阶段已经具备：

- `LineSegmentIntersector` 单击拾取
- `SelectionToken` 语义结果
- `part` 级高亮

本轮继续补齐第二阶段能力：

- 命中 `face` 时只高亮对应面
- 命中 `edge` 时只高亮对应边
- 命中 `vertex` 时只高亮对应点
- 在不同 primitive 类型之间切换时，旧高亮能正确恢复

## 2. 为什么这轮要改成“动态颜色数组”

第一阶段只有面片使用了动态 `vec3Array` 颜色属性，线和点仍然是 shader 内写死颜色：

- 这意味着 `edge / vertex` 没有办法单独改色
- 如果继续沿用常量着色，只能整批换 pipeline，粒度不够

因此这轮统一改成：

- face：保留已有动态颜色数组
- edge：新增动态颜色数组
- vertex：新增动态颜色数组

这样 scene 层只需要改颜色数据，不需要切换渲染节点结构。

## 3. 核心实现

### 3.1 Scene 状态从 `selectedPartId` 扩展到 `selectedToken`

在 `AssemblySceneData` 中新增：

- `selectedToken`

并新增统一接口：

- `setSelection(...)`
- `clearSelection(...)`

兼容性处理：

- `setSelectedPart(...)` 继续保留，但内部转发到 `setSelection(...)`
- `clearSelectedPart(...)` 转发到 `clearSelection(...)`

这样旧调用点不会失效，而新逻辑可以直接表达 `Face / Edge / Vertex` 选择。

### 3.2 PartSceneNode 增加 line/point 颜色缓冲

在 `PartSceneNode` 中新增：

- `lineColors`
- `pointColors`

用途：

- `faceColors` 负责面高亮
- `lineColors` 负责边高亮
- `pointColors` 负责点高亮

## 4. 高亮策略

### 4.1 基础色

- face：沿用 part 本身颜色
- edge：统一基础线框色
- vertex：统一基础点色

### 4.2 高亮色

为了让不同 primitive 更容易区分，使用不同高亮色：

- part：亮金色
- face：橙色
- edge：青色
- vertex：洋红色

### 4.3 切换恢复

scene 只允许一个活动选择：

1. 应用新高亮前，先恢复旧选择所在 part 的基础颜色
2. 再把新 token 对应的 primitive 染成高亮色

这样可以保证：

- face -> edge 切换时，face 会恢复
- edge -> vertex 切换时，edge 会恢复
- 清空选择时，所有颜色回到基础态

## 5. viewer 接入方式

viewer 左键交互没有重写，只做了一点切换：

- 第一阶段：`pickResult.token.partId -> setSelectedPart(...)`
- 第二阶段：`pickResult.token -> setSelection(...)`

这样点击命中的 primitive 会直接驱动细粒度高亮，而不是总是退化成整件高亮。

## 6. 测试补充

新增/保留的验证重点：

- `SelectionHighlightUpdatesFaceColors`
  - 保证第一阶段 part 高亮能力仍然可用
- `PrimitiveSelectionTransitionsAcrossKinds`
  - 验证 `face -> edge -> vertex -> clear` 的颜色切换与恢复

测试思路不是只看“有无命中”，而是直接检查颜色数组：

- 目标 span 被改色
- 非目标 span 保持基础色
- 切换下一种选择时，上一种高亮被恢复

## 7. 验证结果

执行：

```powershell
cmake --build build --config Debug --parallel 1
ctest --test-dir build -C Debug --output-on-failure
```

结果：

- 33/33 测试通过

## 8. 当前边界

这轮完成的是“单选高亮”：

- 同一时间只保留一个活动选择
- 还没有做多选
- 还没有做 hover 预高亮
- 还没有做 face/edge 的描边增强或单独 overlay pass

如果继续往下推进，下一步比较自然的是：

1. face 选中时补局部描边或轮廓强调
2. hover 与 selected 分层
3. 区域选择与批量高亮
