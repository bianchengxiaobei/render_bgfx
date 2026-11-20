# V1 Checklist - VFX Runtime 实现

> 前置：`v0-checklist.md` Go。
> 顺序：V1.0b → V1.1 → V1.2 → V1.3 → V1.4；不跨里程碑并行堆功能。
> 每个任务必须回填：状态、实现位置（`file:line`）、参考（原项目 `file:line`）、偏差/限制。
> 完成后运行对应测试；发布门禁见 §6。

---

## V1.0b 最小垂直切片（unlit quad）

### V1.0b.1 资产 schema + JSON 解析/校验

- [x] 定义 `vfx::graph` C++ 类型（Property/System/Spawner/Block/Expression/Output/Subgraph）。
- [x] 写 JSON 解析器（nlohmann/json，仅 compiler/tool 侧；runtime 不依赖）。
- [x] 实现 `asset-format.md` §11 的校验规则；错误带 JSON 路径。
- [x] 实现属性表/LUT 的确定性排序规则。
- [x] 单测：合法/非法样例、错误路径、默认值。

**参考**
- `com.unity.visualeffectgraph/Editor/Models/VFXGraph.cs:156-206,1491-1535`（编译入口/资产描述）。
- `com.unity.visualeffectgraph/Editor/NewCompiler/VfxGraphCompiler.cs:33-48`（编译 Pass 顺序）。
- `com.unity.visualeffectgraph/Editor/GraphCommon/Compiler/AttributeSetLayout.cs:18-103`（布局语义）。

**验收**
- [x] 10 个以上合法/非法 JSON 单测通过。
- [x] 错误消息包含 `systems[0].update.blocks[3].type` 这类路径。
- [x] 同一输入两次解析得到相同属性顺序/哈希。

**实现位置**：`vfx/include/vfx/graph.h`、`vfx/include/vfx/asset.h`、`vfx/src/core/graph.cpp:9`（内置属性表）、`vfx/src/core/asset_json.cpp:188`（`GraphParser`）、`:606`（property）、`:1204`（system）、`:1653`（`parseGraphJson`）、`:1681`（`graphHash`）、`vfx/tests/test_vfx_asset_json.cpp:66-322`。
- `GraphParser` 用 nlohmann/json 仅编译进 `hbg_vfx` 私有源；公开头不含 JSON 类型。
- `parseGraphJson/loadGraphFile` 返回 `ParseResult{graph, errors}`，错误路径直接使用 `systems[0].update.blocks[3].type` 形式。
- `graphHash()` 走显式 FNV-1a 规范化编码；属性/系统/block 顺序不依赖哈希表。
- 顶层新增可选 `attributes` 数组承载自定义属性（见 `asset-format.md` §1/§3）；系统输出在 system sort 开启时必须显式声明 `sort`。
- curve/gradient keys 乱序输入按 time 稳定排序，重复 time 报错。

**偏差/限制**：V1.0b.1 只做 schema/路径/数值/枚举/引用校验；表达式类型检查在 V1.0b.3，事件图/子图/自定义 HLSL 语义分别在 V1.2/V1.3，lit metallic/roughness 在 V1.4。错误一次返回首个失败（不保证聚合全部错误）。

---

### V1.0b.2 Attribute layout + attribute IO codegen

- [x] 实现 `AttributeSet`、`AttributeLayout`（bucket/offset/stride/capacity 对齐 64）。
- [x] 生成 `vfx_attributes.sc`：`struct Attributes` + `VFX_ATTRIBUTE_DECLARE` 等价宏 + load/store。
- [x] 生成每属性 offset/stride 常量（word/byte）。
- [x] CPU 参考实现：对固定输入写/读回属性，验证布局。
- [x] 单测：offset/stride/buffer size/越界检查。

**参考**
- `Editor/GraphCommon/Compiler/AttributeSetLayout.cs:18-103`。
- `Editor/NewCompiler/Passes/AttributeLayoutPass.cs:30-115`（group/affinity；V1 单 group）。
- `Editor/GraphCommon/Data/DescriptionWriter/AttributeDataDescriptionWriter.cs:6-43`。
- `Shaders/Temp/Data/AttributeBuffer.hlsl:8-29`；`ByteAddressBuffer.hlsl:9-120`。
- `Editor/NewCompiler/Data/DescriptionWriter/ParticleSystemDataDescriptionWriter.cs:61-82`（每实例切片）。

**验收**
- [x] 属性布局与 CPU 参考一致（逐 word）。
- [x] capacity 对齐 64；dispatch padding 不越界。
- [x] 生成 shader 使用 `BUFFER_RAW_*`/rawLoad/rawStore，双后端可编译。

**实现位置**：`vfx/include/vfx/attribute_layout.h`、`vfx/src/compiler/attribute_layout.cpp:193`（`buildAttributeLayout`）、`:213`（CPU read）、`:242`（`generateAttributeShader`）、`vfx/tools/attributes_dump.cpp`、`vfx/tests/test_vfx_attribute_layout.cpp:27-160`、`shaders/vfx/vfx_attributes.sc`、`shaders/vfx/vfx_attributes_test.cs.sc`、`shaders/vfx/vfx_attributes_test.vs.sc`。
- `defaultAttributeSet()`：15 个内置属性按 `asset-format.md` §3 顺序 + 顶层自定义属性 JSON 顺序；`AttributeLayout::find` 保持线性稳定顺序。
- `buildAttributeLayout()`：单 bucket，`strideWords` 累加 componentCount，buffer size = `capacity * strideWords` word；capacity = `alignAttributeCapacity(requested)`（至少 64）。
- `readAttribute/writeAttribute` 做 word 级边界检查；测试覆盖 particle 63 可写、64 越界。GPU dispatch padding 的实际越界检查留到 V1.0b.6（layout 已保证 capacity 为 64 的整数倍）。
- `generateAttributeShader()` 生成 `VfxAttributes`（`Load_*`/`Store_*`，语义等价 `VFX_ATTRIBUTE_DECLARE`），使用 `BUFFER_RAW_RO/RW`、`rawLoadFloat/Uint`、`rawStoreFloat/Uint`；stage 由 `VFX_ATTRIBUTE_BUFFER_STAGE` 注入（compute 0 / output VS 12）。
- `vfx_attributes_test.cs.sc`（stage 0 RW）与 `vfx_attributes_test.vs.sc`（stage 12 RO）由同一 fixture 生成；dxbc/spirv 均编译通过（`vfx_attributes_test.cs.bin` / `.vs.bin`），并校验 checked-in `vfx_attributes.sc` 与生成器逐字节一致。复现：
  `py -3 tools/shader_build.py --shaderc <shaderc> --bgfx-src <bgfx-src> --only vfx_attributes_test --no-manifest --force --jobs 1`。

**偏差/限制**：单 group（D3）；当前 V1.0b 的默认 set 包含全部内置属性 + 已声明自定义属性，后续按 block 读写集合收窄；亲和性分组留待 profiling 后。V1.0b.5 已接入 `vfxbake`/compiler，shaderc 编译验证自动化（`hbg_vfx_compiler_tests` 可选工具链 + checked-in asset `--check`）。

---

### V1.0b.3 表达式 DAG（最小集）+ 类型检查

- [x] 节点：`attr/property/time/dt/lifetime_ratio/add/sub/mul/div/lerp/clamp/random/noise`。
- [x] 类型检查/隐式转换规则（float/vector/uint/bool）。
- [x] 常量折叠；无环校验。
- [x] 生成 HLSL 表达式字符串（用于 block 参数）。
- [x] 单测：类型错误、折叠结果、随机节点确定性。

**参考**
- `Editor/Compiler/VFXExpressionGraph.cs:59-113`（表达式图组织/归约流程）。
- `Editor/Expressions/VFXExpressionAbstract.cs:92-215,389-510`（类型码/常量折叠/代码字符串）。
- `Editor/Expressions/VFXExpressionAbstractNumericOperation.cs:92-223`（逐通道数值运算、uint/int/bool 语义）。
- `Editor/Expressions/VFXExpressionMath.cs:9-604`（sin/cos/abs/saturate/add/sub/mul/div/min/max 的 codegen 与折叠）。
- `Editor/Expressions/VFXExpressionRandom.cs:38-104`、`VFXExpressionNoise.cs:9-203`、`VFXExpressionSampleCurve.cs:9-33`。
- `Editor/Expressions/VFXAttributeExpression.cs:125-173`、`VFXCommon.hlsl:465-580`（属性访问/Rand/SampleCurve 语义参考）。

**验收**
- [x] 类型错误在编译期报出，带 JSON 路径。
- [x] 常量折叠与手工计算一致。
- [x] 生成的表达式在 dxbc/spirv 下编译通过。

**实现位置**：
- `vfx/include/vfx/expression.h`：`ExprType`、`ExpressionOp`、`ExpressionNode`、`ExpressionContext`、`CompiledExpression`。
- `vfx/src/compiler/expression.cpp:538`（表达式分派）、`:660`（二元类型提升/折叠）、`:930`（random）、`:974`（noise）、`:1014`（curve/gradient）、`:1047`（select）、`:1426`（DAG 无环检测）、`:1462`（`compileExpression`）。
- `vfx/src/core/asset_json.cpp:189-258` 把表达式编译接入 parser；`:394` 在 systems 解析后统一校验；错误路径沿用 `systems[0].init.blocks[0].alpha.b` 形式。
- `vfx/tests/test_vfx_expression.cpp:89-353`：类型、广播/转换、折叠、随机/噪声确定性、采样、错误路径、DAG 环/深度。
- `vfx/tools/expression_dump.cpp`：fixture → `shaders/vfx/vfx_expressions.sc`；`vfx/tests/data/expressions.vfx.json` 覆盖最小算子。复现：`build\vs2022\vfx\Debug\hbg_vfx_expression_dump.exe vfx\tests\data\expressions.vfx.json shaders\vfx\vfx_expressions.sc`。
- `shaders/vfx/vfx_expressions_test.cs.sc`：stage 0 compute 编译验证（dxbc/spirv），使用 `VFX_ATTR/VFX_PROP/VFX_TIME/VFX_DT/VFX_LIFETIME_RATIO/VFX_RAND/VFX_RAND4/VFX_NOISE/VFX_SAMPLE_*` 宏。
- `tools/shader_build.py` 增加 `expressions` 入口；复现：
  `py -3 tools/shader_build.py --shaderc <shaderc> --bgfx-src <bgfx-src> --only vfx_expressions_test --no-manifest --force --jobs 1`。当前 dxbc/spirv 2 variants 全部编译通过。

- 类型规则：`float/float2/float3/float4/uint/int/bool`；标量可广播到 float 向量；`int/uint -> float`、`uint<->int` 为显式/隐式 cast 节点；不允许向量截断或向量隐式转标量。
- 折叠：字面量、四则、min/max/clamp/lerp/remap、sin/cos/abs/saturate、swizzle、select 常量分支；浮点除零/非有限结果不折叠，整数除零按 Unity 语义折为 0。
- 非折叠节点：`attr/property/time/dt/lifetime_ratio/random/noise/sample_curve/sample_gradient`；`random` 固定 seed/表达式产生确定性 code，不进行常量折叠。
- HLSL codegen 使用宏而不是硬编码 buffer/uniform：`VFX_ATTR(name)`、`VFX_PROP(name)`、`VFX_TIME`、`VFX_DT`、`VFX_LIFETIME_RATIO`、`VFX_RAND(seed)`、`VFX_RAND4(seed)`、`VFX_NOISE(position,frequency,seed)`、`VFX_SAMPLE_CURVE(name,t)`、`VFX_SAMPLE_GRADIENT(name,t)`。
- 无环：JSON 表达式是内联树，天然无引用环；编译后仍做一次 DAG DFS 检测，并对嵌套深度 >64 报错。子图/命名引用循环在 V1.3 处理。

**偏差/限制**：
- `random` 当前每次求值使用固定 seed 的确定性随机，不推进 `attributes.seed`；seed 推进和每节点 hash 留到 V1.1。
- `noise`、`sample_curve`、`sample_gradient` 在 V1.0b.5 已生成 HLSL value noise/分段曲线函数；运行时 LUT 纹理资源留到 V1.0b.6/V1.1。
- `sample_texture` 明确拒绝，V1.1 再实现；`sample_curve/gradient` 的 name 目前必须是 curve/gradient property。
- parser 校验表达式的内部类型/引用；block 字段的目标类型（如 `set_color` 要求 float3）由 V1.0b.5 kernel 编译入口校验，避免在 schema 层重复类型表。
- `swizzle` 只支持 `xyzw` mask；`int/uint` 不提供向量类型（与 asset 类型系统一致）。

---

### V1.0b.4 Backend 接口 + Mock + hdrp-bgfx 适配

- [x] 实现 `Device`/`CommandSink`/`ShadingProvider` 纯虚接口（`backend-api.md`）。
- [x] `MockBackend`：记录命令、返回 opaque handle。
- [x] `HbgVfxBackend`：
  - [x] `Device` → `gfx::ResourceManager`；
  - [x] `CommandSink` → `gfx::Encoder`；
  - [x] `begin(encoder, viewId)`；
  - [x] `createProgram` 从 `.vfxb` blob；
  - [x] `findUniform/findSampler` 缓存。
- [x] 单测：mock 命令流；adapter 资源创建/销毁。

**参考**
- `src/gfx/resource_manager.h`（createBuffer/createTexture/createProgram/createUniform）。
- `src/gfx/encoder.h:49-58`（setStorageBuffer/dispatch/submitIndirect）。
- `src/gfx/buffer.h`（Storage/StorageVertex/Indirect）。
- `src/gfx/encoder.cpp:241-297`（绑定/间接实现）。
- `src/render/passes/forward_renderer.cpp:3273-3500`（compute pass 绑定范例）。
- `src/gfx/texture.h`、`src/gfx/uniform.h`、`src/gfx/state.h`（格式、uniform、state 映射）。

**验收**
- [x] Mock 命令顺序/stage/count 与预期一致。
- [x] adapter 创建/销毁 100 个 buffer/program 无泄漏（`ResourceManager::hadLeaksAtShutdown`）。
- [x] `vfx/` 公共头文件无 `bgfx::`/`hbg::`。

**实现位置**：
- `vfx/include/vfx/device.h`：`BufferUsage/TextureUsage/ShaderStage/ShaderBackend/Access`、`BufferDesc/TextureDesc/ShaderBlob`、`Device`。
- `vfx/include/vfx/command_sink.h`：`PrimitiveTopology/DrawState/PassDesc`、`CommandSink`。
- `vfx/include/vfx/shading.h`：`FrameContext/LitSettings/RenderOutputDesc`、`ShadingProvider`。
- `vfx/include/vfx/types.h`、`vfx/include/vfx/handles.h`：`Mat4`、格式/uniform/blend/compare/limits、opaque `Buffer/Uniform/Sampler` handle。
- `vfx/tests/support/mock_backend.h`：命令记录、递增 opaque handle、资源 live/destroy 统计。
- `vfx/tests/test_vfx_backend.cpp`：Mock 命令流/stage/count、缓存 handle、公共头文件引擎独立性。
- `src/render/vfx/hbg_vfx_backend.h`、`src/render/vfx/hbg_vfx_backend.cpp`：`hbg::vfx::HbgVfxBackend` 同时实现三者；`begin` 保存 encoder/view；`createProgram` 消费 `ShaderBlob`；uniform/sampler 按名称缓存；`drawIndirect` 懒创建 6 顶点 dummy vertex buffer。
- `vfx/CMakeLists.txt`、`CMakeLists.txt`：`hbg_vfx_adapter`、`hbg_vfx_adapter_tests`；`hbg_app` 链接 adapter。
- `vfx/tests/test_vfx_hbg_backend.cpp`：Noop 后端下 100 buffer/program 创建/销毁、encoder 绑定/dispatch/indirect draw、uniform/sampler 缓存；测试 shader fixture `vfx/tests/data/minimal_{compute,vertex,fragment}_dxbc.bin` 取自 V0 `vfx_spike` 编译产物。
- 验证：当前 `hbg_vfx_tests` 43 cases / 594 assertions、`hbg_vfx_adapter_tests` 4 cases / 274 assertions；`ctest` 通过。

**偏差/限制**：
- `createProgram` 只接收 `.vfxb` 的 shader section（`ShaderBlob`）；完整 `.vfxb` 容器解析在 V1.0b.5 已实现，适配层仍只消费 section。
- `StorageVertex` 的 `BufferDesc` 没有属性 layout；适配层按 `stride` 构造合成 `gfx::VertexLayout` 仅用于 `bgfx::createDynamicVertexBuffer`，VS 仍用 `BUFFER_RAW_RO` + word offset 读取，不使用该 layout 的属性语义。
- 带 `initialData` 的 Storage/StorageVertex 以 `BGFX_BUFFER_COMPUTE_READ` 创建并 CPU 上传；可写 storage 只创建空 buffer，`updateBuffer` 对 `COMPUTE_WRITE` buffer 由 `ResourceManager` 拒绝。
- `bindLighting` 仍只记录调用（V1.4 才接 ForwardRenderer 的 PassUniforms/光照 stage）；`resolveProgram` 返回无效句柄，runtime 直接使用 `.vfxb` program。
- 适配层测试使用 bgfx `Noop` 后端，不验证 D3D11/Vulkan 实际像素；V1.0b.6 已加入 runtime effect 创建测试，真实 pass 接入在 V1.0b.7/V1.4。
- `DrawState` 使用 `BGFX_STATE_CULL_CW` 表示 `cullBackFace`；VFX 默认不剔除。公共头包含 Windows `DrawState` 宏的防护。

---

### V1.0b.5 编译器 pipeline + 最小 kernel 集

- [x] `compileGraph`：JSON → 中间图 → attribute 分析 → layout → dispatch plan。
- [x] 生成 kernel：`spawner`（常量）、`init`（position/color/size/lifetime/velocity）、`update`（age/reap）、`output_update`、`fill_indirect`。
- [x] 生成 unlit quad VS/FS（**6 顶点/粒子**，billboard；4 顶点非索引无法构成两个三角形，V0 已修正）。
- [x] 调用 shaderc：dxbc `s_5_0` + spirv；缓存/错误汇总。
- [x] 生成 `.vfxb`（header/sections/strings/properties/kernels/outputs）。
- [x] `vfxbake --check` 逐字节比较。

**参考**
- 模板入口：`Shaders/Temp/Templates/Init.hlsl:10-42`、`Update.hlsl:10-29`、`OutputUpdate.hlsl:14-33`、`FillIndirect.hlsl:10-24`。
- dead list：`Shaders/Temp/Data/DeadListData.hlsl:6-31`；初始化 `Shaders/VFXCopyBuffer.compute:63-70`。
- quad：`Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58`；`Shaders/Temp/Templates/Output.hlsl:4-60`。
- 线程组：`Editor/NewCompiler/VfxDispatch.cs:11-21`。
- shaderc 参数：`tools/shader_build.py:49-51,78-82`。

**验收**
- [x] 最小 effect（常量生成 + 生命周期 + unlit quad）能烘焙出 `.vfxb`。
- [x] `--check` 第二次运行 0 差异。
- [x] 双后端 shader 均存在且能创建 program。
- [x] 生成 shader 中无 Unity 路径/宏依赖。

**实现位置**：
- `vfx/include/vfx/compiler.h`：`CompileOptions`/`CompileResult`、`compileGraph`/`compileGraphFile`。
- `vfx/src/compiler/codegen.cpp:1126`：`generatePrograms()`；`:818` spawner、`:848` deadList、`:872` init、`:926` update、`:973` output update、`:1002` fill indirect、`:1038` unlit quad VS；`:445` `analyzeAttributeUsage` 收集到 `CompiledSystem::usage`。
- `vfx/src/compiler/compiler.cpp:295`：`compileGraph`；`:374` `compileGraphFile`；shaderc 调用 + 同源 shader 缓存；错误包含 shaderc 日志。
- `vfx/src/compiler/binary.cpp:207`：`writeAssetBinary`；64B header、9 个 section、字符串表、LUT、systems/buffers/kernels/bindings/outputs；`contentHash` 覆盖 section table + content。
- `vfx/tools/vfxbake.cpp:240`：CLI；支持单文件 baking、`--check <asset-dir>`、`--shaderc`、`--bgfx-src`、`--backend`。
- `vfx/tests/assets/minimal_unlit.vfx.json` / `.vfxb`：常量 spawner + init + lifetime + unlit 6 顶点 quad 的 checked-in 资产。
- `vfx/tests/test_vfx_compiler.cpp:117`：kernel plan/source/binding；`:152` 二进制 section/哈希/确定性；`:227` 错误路径；`:257` 工具链集成测试（读取 `VFX_TEST_SHADERC`/`VFX_TEST_BGFX_SOURCE`）。
- `vfx/tests/test_vfx_hbg_backend.cpp:114`：从 `.vfxb` 的 KERNELS 记录取 DXBC blob，创建 7 个 program（6 compute + 1 VS/FS pair），验证无泄漏。
- 验证：`hbg_vfx_compiler_tests` 5 cases / 73 assertions（无工具链）或 168 assertions（带工具链）；`hbg_vfx_adapter_tests` 4 cases / 274 assertions；checked-in 资产 `vfxbake --check` 通过。
- 复现：`.\build\vs2022\vfx\Debug\vfxbake.exe --check vfx\tests\assets --shaderc <shaderc> --bgfx-src <bgfx-src>`。

**偏差/限制**：
- D1/D2/D4：非索引 6 顶点 quad、单 indirect draw、简化 fill；`numVertices = 6 * renderCount`。
- attribute analysis 仅收集读/写集合并用于校验/后续；V1 layout 仍是全部内置属性 + 已声明自定义属性（D3）。
- spawner kernel 只把 CPU 计算的 spawnCount 写入 GPU buffer；rate/burst 累积与属性/纹理绑定已由 V1.0b.6 Runtime 接入。
- 额外生成 `dead_list_init` kernel；deadList/renderCount 使用 `BUFFER_RW`（uint）以便 atomic，attribute/indirection 仍使用 raw word buffer。
- V1.0b.5 只生成 unlit quad；`lit`/`sort`/GPU/CPU events/subgraphs/custom HLSL/turbulence/plane-sphere collision 在编译期带 JSON 路径拒绝，留到 V1.1–V1.4。
- LUT 字节已写入 `.vfxb`，但 compute shader 当前用编译期曲线/梯度分段函数；V1.0b.6 已上传 LUT 纹理，shader 采样仍留到 V1.1。
- `DEBUG` section 与 `--dump-shaders` 尚未实现；`debugInfo` 保留。
- shaderc 是外部依赖；compiler 集成测试需要 `VFX_TEST_SHADERC` / `VFX_TEST_BGFX_SOURCE` 环境变量；V1.0b.8 已加入 `vfxbake --check`、reference 与 golden CTest。

---

### V1.0b.6 Runtime：Effect/System 资源与执行

- [x] `Runtime::loadAsset`、`createEffect`、`destroyEffect`。
- [x] 每 system 创建 attribute/deadList/renderKVP/indirect/bounds buffer。
- [x] dead list 初始化（counter=size、free list 逆序）；reset 时同 kernel 清空 attributes/indirection，避免 CPU 更新 COMPUTE_WRITE buffer。
- [x] `update(dt)`：常量 spawn 累积、时间/种子、bounds。
- [x] `encodeSimulation`：spawn → init → update → outputUpdate → fillIndirect；上传 uniforms。
- [x] `encodeOutput`：绑定 attribute/indirection、unlit program、indirect draw。
- [x] fill args `{6*alive,1,0,0,...}`（由 `fill_indirect` 用 renderCount 写）；输出绑定非空 vertex stream（V0：dummy 6 顶点）。
- [x] `alive = capacity - deadList.counter` 的 GPU readback（V1.0b.8 `--vfx-reference` 测试路径；运行时 diagnostics 仍用 CPU 估算）。
- [x] 属性/纹理绑定；property 名称查找。

**参考**
- `Editor/NewCompiler/Data/DescriptionWriter/ParticleSystemDataDescriptionWriter.cs:61-112`（切片/实例）。
- `Shaders/Temp/Templates/Init.hlsl:10-42`、`Update.hlsl:10-29`、`OutputUpdate.hlsl:14-33`。
- `Shaders/Temp/Data/DeadListData.hlsl:6-31`、`RenderingData.hlsl:4-61`。
- `Editor/Data/VFXDataParticle.cs:898-900`（deadList `capacity+2`）。
- `src/gfx/encoder.cpp:282-297`（间接 draw）。

**验收**
- [x] 1024 粒子 10 帧后，GPU 读回 position/age/alive 与 CPU reference 在 1e-3 内。
- [x] `alive` 计数与 dead list 一致（当前参考场景为单次 1024 burst，dead_counter=0）。
- [x] 重播/重置后状态恢复。
- [x] 无越界/validation error（Mock；真实 D3D11/Vulkan validation 留到 V1.0b.7）。

**实现位置**：
- `vfx/include/vfx/runtime.h`、`vfx/include/vfx/events.h`：`Runtime`/`Effect`/`EffectDesc`/`Diagnostics`/`PropertyValue`/`EventAttribute`。
- `vfx/src/runtime/runtime_asset.cpp:472`：`.vfxb` 校验/reader；`:584` `loadAssetBinaryFile`。
- `vfx/src/runtime/runtime.cpp:310`：effect 资源创建；`:681` spawn 累积；`:781` simulation；`:861` output；`:898` LUT 上传。
- `vfx/src/compiler/codegen.cpp:1059`：output VS `u_vfx_world`；`:1086` local/world 位置。
- `vfx/CMakeLists.txt:20`：`hbg_vfx_runtime`；`:76` `hbg_vfx_runtime_tests`。
- `vfx/tests/test_vfx_runtime.cpp:70`：资源、spawn/dispatch、命令流/binding、属性、LUT、local bounds、错误/hash 测试；7 cases / 157 assertions。
- `vfx/tests/assets/minimal_unlit.vfxb`、`curve_unlit.vfxb`：runtime 测试资产。
- `vfx/tests/test_vfx_hbg_backend.cpp`：Noop 后端实际创建 runtime effect；`hbg_vfx_adapter_tests` 4 cases / 274 assertions。

**偏差/限制**：
- `update` 使用 CPU 的常量速率/Burst 累积决定 spawnCount；V1.0b.8 reference 测试路径已读回 GPU deadList/attributes，但交互运行时 `Diagnostics::particles` 仍是 CPU 估算值。
- `fillIndirect` 仍由生成的 compute kernel 用 `renderCount` 写 args；runtime 只 dispatch，不直接 CPU 写 indirect args。
- 输出必须由适配层提前绑定 view/encoder；`drawIndirect` 的 dummy 6 顶点由 `HbgVfxBackend` 负责。
- LUT 已上传（Curve/Gradient），但当前生成的 unlit shader 仍使用编译期分段函数；shader 采样 LUT 留到 V1.1。
- `sendEvent` 当前只报告 events 未支持（V1.2），没有事件 buffer。
- runtime 仍按 effect 独立创建 program/buffer；不共享 GPU instancing。
- 真实 D3D11/Vulkan pass 在 V1.0b.7 接入；GPU readback + CPU reference 在 V1.0b.8 完成（仅 reference 测试路径）。

---

### V1.0b.7 hdrp-bgfx 集成（scene/CLI/pass）

- [x] `RenderScene` 增加 `vfxInstances`；`.hdrp.json` 解析 `"vfx"` 数组（asset/transform/play/properties）。
- [x] `ForwardRenderer` 增加惰性 `VfxRenderer`：
  - [x] 仿真 pass（compute，`allowCulling(false)`）；
  - [x] 输出 pass（raster，color/depth attachment，`readTexture(depth)`）。
- [x] 插入位置：forward 在 sky 后、opaque 前仿真；opaque 与 transparent 之间输出；deferred 同理。
- [x] `PipelineSettings::supportVFX`（默认 false；测试开启）；`--vfx`/`--vfx-test`/`--no-vfx` CLI。
- [x] 资源初始化/关闭；`FrameDiagnostics` 增加 VFX 计数。
- [x] 示例 scene sidecar + effect。

**参考**
- 帧顺序：`src/render/passes/forward_renderer.cpp:4670-4682,4731-4756`。
- forward pass：`src/render/passes/forward_renderer.cpp:4828-4880`。
- sidecar：`src/asset/scene_loader.cpp:1316-1345,1479-1488`。
- 设置：`src/render/pipeline/pipeline_settings.h:40-90`。
- HDRP VFX camera buffers 语义：`Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.RenderGraph.cs:1932-1970,2050-2100`。

**验收**
- [x] `--vfx-test` 在 D3D11/Vulkan 出图；VFX off 与同场景无 VFX sidecar baseline **0 diff**。
- [x] shutdown 无泄漏；`supportVFX=false` 时不创建 VFX 资源；resize 仅走 renderer 的 viewport 更新，未单独自动化。
- [x] `render performance` 输出 `vfx_*`。

**实现位置**：
- `src/render/scene/render_scene.h:48`：`VfxInstance`/`VfxPropertyValue`；`render_scene.cpp:44`：`vfxInstanceWorld`。
- `src/asset/scene_loader.cpp:610`：property 解析；`:697`：VFX instance；`:1621`：sidecar `vfx` 数组。
- `src/render/passes/forward_renderer.cpp:4694`：`ensureVfx`；`:4742`：`releaseVfx`；`:4750`：`updateVfx`；`:4786`：simulation pass；`:4801`：output pass；`:4834,4846`：forward 插入；`:4912,4931`：deferred 插入。
- `src/render/passes/forward_renderer.h:142`：VFX diagnostics；`:415-420`：VFX 私有接口。
- `src/render/pipeline/pipeline_settings.{h,cpp}:85,365,462`：`supportVFX`。
- `src/app/backend.{h,cpp}:160,666,2068`：CLI；`src/app/application.cpp:330`：设置；`src/app/render_test.cpp:124`：性能日志。
- `vfx/tests/assets/vfx_scene{,_mesh}.gltf`、对应 `.hdrp.json`：示例（camera-only + deferred mesh）。
- `tests/asset/test_gltf.cpp`、`tests/render/test_frame_settings.cpp`、`tests/app/test_backend.cpp`：sidecar/settings/CLI 测试。

**偏差/限制**：
- VFX 输出目前在 Forward Transparent 之前（D8）；没有并入场景透明排序。
- 仅接入 unlit quad；lit/bindLighting、VFX camera normal/depth buffer 仍留到 V1.4；GPU readback 已由 V1.0b.8 reference 测试路径覆盖。
- 示例包含 camera-only `vfx_scene` 与带 mesh 的 `vfx_scene_mesh`；D3D11/Vulkan forward 截图 0 diff，D3D11 deferred mesh 已验证 VFX 输出。
- VFX effect 资源随 renderer shutdown 释放；运行时 uniform/sampler 名称缓存由 `HbgVfxBackend` 管理，共享 `u_global_*` 不重复销毁。

---

### V1.0b.8 测试与文档回填

- [x] 单测：JSON/layout/expression/dispatch/indirect/asset roundtrip。
- [x] Mock 命令流测试：dispatch 顺序/数量/绑定。
- [x] CPU reference：1024 粒子 10 帧。
- [x] GPU golden：D3D11/Vulkan unlit quad（固定相机/种子/帧数）。
- [x] 文档：回填 V1.0b 实现位置/偏差；更新 `README` 状态。
- [x] `ctest` + IDL check + `vfxbake --check`。

**参考**
- dead list 分配/回收：`Packages/com.unity.visualeffectgraph/Shaders/Temp/Data/DeadListData.hlsl:20-38`；初始化 `Shaders/VFXCopyBuffer.compute:63-70`；buffer 布局 `Editor/Data/VFXDataParticle.cs:898-900`。
- init/update：`Shaders/Temp/Templates/Init.hlsl:10-42`、`Update.hlsl:10-29`；spawn 索引来自 `ContextData.initSpawnIndex` 与 event spawnCount。
- spawner 语义：`Editor/Models/Blocks/Implementations/Spawn/VFXSpawnerConstantRate.cs`、`VFXSpawnerBurst.cs`；V1 runtime 保留 CPU 常量速率/Burst 累积。
- render 输出：`Shaders/Temp/Templates/FillIndirect.hlsl:10-24`；V1 6 顶点/粒子与 `renderCount * 6`。

**验收**
- [x] 以上全部通过；CPU reference 与 D3D11/Vulkan readback 在 1e-3 内。
- [x] `docs/vfx/v1-checklist.md` 的 V1.0b 全部勾选。

**实现位置**：
- `vfx/include/vfx/reference.h`、`vfx/src/reference/cpu_reference.cpp`：独立 CPU reference；复用 Wang hash/FNV block seed、初始化与 update block 公式，输出 position/velocity/age/alive/seed 等。
- `vfx/tests/test_vfx_reference.cpp`：1024 粒子 × 10 帧 CPU reference、确定性、错误路径。
- `vfx/tests/assets/reference_1024.vfx.json/.vfxb`：1024 capacity、单次 1024 burst、常数 init/update；`vfx/tests/golden/vfx_scene.png`：固定 640×360 三帧 golden。
- `vfx/tests/test_vfx_compiler.cpp:233`：writer→`parseAssetBinary` roundtrip；header offset 28 增加 `assetNameOffset`。
- `vfx/tests/test_vfx_runtime.cpp:160`：逐 dispatch 校验 stage/buffer bindings、reset/continuous 命令数、indirect draw。
- `src/render/vfx/vfx_reference.{h,cpp}`：真实 GPU runtime + `bgfx::read` 读取 attributes/deadList/indirection/renderCount/indirect；逐粒子按 `spawnIndex` 与 CPU reference 比较。
- `src/app/backend.{h,cpp}`：`--vfx-reference[=<asset>]`、`--vfx-reference-graph=<json>`；`src/app/application.cpp`：reference 分支。
- `vfx/tests/run_vfx_golden.cmake`、`vfx/CMakeLists.txt`：CTest 运行 D3D11/Vulkan screenshot 并逐字节比较 golden。
- `vfx/CMakeLists.txt`：新增 `hbg_vfx_reference_d3d11/vulkan`、`hbg_vfx_golden_d3d11/vulkan`。

**验证**
- `hbg_vfx_tests`：46 cases / 21095 assertions。
- `hbg_vfx_runtime_tests`：7 cases / 157 assertions；`hbg_vfx_compiler_tests`：6 cases / 115 assertions（无工具链）/ 210（带 shaderc）。
- `hbg_vfx_adapter_tests`：4 cases / 274 assertions。
- `hbg_vfx_reference_d3d11/vulkan`：10 帧、1024 particles、alive=1024、dead_counter=0、render_count=1024、indirect=[6144 1]，CPU/GPU 逐粒子 1e-3 内。
- `hbg_vfx_golden_d3d11/vulkan`：640×360、3 帧、固定 seed/camera，均与 checked-in golden 0 字节差异。
- `hbg_tests` 248 cases / 32525 assertions；`hbg_tests` + `hbg_vfx_*` CTest 11/11 通过；全量 CTest 32/32；`vfxbake --check` 5 个资产通过；IDL check 通过。

**偏差/限制**：
- CPU reference 只覆盖当前参考资产使用的 init/update block；表达式、事件、排序、lit、soft particles 仍按 V1.1/V1.4 计划拒绝或延后。
- GPU readback 只在 `--vfx-reference` 测试路径同步等待；不进入交互帧循环，也不会更新 `Diagnostics::particles`；后者仍是 CPU spawn 估算。
- golden 为严格逐字节比较，当前 D3D11/Vulkan 输出完全相同；后续驱动/shader 变化需要重新生成 golden 并记录。
- D3D11 CTest 仅在 Windows 注册；Vulkan 测试需要可用驱动。全量 `ctest` 32/32 通过。

---

## V1.1 生成 / 运动 / 碰撞 / 曲线

### V1.1.1 Spawner：常量 + 单次/周期 Burst

- [x] 单次 burst；周期 burst（period/repeat）；同帧合并。
- [x] dt clamp；后台恢复不爆发。
- [x] spawn 数上传 uniform；spawner kernel 处理。
- [x] 单测：时间序列 spawn 数与 CPU 参考一致。

**实现位置**
- `vfx/include/vfx/reference.h`、`vfx/src/reference/cpu_reference.cpp`：`SpawnerState`、`resetSpawner`、`updateSpawner`。
- `vfx/src/runtime/runtime.cpp`：`SystemRuntime::time`/`spawnAccumulator` 使用 double；`burstOccurrences` 统一单次/周期/repeat；`setUniforms` 上传 spawnCount。
- `vfx/src/compiler/codegen.cpp`：spawner kernel 将 `u_vfx_params.z` 写入 `s_vfxSpawnCount`。
- `vfx/tests/assets/spawner_bursts.vfx.json/.vfxb`：rate=0、t=0 single burst、t=0.1 single burst、t=0 周期 burst（period=0.1、repeat=6）；t=0.1 帧同时合并 periodic + single。

**语义**
- 单次 burst 在 `time` 后只触发一次；周期 burst 首次在 `time`，之后每 `period` 触发，`repeat` 为总次数上限；`repeat=0` 表示无限。
- 周期使用 1e-6 边界 epsilon，避免 float 边界漏触发；一帧跨过多个周期时同帧合并（受 dt clamp 限制）。
- 常量速率使用 double 累加，取整 spawn，余量保留到下一帧。
- dt clamp 为 0.1s；负值/NaN 视为 0；播放切回后不会因后台 gap 爆发。

**参考**
- `Runtime/CustomSpawners/LoopAndDelay.cs`、`SetSpawnTime.cs`、`SpawnOverDistance.cs`（行为参考）。
- `Shaders/Temp/Templates/Init.hlsl:10-14`（spawnCount）。
- `Editor/Models/Blocks/Implementations/Spawn/VFXSpawnerBurst.cs`、`VFXSpawnerConstantRate.cs`（语义参考）。
- `Tests/SRPTests/Packages/com.unity.testing.visualeffectgraph/Tests/Runtime/VFXSpawnerTests.cs:460`（固定 dt 与 `floor(simulateTime / spawnCount)` 验证）。

**验收**
- [x] 固定 dt 序列下 spawn 总数与 CPU 参考一致：`spawner_bursts` 首帧 14（3 single + 2×2 periodic + 7 same-frame single），后续 2,2,2,2,0,0,0。
- [x] burst 边界（周期首帧/尾帧）无重复/丢失；repeat 尾帧停在 6 次。
- [x] `hbg_vfx_runtime_tests` 8 cases / 190 assertions；`hbg_vfx_tests` 48 cases / 21115 assertions；compiler 6 / 117（无工具链）/ 212（带 shaderc）；`vfxbake --check` 4 个资产通过。
- 备注：一帧跨过多个周期时本实现按 elapsed 补齐（受 0.1 dt clamp 限制）；Unity `PeriodicBurstEquivalent` 示例每次 OnUpdate 最多触发一次，本项目选择补齐以避免固定 dt/低帧率丢失 burst。

---

### V1.1.2 力场：Gravity / Drag / Force / Attractor / Turbulence

- [x] `integrate`、`gravity`、`drag`、`force`、`attractor`、`turbulence`。
- [x] Turbulence：Value curl noise，frequency/octaves/roughness/lacunarity/scroll；field 由 position/time 确定性求值。
- [x] 节点/参数支持 property/表达式（compiler 覆盖 intensity 的 property 表达式）。
- [x] 单测：CPU 1 帧 Force absolute/relative、size-aware Drag 数学；GPU/CPU 10 帧位置/速度/颜色/LUT 对比。
- [x] 参考 `Editor/Models/Blocks/Implementations/Forces/Gravity.cs`、`Drag.cs`、`Force.cs`、`ForceHelper.cs`、`Turbulence.cs` 与 `Shaders/VFXNoise.hlsl`。

**实现位置**
- `vfx/src/reference/cpu_reference.cpp`：`applyUpdateBlock` 的 gravity/drag/force/attractor/turbulence；`valueNoise2D`/`curlNoise3D`。
- `vfx/src/compiler/codegen.cpp`：`compileUpdateBlock` 生成同一公式；`makeExpressionHelpers` 注入 Integer-hash Value curl noise。
- `vfx/src/core/asset_json.cpp`：`drag.useSize`、`force.mode/drag`、`turbulence.intensity/frequency/octaves/roughness/lacunarity/scroll/mode/drag` schema。
- `vfx/tests/assets/forces_1024.vfx.json/.vfxb`：gravity + useSize drag + absolute/relative force + turbulence + attractor；1024 粒子、1024 live。
- `vfx/tests/test_vfx_reference.cpp`：绝对/相对 force 公式、turbulence 确定性。
- `vfx/tests/test_vfx_compiler.cpp`：force/turbulence codegen、property 表达式、shaderc 烘焙。
- `vfx/tests/test_vfx_hbg_backend.cpp`：适配层命名空间与资源生命周期回归。

**语义**
- `gravity`: `velocity += gravity*dt`。
- `drag`: `velocity *= exp(-dragEffective*dt)`；`useSize` 时 `dragEffective *= size^2*scale.x*scale.y`。
- `force`: 默认 `absolute`，`velocity += force*dt`；`relative` 使用 `force` 作为目标速度，`velocity += (force-velocity)*min(1,drag*dt)`。
- `attractor`: `delta=position-center`，距离平方下限 `1e-4`，`velocity += normalize(delta)*strength/(distanceSquared+falloff)*dt`。
- `turbulence`: 默认 `relative`；`field = position + scroll*time + 0.5`；`value = curlNoise(field)*intensity`；relative 与 ForceHelper 相同。
- Turbulence 使用 Value noise（原始 `Turbulence.cs` 默认）和整数 Wang cell hash；`octaves` clamp 到 1..8。
- `mass` 固定 1.0（见偏差 D10）；`FieldTransform`/Perlin/Cellular/force `space` 暂不支持（D11/D12）。

**参考**
- `Editor/Models/Blocks/Implementations/Forces/Gravity.cs:39-45`：`velocity += Force * deltaTime`。
- `Drag.cs:50-64`：`velocity *= exp(-(dragCoefficient*deltaTime)/mass)`；`UseParticleSize` 的 size/scale 因子。
- `Force.cs:61-67`、`ForceHelper.cs:32-40`：Absolute `/mass*dt`；Relative `(Velocity-velocity)*min(1,Drag*dt/mass)`。
- `Turbulence.cs:67-78`：`Generate{NoiseType}CurlNoise(fieldCoord + 0.5, frequency, octaves, roughness, lacunarity)`；默认 Relative/Value。
- `Shaders/VFXNoise.hlsl:23-76`：2D/3D curl 结构与导数。

**验收**
- [x] CPU reference 与 GPU 在 1e-3 内：`forces_1024` D3D11/Vulkan 10 帧、1024 粒子全部通过。
- [x] turbulence 关闭时与 V1.0b 结果一致：`minimal_unlit` 双后端 golden 0 字节差异。
- [x] 双后端 golden 基线未回归：`hbg_vfx_golden_d3d11`/`vulkan` 通过；力场本身由 `forces_1024` CPU/GPU readback 覆盖，force 场景视觉 golden 留到 V1.1.5。
- [x] `hbg_vfx_tests` 51 cases / 21142 assertions；runtime 8 / 190；compiler 9 / 131（无工具链）/ 229（带 shaderc）；adapter 4 / 274；`hbg_tests` 248 / 32525。
- [x] `vfxbake --check` 5 个资产通过；VFX 相关 CTest 11/11；全量 CTest 32/32。

---

### V1.1.3 碰撞：平面 / 球

- [ ] `collision_plane`：normal/distance/mode/restitution/friction。
- [ ] `collision_sphere`：center/radius/mode/restitution/friction。
- [ ] kill/bounce/stick；位置修正 + 速度反射。
- [ ] 单测：穿透/边界/多帧稳定；无 NaN。
- [ ] 参考 `Editor/Models/Blocks/Implementations/Collision/CollisionPlane.cs`、`CollisionSphere.cs`。

**验收**
- [ ] 粒子不穿平面/球（固定 dt 步进）。
- [ ] bounce 后速度方向/大小符合 restitution。
- [ ] CPU/GPU 一致（epsilon）。

---

### V1.1.4 Curve / Gradient + over-life 块

- [ ] 编译期 LUT 烘焙（256×1，RGBA16F/RGBA8）。
- [ ] `color_over_life`/`alpha_over_life`/`size_over_life`。
- [ ] `sample_curve`/`sample_gradient` 表达式。
- [ ] LUT 写入 `.vfxb`；运行时上传纹理；双线性采样。
- [ ] 单测：控制点 → LUT 字节；采样值与 CPU 参考一致。
- [ ] 参考 `Editor/Models/Blocks/Implementations/Color/ColorOverLife.cs`、`Editor/Models/Blocks/Implementations/Attribute/AttributeFromCurve.cs`；`Editor/Models/Operators/Implementations/SampleCurve.cs`/`SampleGradient.cs`。

**验收**
- [ ] LUT 字节确定性；`--check` 0 差异。
- [ ] GPU 采样与 CPU 参考一致（epsilon）。
- [ ] curve/gradient 纹理 stage 不与光照冲突。

---

### V1.1.5 V1.1 验收

- [ ] 双后端 golden：bursts、forces、collision、curves。
- [ ] CPU reference 场景（固定 seed/dt）。
- [ ] 性能记录（粒子数/耗时）。
- [ ] 文档回填。

---

## V1.2 GPU 事件 + 排序

### V1.2.1 Rendering data / KVP / indirection

- [ ] `renderKVP` buffer（counter + KVP）；`indirection` buffer。
- [ ] `output_update`：alive 才注册 KVP；无排序时写顺序索引。
- [ ] sort 后写 indirection；VS 读 `indirection[slot]`。
- [ ] 单测：KVP 内容/计数/索引范围。
- [ ] 参考 `Shaders/Temp/Data/RenderingData.hlsl:4-61`、`RenderingDataDescriptionWriter.cs:54-96`。

**验收**
- [ ] `alive` 与 KVP count 一致；indirection 覆盖 [0,alive)。
- [ ] 无排序时画面与 V1.0b 一致。

---

### V1.2.2 GPU sort（bitonic + merge）

- [ ] 迁移 `Sort.compute` 语义：KVP、tie-break、descending/ascending。
- [ ] 128/1024/2048/4096 单 dispatch；更大走 pre-pass + merge。
- [ ] 每 output 排序或全局排序（V1 按 output）。
- [ ] 单测：随机 KVP 排序与 CPU std::sort 比较；稳定性。
- [ ] 参考 `Shaders/Sort.compute:1-120,240-345`。

**验收**
- [ ] 排序结果与 CPU 参考一致（key/value）。
- [ ] 排序 on/off 画面可区分；透明粒子前后关系正确。
- [ ] 最大容量压力测试（262144）无越界/NaN。

---

### V1.2.3 GPU 事件 + prefix sum

- [ ] GPU event list（counter/total/index/attributes）。
- [ ] 死亡生成/子发射器；事件属性传递。
- [ ] 跨 system prefix sum（`VFXPrefixSum.compute` 语义）。
- [ ] 事件上限/丢弃策略；诊断计数。
- [ ] 单测：单/多 system 事件数/属性。
- [ ] 参考 `Shaders/Temp/Data/EventListData.hlsl:37-84`、`Shaders/VFXPrefixSum.compute:27-143`、`Editor/NewCompiler/Builder/GpuEventBuilder.cs`。

**验收**
- [ ] 事件链（A 死亡 → B 生成）数量/属性正确。
- [ ] 双 system 事件并发时 prefix sum 连续。
- [ ] CPU reference/读回一致（epsilon）。

---

### V1.2.4 CPU 事件 + 事件属性

- [ ] `Effect::sendEvent(name, attributes)`。
- [ ] CPU 事件缓冲/前缀和；下游 init 读取。
- [ ] 事件属性类型/默认值校验。
- [ ] 单测：事件顺序/属性映射。

**验收**
- [ ] 事件触发数量与调用次数一致（有上限时记录丢弃）。
- [ ] 属性映射正确（CPU/GPU）。

---

### V1.2.5 V1.2 验收

- [ ] 事件/排序 golden（D3D11/Vulkan）。
- [ ] 确定性（固定 seed/dt/frame）。
- [ ] 性能记录（sort/事件）。
- [ ] 文档回填。

---

## V1.3 子图 / 自定义 HLSL / 图校验

### V1.3.1 子图展开

- [ ] 纯函数子图：输入/输出、参数重映射。
- [ ] 递归深度 ≤16；循环检测。
- [ ] 子图内 block/表达式展开到父图。
- [ ] 单测：展开前后 CPU reference/生成 shader 等价。
- [ ] 参考 Unity `VFXSubgraph*`（按需复核）。

**验收**
- [ ] 展开前后渲染/仿真一致（epsilon/0-diff）。
- [ ] 循环/过深报错带 JSON 路径。

---

### V1.3.2 自定义 HLSL block

- [ ] 字段：`code`/`reads`/`writes`。
- [ ] 校验：禁用 include/define/buffer/uniform；只读写声明属性。
- [ ] 注入宏：`VFX_ATTR`/`VFX_SET_ATTR`/`VFX_PROP`/`VFX_TIME`/`VFX_DT`。
- [ ] 生成 kernel 作用域；shaderc 双后端门禁。
- [ ] 单测：合法/非法代码；属性读写检查。
- [ ] 参考 `Editor/Models/Blocks/Implementations/HLSL/CustomHLSL.cs`（语义/限制参考，不复制）。

**验收**
- [ ] 合法样例双后端编译通过；非法样例明确报错。
- [ ] 自定义代码只影响声明属性。

---

### V1.3.3 图校验 / 错误定位 / hot reload

- [ ] 所有错误带 JSON 路径 + 源码/hlsl 行号（如可得）。
- [ ] `vfxbake --check` 在 CI 上校验测试资产。
- [ ] 运行时 `Runtime::reloadEffect` 或 effect 重建（V1 支持销毁重建即可）。
- [ ] 编辑器后续直接链接 `vfx_compiler`。

**验收**
- [ ] 错误路径测试覆盖 ≥10 类。
- [ ] 热重载后状态/资源无泄漏。

---

### V1.3.4 V1.3 验收

- [ ] 子图/自定义 HLSL 的双后端 golden/编译测试。
- [ ] 图校验测试。
- [ ] 文档回填。

---

## V1.4 Lit quad + 发布

### V1.4.1 Lit shading pack

- [ ] `shaders/vfx/vfx_lit_quad.{vs,fs}.sc`：
  - [ ] `SurfaceData` 填充；
  - [ ] `hbgEvaluateLightLoop`；
  - [ ] `hbgSampleDirectionalShadow`；
  - [ ] contact shadow/环境/cluster 可选绑定。
- [ ] Adapter `bindLighting`：复用 `PassUniforms`/`GlobalUniforms`/`ObjectUniforms`/`ClusterUniforms`/`EnvironmentUniforms`、stage 5/6/7/8/9/10/11/14/15；VFX attribute/indirection buffer 使用 stage 12/13。VFX lit variant **不得定义** `HBG_SHADOW_POINT_SAMPLER`/`HBG_SHADOW_AREA_SAMPLER`，否则 point/area depth sampler（12/13）与 buffer 冲突；需要时先重排 stage。
- [ ] 提取共享 `bindForwardLighting()`，forward 与 VFX 共用（避免漂移）。
- [ ] Lit output 属性：metallic/roughness；默认值。
- [ ] 单测：stage/绑定表；lit program 存在。
- [ ] 参考 `shaders/lighting/light_loop.sc:339`、`shaders/material/surface_data_types.sc:6-37`、`shaders/passes/forward.fs.sc:1-36`、`forward_renderer.cpp:6270-6310`。

**验收**
- [ ] Lit VFX 在 forward/deferred 下被场景光照照亮；阴影方向正确。
- [ ] 与 lit mesh 视觉趋势一致（记录截图/PSNR）。
- [ ] 无 stage 冲突/validation error。
- [ ] 共享 `bindForwardLighting()` 重构后，普通 forward 场景与重构前 **0 diff**（或记录 PSNR 与原因）。

---

### V1.4.2 Soft particles

- [ ] FS 采样 scene depth（stage 4）；线性化；fade。
- [ ] Output 属性 `softParticles`/`softDistance`。
- [ ] 输出 pass 显式 `readTexture(depth)`。
- [ ] 单测/截图：关闭时与 V1.3 一致；开启时边缘淡出。

**验收**
- [ ] 关闭时 0 diff/epsilon。
- [ ] 开启时没有硬边/深度反转。

---

### V1.4.3 Forward/Deferred 帧顺序与排序

- [ ] Forward：仿真在 sky 后/opaque 前；输出在 opaque 与 transparent 之间。
- [ ] Deferred：仿真在 contact 后/GBuffer 前；输出在 deferred lighting/SSR 后、forward-only/transparent 前。
- [ ] 透明 lit 使用 cluster light list。
- [ ] VFX 输出与透明物体排序的记录（D8）；评估并入 DrawList 的成本。
- [ ] 参考 `forward_renderer.cpp:4670-4682,4731-4756,4828-4880`。

**验收**
- [ ] forward/deferred 双后端 golden。
- [ ] VFX off 0-diff。
- [ ] 透明场景无严重穿插（记录已知限制）。

---

### V1.4.4 诊断 / 性能 / 资源生命周期

- [ ] `FrameDiagnostics`：`vfx_effects/systems/particles/draws/dispatches/sort_passes/events/vfx_ms`。
- [ ] `render performance` 输出 VFX 指标。
- [ ] 资源：effect 创建/销毁、resize、shutdown 泄漏检查。
- [ ] 10k/100k/1M quad 基准；记录 sim/draw/sort/CPU。
- [ ] 失败降级：shader 缺失/容量超限/绑定不足时明确日志，不崩溃。

**验收**
- [ ] 多次 init/shutdown 无泄漏。
- [ ] 性能数据记录到 checklist。
- [ ] 容量/绑定超限有明确错误。

---

### V1.4.5 V1 发布验收

- [ ] `ctest` 全绿；`py -3 tools/idl_gen.py --check` 通过；`vfxbake --check` 通过。
- [ ] D3D11/Vulkan：unlit/lit、alpha/additive、soft particles、bursts、collision、events、sort、subgraph、custom HLSL 的 golden。
- [ ] VFX off / empty effect 与基线 **0 diff**。
- [ ] CPU reference（1024 粒子 10 帧）通过。
- [ ] 资源泄漏/validation error 清零。
- [ ] 文档回填：实现位置、偏差、已知限制、性能数据。
- [ ] `README.md` 状态改为“V1 完成”。

---

## 6. V1 发布门禁

| 门禁 | 条件 |
|---|---|
| 功能 | V1.0b–V1.4 全部勾选；无未登记的偏差 |
| 正确性 | CPU reference + 双后端 golden；VFX off 0-diff |
| 资源 | init/destroy/resize 无泄漏；无 validation error |
| 工具 | `ctest`、IDL check、`vfxbake --check` 通过 |
| 文档 | checklist 实现位置/参考/限制完整 |
| 性能 | 记录 10k/100k/1M 数据；不设硬门槛但不得崩溃/超时 |

## 7. 已知限制（V1）

- 仅 quad 输出；无 mesh/line/ribbon/decal/SDF。
- 每 effect 独立 buffer；无共享仿真 instancing。
- 非索引 6 顶点 quad；无 2048 分块（D1/D2）。
- 单 attribute group；无亲和性分组（D3）。
- VFX 输出在透明 pass 之前，不与透明物体交错排序（D8）。
- 自动 bounds/GPU AABB readback 未实现。
- 无 async compute；sort/事件为同步。
- 自定义 HLSL 仅 `.sc` 兼容子集。
- 无粒子投影阴影/motion vector/TAA 专用处理。
