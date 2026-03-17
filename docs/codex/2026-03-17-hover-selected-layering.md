# vsgOcct Hover / Selected 分层开发记录
日期：2026-03-17

## 1. 本轮目标

在前一轮基础上，系统已经支持：

- 点击得到 `SelectionToken`
- `face / edge / vertex` 细粒度高亮
- 单一 `selected` 状态

本轮继续补齐交互层：

- 鼠标移动时提供 `hover` 预高亮
- 点击后的 `selected` 高亮常驻保留
- `hover` 和 `selected` 可以分层共存
- `hover` 消失时，颜色能正确回落到 `selected`，而不是直接回基础色

## 2. 设计原则

### 2.1 scene 层同时保存两种状态

在 `AssemblySceneData` 中同时维护：

- `selectedToken`
- `hoverToken`

新增接口：

- `setHoverSelection(...)`
- `clearHoverSelection(...)`

而已有接口：

- `setSelection(...)`
- `clearSelection(...)`

继续负责常驻选择。

### 2.2 所有颜色刷新统一走重建逻辑

为了避免出现：

- `hover` 覆盖了 `selected`
- 清掉 `hover` 后无法恢复 `selected`
- 同 part 内不同 primitive 类型切换后颜色残留

本轮把颜色刷新统一收口到一条路径：

1. 恢复受影响 part 的基础色
2. 重新应用 `selected`
3. 再应用 `hover`

这条顺序非常关键，因为它定义了视觉优先级：

- `selected` 是底层常驻态
- `hover` 是顶层预览态

## 3. 颜色叠加规则

### 3.1 优先级

应用顺序为：

```text
base -> selected -> hover
```

因此：

- 选中整个 part 后，再 hover 某个 face，该 face 会显示 hover 色
- 清掉 hover 后，该 face 会恢复成 selected 色
- 如果 hover 与 selected 是同一个 token，则不额外覆盖 selected

### 3.2 为什么同 token 的 hover 不覆盖 selected

如果鼠标停在当前已选中的 primitive 上，还继续改成 hover 色，用户会感知不到“当前是选中态还是只是鼠标扫过”。

所以本轮约定：

- `hoverToken == selectedToken` 时，只显示 `selected`

这样状态语义更清楚。

## 4. viewer 行为调整

### 4.1 鼠标移动

新增 `MoveEvent` 处理：

- 无按键拖动时，实时做 hover pick
- 如果命中为空，则清掉 hover
- 如果命中和 selected 相同，则不建立额外 hover 高亮

### 4.2 鼠标按下 / 拖动 / 释放

为了避免轨道球旋转期间残留旧 hover：

- 左键按下时先清掉 hover
- 拖动过程中不做 hover pick
- 释放后：
  - 若是点击，则更新 selected
  - 若是拖动结束，则重新计算当前位置的 hover

### 4.3 隐藏 part

当 part 被复选框隐藏时：

- 如果它处于 `selected`，则清理 selected
- 如果它处于 `hover`，则清理 hover

这样不会出现已隐藏对象仍保留高亮颜色的残留状态。

## 5. 状态栏策略

状态栏也分层展示：

- 有 hover 时，优先显示 hover 命中信息
- 若同时存在 selected，状态栏会追加 selected 摘要
- 无 hover 但有 selected 时，显示 selected 信息
- 两者都没有时，回到默认统计信息

## 6. 测试补充

新增测试重点：

- `HoverOverridesSelectedPartAndClearsBackToSelection`
  - 验证 hover 可以覆盖 selected 的局部区域
  - 验证 clear hover 后能正确回落到 selected
- `MatchingHoverDoesNotOverrideSelectedPrimitive`
  - 验证 hover 与 selected 相同 token 时，不会把 selected 色改掉

## 7. 验证结果

执行：

```powershell
cmake --build build --config Debug --parallel 1
ctest --test-dir build -C Debug --output-on-failure
```

结果：

- 35/35 测试通过

## 8. 下一步建议

这轮已经有了“状态分层”，所以下一步可以往视觉表达继续增强：

1. `hover` 使用描边或发光，而不是只改底色
2. `selected` 与 `hover` 拆成不同 render pass
3. 增加 hover 节流，避免高频鼠标移动下做过多 pick
