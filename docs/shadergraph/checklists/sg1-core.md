# SG-1 核心模型

> 状态：完成（2026-11-23）。实现见 §7；核心测试 `hbg_sg_core` 全绿。
> 目标：完成可独立测试的图数据、类型系统、校验、序列化与子图展开；不依赖 GPU/bgfx。
> 前置：SG-0 通过。
> 参考：Unity 图/节点/槽位实现；只对齐语义，不复用代码。

---

## 1. 范围

- `shadergraph/include/sg/`：`types.h`、`graph.h`、`registry.h`、`serialize.h`。
- `shadergraph/src/core/`：`graph.cpp`、`concretize.cpp`、`validate.cpp`、`serialize.cpp`、`subgraph.cpp`、`migrate.cpp`。
- 单测：`shadergraph/tests/core/`。
- 不实现 HLSL 生成、shaderc、运行时、HTTP。

## 2. 任务

| ID | 任务 | 产物 | 参考 | 验收 |
|---|---|---|---|---|
| SG1-1 | 基础类型：`ValueType/Precision/ConcretePrecision/Value` | `types.h` | `Editor/Data/Nodes/SlotValue.cs:8/32`、`Editor/Data/Enumerations/Precision.cs:23/47` | 枚举覆盖完整；值构造/比较单测 |
| SG1-2 | 图模型：NodeDef/NodeInstance/SlotDesc/Edge/Property/Keyword/Group/Note | `graph.h` | `Editor/Data/Graphs/GraphData.cs:28`、`Editor/Data/Nodes/AbstractMaterialNode.cs:16`、`Editor/Data/Graphs/MaterialSlot.cs:14` | 可构造/查询/增删；id 唯一性 |
| SG1-3 | 槽位：固定/动态、默认值、Literal、连接状态 | `graph.h`/`graph.cpp` | `Editor/Data/Graphs/DynamicValueMaterialSlot.cs:14/80`、`AbstractMaterialNode.cs:425/431/887` | 动态槽类型可持久化 |
| SG1-4 | 节点注册表：`NodeRegistry`、spec 加载、类型名稳定 | `registry.h` | `Editor/Data/Nodes/NodeClassCache.cs` | 未知类型有 fallback；重复 type 报错 |
| SG1-5 | 具体化：活跃节点集、动态类型固定点、精度传播、字面量 | `concretize.cpp` | `Editor/Data/Graphs/GraphConcretization.cs:8`、`AbstractMaterialNode.cs:1098`、`Editor/Data/Implementation/NodeUtils.cs:22/494` | 与 Unity 预期用例一致 |
| SG1-6 | 校验：环、缺失输入、类型不匹配、属性/关键字重名、Target 兼容 | `validate.cpp` | `Editor/Data/Graphs/GraphData.cs:2056`、`Editor/Data/Graphs/GraphValidation.cs` | 错误码稳定；定位到 node/slot |
| SG1-7 | 属性/关键字模型：名称/refName/exposed/默认值/枚举 entry | `graph.h` | `Editor/Data/Graphs/AbstractShaderProperty.cs:8`、`Editor/Data/Graphs/ShaderKeyword.cs:11/14`、`Generation/Enumerations/KeywordType.cs:6` | 名称唯一；非法标识符拒绝 |
| SG1-8 | JSON 读写：schemaVersion、canonical 排序、round-trip | `serialize.cpp` | `asset-format.md` §2-§9 | 两次序列化字节一致 |
| SG1-9 | 迁移：旧 schema/节点 typeVersion、未知字段策略 | `migrate.cpp` | `Editor/Data/Nodes/LegacyUnknownTypeNode.cs` | 旧 fixture → 新 JSON；更高版本拒绝 |
| SG1-10 | 子图展开：命名空间、属性提升、递归/深度限制 | `subgraph.cpp` | `Editor/Data/Nodes/Utility/SubGraphNode.cs:13`、`Editor/Data/SubGraph/SubGraphOutputNode.cs:14` | 嵌套/循环/属性冲突用例 |
| SG1-11 | 布局 sidecar 读写（不参与内容哈希） | `serialize.cpp` | `asset-format.md` §9 | 布局损坏不影响图加载 |
| SG1-12 | 单测：移植 Unity 预期 + fuzz 非法 JSON | `tests/core/` | `Tests/Editor/DynamicSlotTests.cs`、`LiteralFloatTests.cs`、`MaterialSlotTests.cs`、`SerializedGraphTests.cs` | 全绿；fuzz 不崩溃 |

## 3. 设计要点

### 3.1 所有权与错误处理

- 图对象值语义 + 稳定 id；node/slot/property 在编辑期可增删，编译期生成只读视图。
- 不使用异常做控制流；公开 API 返回 `Result<T>`（`bool ok + std::string error + Diagnostic[]`）或 `std::optional`。
- 所有内部容器使用确定性顺序（`std::map`/排序后的 `std::vector`），禁止依赖 `unordered_map` 迭代顺序。

### 3.2 动态类型固定点

```
repeat:
  changed = false
  for edge in edges:
    srcType = concreteType(edge.from)
    if srcType != slot(edge.to).concreteType and slot(edge.to).isDynamic:
      slot(edge.to).concreteType = promote(srcType)
      changed = true
  for node in activeNodes:
    changed |= node.def->updateSlots(node)   // 可能改变输出类型/槽位集合
until !changed or iterations > 1024
```

- 收敛失败报 `SG_TYPE_UNRESOLVED`，附节点/槽位。
- 未连接动态槽使用 JSON 的 `concreteType`；缺失时用节点 spec 默认值。
- 精度：先计算 `GraphPrecision`，再回退图默认，最后 `ConcretePrecision`；规则见 `codegen.md` §2。

### 3.3 校验错误码

| code | 场景 |
|---|---|
| `SG_UNKNOWN_NODE` | 类型不存在/版本不支持 |
| `SG_DUPLICATE_ID` | node/slot/property id 冲突 |
| `SG_MISSING_INPUT` | 必需输入未连接且无默认值 |
| `SG_TYPE_MISMATCH` | 连接/赋值类型非法 |
| `SG_CYCLE` | 图或子图成环 |
| `SG_NAME_CONFLICT` | 属性/关键字重名或非法标识符 |
| `SG_TARGET_UNSUPPORTED` | 节点/设置不被 Target 支持 |
| `SG_KEYWORD_LIMIT` | 关键字数量/变体位超限 |
| `SG_SUBGRAPH_DEPTH` | 子图展开过深/递归 |

诊断结构见 `codegen.md` §10；错误码在 SG-1 固化，后续只增不改。

## 4. 验收

```powershell
cmake --build --preset vs2022-debug --target hbg_sg_tests
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "sg_core"
```

通过标准：

- 核心单测全绿；覆盖动态槽、精度、Literal、子图、迁移、fuzz。
- 同一图两次 `serialize()` 字节一致；布局变化不影响内容哈希。
- 非法/更高版本 JSON 返回明确错误，不崩溃。
- 子图展开后的图与手工内联图在语义上等价（类型/连接/属性值一致）。

## 5. Known limits

- 不支持 Unity 资产导入。
- 不支持自定义节点插件/动态库；节点静态注册。
- 子图属性提升仅支持标量/向量/矩阵/纹理；Gradient 提升待 SG-6。
- 布局 sidecar 不进入 undo 历史。
- 当前没有 TargetDesc/active block 图遍历；`concretize()` 的 activeNodes 是图内显式 active、非 unknown 的节点，Target 驱动的活跃集留给 SG-2。
- Literal 采用显式 `SlotMode::Literal` + literal source 兼容检查；Unity 的 `LiteralMode` 跨 Redirect/Property 反向传播尚未实现，待 SG-4 编辑器命令接入。
- `NodeDef::updateSlots` 已保留回调接口，但 JSON spec 加载不执行回调；动态向量/矩阵的通用固定点已在 core 实现。
- JSON 当前完整覆盖节点/槽位/属性/关键字/分类/组/便签/Target/outputs；`customFunctions` 暂不建模（SG-6），未知顶层字段默认警告并忽略。
- 子图展开按本实现的 `Property`/`SubGraphOutput` 节点约定；外部输入属性不提升，未匹配输入会告警丢弃。
- 图默认精度同时保存 `GraphPrecision` 与 `ConcretePrecision`；子图作为父节点时的精度切换/回退仍留到 Target/子图编译接入。
- `ValueType::Color/String` 为属性/设置序列化补充；槽位兼容性仍以 Unity 的 Vector/Matrix/Boolean/Texture 族为准。

## 6. 对齐参考

- 图：`Editor/Data/Graphs/GraphData.cs:28/759/2056/2998`。
- 节点/槽位：`Editor/Data/Nodes/AbstractMaterialNode.cs:16/425/431/821/887/1098`、`Editor/Data/Graphs/MaterialSlot.cs:14/314`、`Editor/Data/Graphs/DynamicValueMaterialSlot.cs:14/80`。
- 类型/精度：`Editor/Data/Nodes/SlotValue.cs:8/32`、`Editor/Data/Enumerations/Precision.cs:23/47`、`Editor/Data/Implementation/NodeUtils.cs:22/494`。
- 具体化：`Editor/Data/Graphs/GraphConcretization.cs:8`。
- 校验/设置：`Editor/Data/Graphs/GraphValidation.cs`、`Editor/Data/Graphs/GraphSetup.cs`。
- 子图：`Editor/Data/Nodes/Utility/SubGraphNode.cs:13`、`Editor/Data/SubGraph/SubGraphOutputNode.cs:14`。
- 兼容：`Editor/Data/Nodes/LegacyUnknownTypeNode.cs`、`Editor/Data/Nodes/NodeClassCache.cs`。
- 测试：`Tests/Editor/DynamicSlotTests.cs`、`LiteralFloatTests.cs`、`MaterialSlotTests.cs`、`MaterialNodeTests.cs`、`SerializedGraphTests.cs`。


## 7. 实现回填（2026-11-23）

### 7.1 代码

| 模块 | 实现 |
|---|---|
| 类型/值/精度 | `shadergraph/include/sg/types.h`、`shadergraph/src/core/types.cpp` |
| 图/节点/槽位/边/属性/关键字 | `shadergraph/include/sg/graph.h`、`shadergraph/src/core/graph.cpp` |
| 节点注册/spec 加载/未知节点 fallback | `shadergraph/include/sg/registry.h`、`shadergraph/src/core/registry.cpp` |
| 动态类型固定点/精度/Literal 校验 | `shadergraph/include/sg/concretize.h`、`shadergraph/src/core/concretize.cpp`、`shadergraph/include/sg/validate.h`、`shadergraph/src/core/validate.cpp` |
| JSON/layout/内容哈希 | `shadergraph/include/sg/serialize.h`、`shadergraph/src/core/serialize.cpp`、`shadergraph/src/core/value_json.h` |
| schema/typeVersion 迁移 | `shadergraph/src/core/migrate.h`、`shadergraph/src/core/migrate.cpp` |
| 子图展开 | `shadergraph/include/sg/subgraph.h`、`shadergraph/src/core/subgraph.cpp` |
| 构建 | `shadergraph/CMakeLists.txt`、根 `CMakeLists.txt` |
| 迁移 fixture | `shadergraph/tests/fixtures/old_graph_v0.sgraph.json` |

### 7.2 测试

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug --target hbg_sg_tests
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "sg_core"
```

- `shadergraph/tests/core/test_model.cpp`：Value/兼容性；Unity 动态向量 36 组合；动态矩阵；固定槽；精度继承/Graph/Half/Single；Literal 类型与 literal source；图增删/ID/边清理。
- `shadergraph/tests/core/test_validate_serialize.cpp`：环、missing input、type mismatch、target、keyword limit、unknown node；确定性 round-trip、内容哈希忽略布局、v0 迁移、typeVersion 回调迁移、未知字段、非法 JSON fuzz、layout sidecar、registry spec/fallback。
- `shadergraph/tests/core/test_subgraph.cpp`：命名空间属性、输出重连、外部输入属性重连、嵌套展开、递归/深度、属性冲突。
- 当前 `hbg_sg_core`：17 cases / 666 assertions 全绿。

### 7.3 对齐说明

- 动态向量宽度规则按 `AbstractMaterialNode.ConvertDynamicVectorInputTypeToConcrete`：排除单通道后取最小宽度；布尔按 Vector1；动态矩阵取最大维度。
- 精度按 `PrecisionUtil`：`Single(0) < Graph(1) < Half(2)` 取 min，`Graph` 回退 `Graph::defaultPrecision`。
- JSON 顶层/节点/边/属性按稳定排序输出；两次序列化字节一致；内容哈希排除节点/组/便签布局字段。
- 子图属性使用 `sg<nodeId>_<refName>` 命名空间；内部 `Property` 节点在外部输入覆盖时被移除并直接重连 consumer。
