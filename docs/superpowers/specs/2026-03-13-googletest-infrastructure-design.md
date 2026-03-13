# GoogleTest 测试基础设施设计

## 概述

为 vsgOcct 引入 GoogleTest 单元测试框架，覆盖全部三个模块（cad、mesh、scene），建立回归测试基线。

## 设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 测试框架 | GoogleTest（实现时确认最新稳定版 tag） | 业界标准，CMake 集成好，文档丰富 |
| 集成方式 | FetchContent | 零配置，无需预装 |
| 测试组织 | 按模块分文件、独立可执行文件 | 职责清晰，可单独运行 |
| 测试数据 | 内置小型 STEP 文件 | 提交到仓库，无需外部依赖 |
| 测试注册 | gtest_discover_tests() | 自动注册到 CTest |
| API 边界 | 测试代码仅使用 vsgocct 公共 API | 避免直接调用 OCCT 函数导致链接问题（OCCT 库为 PRIVATE 链接） |

## 文件结构

```
tests/
├── CMakeLists.txt              # GoogleTest FetchContent + 三个测试目标
├── test_helpers.h              # TEST_DATA_DIR 路径宏
├── data/
│   ├── box.step                # 单个长方体（<10KB）
│   └── assembly.step           # 2-3 零件装配体（<10KB）
├── test_step_reader.cpp        # cad 模块测试
├── test_shape_mesher.cpp       # mesh 模块测试
└── test_scene_builder.cpp      # scene 模块测试
```

## CMake 集成

### 根 CMakeLists.txt 变更

- 添加 `option(BUILD_TESTING "Build unit tests" ON)`
- 添加 `enable_testing()` + `add_subdirectory(tests)`（仅在 BUILD_TESTING=ON 时）

### tests/CMakeLists.txt

- `FetchContent_Declare(googletest, GIT_TAG <latest-stable>)`
- 三个可执行目标：`test_step_reader`、`test_shape_mesher`、`test_scene_builder`
- 每个链接 `GTest::gtest_main` + `vsgocct`
- 通过 `target_compile_definitions` 传入 `TEST_DATA_DIR`（统一使用此命令，不使用 `add_definitions`）
- 用 `gtest_discover_tests()` 自动注册

**TEST_DATA_DIR 定义方式**（注意 Windows 路径兼容，CMake 内部使用正斜杠）：
```cmake
target_compile_definitions(test_step_reader PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
```

### 构建与运行

```bash
cmake -S . -B build -DBUILD_TESTING=ON ...
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
ctest --test-dir build -R step_reader      # 单模块运行
```

## 测试数据

### test_helpers.h

提供 `TEST_DATA_DIR` 宏，指向 `tests/data/` 的绝对路径。由 CMake `target_compile_definitions` 注入。提供辅助函数拼接测试文件路径。

### STEP 测试文件

- `box.step`：用 OCCT `BRepPrimAPI_MakeBox` + `STEPControl_Writer` 生成的 10x20x30 长方体
- `assembly.step`：含 2-3 个基本几何体（Box + Cylinder）的装配体

## 测试用例设计

### test_step_reader.cpp（cad 模块）

| 测试名 | 验证内容 |
|--------|---------|
| ReadValidBox | 读取 box.step，shape 非空、shape 类型正确 |
| ReadAssembly | 读取 assembly.step，返回的 shape 为 `TopAbs_COMPOUND` 类型，包含多个子 shape |
| ReadNonExistentFile | 不存在的路径，`EXPECT_THROW(readStep(...), std::runtime_error)` |
| ReadInvalidFile | 非 STEP 文件（空文件），`EXPECT_THROW(readStep(...), std::runtime_error)` |
| ReadEmptyPath | 空路径 `""`，`EXPECT_THROW(readStep(""), std::runtime_error)` |

### test_shape_mesher.cpp（mesh 模块）

| 测试名 | 验证内容 |
|--------|---------|
| TriangulateBox | box 三角化，pointCount > 0、triangleCount >= 12 |
| TriangulateBoxHasNormals | faceNormals.size() == facePositions.size() |
| TriangulateBoxBounds | boundsMax - boundsMin 近似 [10, 20, 30]（允许浮点误差） |
| TriangulateBoxEdges | lineSegmentCount > 0（长方体 12 条边） |
| TriangulateAssembly | assembly.step 三角化，验证多体处理正确，三角形数 > box 单体 |
| CustomMeshOptions | 设置两个正值 deflection（如 0.1 和 2.0），验证小 deflection 产生更多三角形 |
| EmptyShape | 空 shape 输入，`EXPECT_THROW(triangulate(emptyShape), std::runtime_error)` |

### test_scene_builder.cpp（scene 模块）

注：`buildScene()` 仅在 CPU 侧构建 VSG 场景图节点，不初始化 Vulkan 设备，所有测试无需 GPU。

| 测试名 | 验证内容 |
|--------|---------|
| BuildSceneFromBox | 从 box MeshResult 构建场景，scene 非空 |
| SwitchNodesExist | pointSwitch/lineSwitch/faceSwitch 均非空 |
| ToggleVisibility | setPointsVisible(false) 等切换状态正确 |
| SceneCenterAndRadius | center/radius 为合理值（非零、非 NaN） |
| EmptyMeshResult | 空输入，不崩溃 |

## 设计边界（不做的事）

- 不做 CI 配置（后续单独做）
- 不做性能基准测试
- 不做代码覆盖率统计
- 不改动现有 src/ 代码（纯增量）
