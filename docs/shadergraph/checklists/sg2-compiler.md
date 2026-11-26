# SG-2 编译器

> 目标：完成图 → HLSL → `.sgb` 的完整离线管线，并落地 A 批核心节点（至少 60 个）。
> 前置：SG-1。
> 状态：完成（2026-11-23）。实现与测试回填见 §10。
> 参考：Unity `Generator`/`GenerationUtils`/`PropertyCollector`/`KeywordUtil`；hdrp-bgfx `shader_build.py` 与 VFX 编译器。

---

## 1. 范围

- `shadergraph/src/compile/`：`emit.cpp`、`function_registry.cpp`、`properties.cpp`、`keywords.cpp`、`pass_plan.cpp`、`shaderc.cpp`、`pack.cpp`、`generator.cpp`。
- `shadergraph/src/nodes/`：A 批节点（至少 60；目标 83）。
- `shadergraph/tools/sgbake.cpp`。
- `shaders/shadergraph/`：forward/gbuffer/preview 模板与 `varying_graph.def.sc`（先最小集）。
- 不实现运行时材质系统、HTTP、Web。

## 2. 任务

| ID | 任务 | 产物 | 参考 | 验收 |
|---|---|---|---|---|
| SG2-1 | 节点 spec 加载与校验；A 批 60+ 节点 | `nodes/*.cpp`、`node_specs/*.json` | `Editor/Data/Nodes/Math/Basic/MultiplyNode.cs:13`、`Editor/Data/Nodes/Input/Texture/SampleTexture2DNode.cs:18` | spec 校验全绿；节点可实例化 |
| SG2-2 | 函数注册表：签名去重、稳定顺序 | `function_registry.cpp` | `Editor/Data/Util/FunctionRegistry.cs:17` | 重复函数只生成一次；顺序稳定 |
| SG2-3 | 属性收集/布局/uniform 命名 | `properties.cpp` | `Editor/Generation/Processors/PropertyCollector.cs:10/126` | 属性表与声明一致；预算校验 |
| SG2-4 | 关键字收集/排列/变体键/上限 | `keywords.cpp` | `Editor/Data/Util/KeywordUtil.cs:178/196/201`、`Editor/Data/Graphs/ShaderKeyword.cs:13` | 排列数正确；超限报错 |
| SG2-5 | Surface 生成（lit/unlit/preview） | `emit_surface.cpp` | `Editor/Generation/Processors/GenerationUtils.cs:1081/1131`、`shaders/material/surface_data_types.sc:6` | 生成 HLSL 可编译 |
| SG2-6 | Vertex 生成 + GraphInputs/varying 裁剪 | `emit_vertex.cpp` | `GenerationUtils.cs:1287/1306`、`Editor/Generation/TargetResources/StructFields.cs:6/42/96/200` | 顶点位移用例 |
| SG2-7 | Pass/variant 计划 + stage 表 + 模板 | `pass_plan.cpp`、`shaders/shadergraph/*` | `Editor/Generation/Descriptors/PassDescriptor.cs:4`、`SubShaderDescriptor.cs:6`、`architecture.md` §3.3 | stage 冲突校验；模板齐全 |
| SG2-8 | shaderc 调用/缓存/并行/错误解析 | `shaderc.cpp` | `tools/shader_build.py:49/55/74`、`vfx/src/compiler/compiler.cpp:201/296` | dxbc+spirv 成功；错误含 pass/variant |
| SG2-9 | `.sgb` 打包/回读校验 | `pack.cpp` | `asset-format.md` §10、`vfx/src/compiler/binary.cpp` | 字节确定性；section 完整 |
| SG2-10 | `sgbake` CLI | `tools/sgbake.cpp` | `vfx/tools/vfxbake.cpp:27/240` | `--check`/`--backend`/`--variants` 可用 |
| SG2-11 | 预览 target（单 pass） | `targets/preview.cpp` | `Editor/Generation/Targets/PreviewTarget.cs:9/29` | 节点预览可编译 |
| SG2-12 | fixture/确定性/双后端测试 | `tests/compiler/` | `Tests/Editor/ShaderGeneratorTests.cs`、`PropertyChunkTests.cs` | 20 fixture；两次字节一致 |

## 3. 生成管线（实现顺序）

1. `load`：JSON → Graph（SG-1）。
2. `validate`：整图校验；失败仍可返回诊断（不生成）。
3. `concretize`：动态类型/精度/Literal（SG-1）。
4. `active nodes`：从 active blocks 收集上游；计算 requirements。
5. `properties`：图属性 + 节点隐式属性 + Target 属性；排序锁定。
6. `keywords`：收集/排列；计算变体键与数量；超限失败。
7. `emit functions`：按拓扑序注册函数（去重）。
8. `emit body`：节点体；Surface/Vertex 输出结构。
9. `pass plan`：每个 Target 的 pass × variant × backend；stage 表；宏。
10. `compile`：写 HLSL → shaderc → blob；缓存/并行。
11. `pack`：`.sgb` + manifest；回读校验。

## 4. 模板约定

- 模板是 bgfx `.sc`（`$input/$output`），不允许 ShaderLab。
- 生成 include 路径：`<cache>/<graphHash>/surface.sc`；通过 shaderc `-i` 注入。
- stage map：生成器输出语义→stage 映射；v1 使用引擎默认 stage（5/6/7/9-15），不做宏化。
- `varying_graph.def.sc` 固定超集；未用字段由预处理器裁剪（或保留，成本可接受）。
- 模板文件必须与生成器版本一起纳入 contentHash（模板改动使缓存失效）。

## 5. 属性/关键字细节

- uniform 名：`u_sg_<refName>`；refName 去除非标识符字符并保证唯一。
- 默认值：未连接 block/输入的默认值与 Unity 对齐（逐项记录在节点 spec）。
- 变体位：引擎 bit0-4，图关键字 bit8+；枚举关键字每个 entry 一位。
- cap 默认 64；`sgbake --variants=all` 仍受 cap 限制；超限返回 `SG_VARIANT_LIMIT`。
- 关键字引用名必须是合法 HLSL 标识符；冲突时报错。

## 6. 诊断

- 生成阶段错误：`SG_TYPE_UNRESOLVED`、`SG_MISSING_INPUT`、`SG_BUDGET_EXCEEDED`、`SG_VARIANT_LIMIT`、`SG_TEMPLATE_ERROR`。
- shaderc 错误：解析 `file(line): error`；映射到 pass/variant；保留原始 stderr 前 8 KB。
- 所有错误必须能在 CLI 与宿主中复用（同一 `Diagnostic` 结构）。

## 7. 验收

```powershell
# 单图
.\build\vs2022\Debug\sgbake.exe --input tests\shadergraph\assets\smoke_lit.sgraph.json --output out\smoke_lit.sgb --backend dxbc,spirv

# 目录确定性
.\build\vs2022\Debug\sgbake.exe --check tests\shadergraph\assets

# 单元
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "sg_compiler"
```

通过标准：

- 20 个 fixture（含动态类型/矩阵/纹理/子图/关键字）双后端编译通过。
- 同一输入两次 `.sgb` 字节一致；模板/生成器版本变化会使 hash 变化。
- 属性/关键字/变体预算超限返回稳定错误码。
- 生成的 forward/gbuffer 模板在 SG-0 spike 环境可加载（正式渲染在 SG-3）。

## 8. Known limits（初始）

- A 批目标 83，SG-2 至少 60；剩余在 SG-6。
- 不支持 Compute/Dots 实例化；图材质 v1 不参与 instancing。
- 自定义函数 v1 限制 include 白名单；完整编辑在 SG-6。
- 枚举关键字的 `dynamicbranch` 在 v1 按变体处理。

## 9. 对齐参考

- 生成器：`Editor/Generation/Processors/Generator.cs:35/256/415/576`。
- 属性/结构：`Editor/Generation/Processors/GenerationUtils.cs:907/1081/1131/1287/1306`、`Editor/Generation/Processors/PropertyCollector.cs:10/126`。
- 关键字：`Editor/Data/Util/KeywordUtil.cs:178/196/201`、`Editor/Generation/Processors/KeywordCollector.cs:8`、`Editor/Data/Graphs/ShaderKeyword.cs:13`。
- 活跃字段：`Editor/Generation/Processors/ActiveFields.cs:64`。
- 字符串构建：`Editor/Generation/Processors/ShaderStringBuilder.cs:19/167`。
- 函数注册：`Editor/Data/Util/FunctionRegistry.cs:17`。
- 描述符：`Editor/Generation/Descriptors/PassDescriptor.cs:4`、`SubShaderDescriptor.cs:6`、`FieldDescriptor.cs:4`、`KeywordDescriptor.cs:4`、`StructDescriptor.cs:4`。
- 模板：`Editor/Generation/Templates/PassMesh.template`、`BuildSurfaceDescriptionInputs.template.hlsl`、`BuildVertexDescriptionInputs.template.hlsl`。
- hdrp-bgfx 编译：`tools/shader_build.py:49/55/74`、`vfx/src/compiler/compiler.cpp:201/296`、`vfx/tools/vfxbake.cpp:27/240`。


## 10. 实现与测试回填

### 10.1 实现位置

| 任务 | 实现 |
|---|---|
| SG2-1 节点 | `shadergraph/src/nodes/node_library.cpp`、`shadergraph/include/sg/node_library.h`；104 个 SG-2 canonical NodeDef（含 26 个 Block/Lit/Unlit 别名）+ SG-6 Input/Basic/Property/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 208 个；空格/连字符归一化别名 |
| SG2-2 函数注册 | `shadergraph/src/compile/function_registry.cpp` |
| SG2-3 属性 | `shadergraph/src/compile/properties.cpp`：按 refName 排序、`u_sg_<ref>`、纹理 stage 0-4、32 uniform/4 matrix/5 texture 预算 |
| SG2-4 关键字 | `shadergraph/src/compile/keywords.cpp`：引擎 bit0-2、图关键字 bit8+、布尔/枚举排列、64 variant cap |
| SG2-5 Surface | `shadergraph/src/compile/emit.cpp`；lit/unlit/preview 模板 |
| SG2-6 Vertex/varying | `shaders/shadergraph/forward_graph.vs.sc`、`varying_graph.def.sc`；Position block 位移已接线，法线/切线位移与 varying 裁剪仍是已知限制 |
| SG2-7 Pass/stage | `shadergraph/src/compile/targets.cpp`、`pass_plan.cpp`；lit 4 pass、unlit 3 pass、preview 1 pass |
| SG2-8 shaderc | `shadergraph/src/compile/shaderc.cpp`：dxbc/spirv、缓存 key、错误含 pass/variant |
| SG2-9 SGB | `shadergraph/src/compile/pack.cpp`、`validateSgb` 回读 section/blob 范围 |
| SG2-10 CLI | `shadergraph/tools/sgbake.cpp`：`--input/--output/--check/--backend/--variants/--cache` |
| SG2-11 Preview | `preview` Target（单 forward unlit pass） |
| SG2-12 测试 | `shadergraph/tests/compiler/test_compiler.cpp`、`tests/shadergraph/assets/*.sgraph.json` |

### 10.2 对齐参考（实际阅读）

- `Editor/Generation/Processors/Generator.cs:256`（活跃节点/`BuildShader`）、`:415`（SubShader）、`:576`（Pass）。
- `Editor/Generation/Processors/GenerationUtils.cs:907`（属性块）、`:1081`（SurfaceDescription 结构）、`:1131`（Surface 函数）、`:1287`（VertexDescription 结构）、`:1306`（Vertex 函数）。
- `Editor/Generation/Processors/PropertyCollector.cs:10/34/126`（收集、排序、声明）。
- `Editor/Data/Util/FunctionRegistry.cs:17`（按名字/签名去重与稳定顺序）。
- `Editor/Data/Util/KeywordUtil.cs:178/196/201`、`Editor/Data/Util/KeywordCollector.cs`（排列/定义/上限）。
- `Editor/Data/Nodes/AbstractMaterialNode.cs:425/431/517/549`（槽位取值/动态类型）、`:572`（精度）。
- `Editor/Data/Implementation/NodeUtils.cs:494`（输出适配）。
- `Editor/Generation/Descriptors/PassDescriptor.cs:4`、`SubShaderDescriptor.cs:6`（pass 描述语义）。
- hdrp-bgfx：`tools/shader_build.py:49/55/74`、`vfx/src/compiler/compiler.cpp:201/296`、`vfx/src/compiler/binary.cpp`、`vfx/tools/vfxbake.cpp:27/240`。

### 10.3 验证结果

- `hbg_sg_core`：**29 cases / 1335 assertions 全绿**。
- `sgbake --check tests\shadergraph\assets`：通过；33 个 `.sgraph.json` fixture 均有 `.sgb`，section 1-7 完整。
- 每个 fixture 均生成 **dxbc + spirv、vertex + fragment**；同一输入两次编译字节一致。
- `keyword_bool` 的 `--variants=all` 生成 32 个 shader，variant 表稳定。
- 超 64 variant 返回 `SG_VARIANT_LIMIT`；纹理/uniform/matrix 超预算返回 `SG_BUDGET_EXCEEDED`。
- 相对路径运行 `sgbake`（不带 `--root/--shaderc/--bgfx-src`）可完成编译；默认路径由 CMake 注入。

### 10.4 Known limits

- Vertex：Position block 已生成 `surface.positionOS` 并驱动顶点位移；法线/切线 block 位移和 varying 裁剪尚未生成，留到 SG-3/SG-6。
- `CompileOptions.subgraphs` 已接入 SG-1 `expandSubgraphs`；`sgbake` CLI 尚未自动加载 `.ssubgraph.json`，当前 fixture 使用直通节点。
- `Custom Function` v1 支持字符串函数体与 `.hlsl`/`.cginc`/`.cg` include；纹理/SamplerState 槽位、Unity GUID 源引用和 include 白名单尚未实现。
- `Dropdown` v1 使用 Graph 级 `dropdowns` 的活动 `value` 选择输入；Unity 子图按实例选择 entry 的 `if` 链和主图禁用语义尚未实现。
- shaderc 编译当前串行；缓存 key 暂未包含 shaderc 版本字符串。
- `sgbake` 不写 `manifest.json`；目录发现暂由运行时扫描，SG-3 再生成 manifest。
- `append/swizzle/flip` 对非四通道输入按可用通道构造，完整 Unity 变体测试待 SG-6。
- 节点定义以 C++ 表为主；`NodeRegistry::loadSpecs` 已支持 `emit.body/function`、`block`、`requirements`，尚未把所有内置节点导出为 JSON spec。
