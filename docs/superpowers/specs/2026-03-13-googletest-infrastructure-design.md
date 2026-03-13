# GoogleTest 测试基础设施设计

## 概述

为 vsgOcct 引入 GoogleTest 单元测试框架，覆盖全部三个模块（cad、mesh、scene），建立回归测试基线。

## 设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 测试框架 | GoogleTest v1.15.2 | 业界标准，CMake 集成好，文档丰富 |
| 集成方式 | FetchContent | 零配置，无需预装 |
| 测试组织 | 按模块分文件、独立可执行文件 | scene 依赖 Vulkan，与纯 CPU 测试隔离 |
| 测试数据 | 内置小型 STEP 文件 | 提交到仓库，无需外部依赖 |
| 测试注册 | gtest_discover_tests() | 自动注册到 CTest |

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

- `FetchContent_Declare(googletest, GIT_TAG v1.15.2)`
- 三个可执行目标：`test_step_reader`、`test_shape_mesher`、`test_scene_builder`
- 每个链接 `GTest::gtest_main` + `vsgocct`
- 通过 `target_compile_definitions` 传入 `TEST_DATA_DIR`
- 用 `gtest_discover_tests()` 自动注册

### 构建与运行

```bash
cmake -S . -B build -DBUILD_TESTING=ON ...
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
ctest --test-dir build -R step_reader      # 单模块运行
```

## 测试数据

### test_helpers.h

提供 `TEST_DATA_DIR` 宏，指向 `tests/data/` 的绝对路径。CMake 通过 `add_definitions(-DTEST_DATA_DIR="...")` 传入。

### STEP 测试文件

- `box.step`：用 OCCT `BRepPrimAPI_MakeBox` + `STEPControl_Writer` 生成的 10x20x30 长方体
- `assembly.step`：含 2-3 个基本几何体（Box + Cylinder）的装配体

## 测试用例设计

### test_step_reader.cpp（cad 模块）

| 测试名 | 验证内容 |
|--------|---------|
| ReadValidBox | 读取 box.step，shape 非空、类型正确 |
| ReadAssembly | 读取 assembly.step，多 shape 处理 |
| ReadNonExistentFile | 不存在的路径，不崩溃 |
| ReadInvalidFile | 非 STEP 文件，错误处理正确 |

### test_shape_mesher.cpp（mesh 模块）

| 测试名 | 验证内容 |
|--------|---------|
| TriangulateBox | box 三角化，pointCount > 0、triangleCount >= 12 |
| TriangulateBoxHasNormals | faceNormals.size() == facePositions.size() |
| TriangulateBoxBounds | boundsMin/boundsMax 符合预期尺寸 |
| TriangulateBoxEdges | lineSegmentCount > 0（长方体 12 条边） |
| CustomMeshOptions | 不同 linearDeflection 影响三角形数量 |
| EmptyShape | 空 shape 输入，不崩溃、返回零计数 |

### test_scene_builder.cpp（scene 模块）

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
