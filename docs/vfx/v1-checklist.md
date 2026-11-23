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
- [x] 每 system 创建 attribute/deadList/indirection/renderCount/spawnCount/indirect/bounds buffer；V1.2.1 增加 renderKVP。
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
- LUT 已上传（Curve/Gradient）；V1.0b.6 时生成 shader 仍使用编译期分段函数，V1.1.4 已改为 stage 9/10 LUT 双线性采样。
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
- [x] 双后端 golden 基线未回归：`hbg_vfx_golden_d3d11`/`vulkan` 通过；力场本身由 `forces_1024` CPU/GPU readback 覆盖，force 场景视觉 golden 已在 V1.1.5 完成。
- [x] `hbg_vfx_tests` 51 cases / 21142 assertions；runtime 8 / 190；compiler 9 / 131（无工具链）/ 229（带 shaderc）；adapter 4 / 274；`hbg_tests` 248 / 32525。
- [x] `vfxbake --check` 5 个资产通过；VFX 相关 CTest 11/11；全量 CTest 32/32。

---

### V1.1.3 碰撞：平面 / 球

- [x] `collision_plane`：normal/distance/mode/restitution/friction。
- [x] `collision_sphere`：center/radius/mode/restitution/friction。
- [x] kill/bounce/stick；位置修正 + 速度反射。
- [x] 单测：穿透/边界/多帧稳定；无 NaN。
- [x] 参考 `Editor/Models/Blocks/Implementations/Collision/CollisionPlane.cs`、`CollisionSphere.cs`。

**实现位置**
- `vfx/src/reference/cpu_reference.cpp`：`CollisionMode`、`intersectPlane`、`intersectSphere`、`applyBounce`；`applyUpdateBlock` 的 plane/sphere 分支。
- `vfx/src/compiler/codegen.cpp`：`makeCollisionResponseCode` 与 `compileUpdateBlock` 的 plane/sphere sweeping/inside detection。
- `vfx/tests/assets/collision_1024.vfx.json/.vfxb`：1024 粒子，plane + sphere bounce，10 帧 CPU/GPU reference。
- `vfx/tests/test_vfx_reference.cpp`：plane kill/bounce/stick、sphere sweep/restitution、多帧稳定。
- `vfx/tests/test_vfx_compiler.cpp`：plane/sphere codegen、kill/stick codegen。
- `vfx/CMakeLists.txt`：`hbg_vfx_reference_collision_d3d11/vulkan`。

**语义**
- `collision_plane`：`normal` 归一化；`d = dot(position, normal) - distance`；粒子在平面负侧或本帧从正侧穿过时发生碰撞；位置修正到平面。
- `collision_sphere`：`center/radius`；当前在球内或本帧射线从外到内与球相交时发生碰撞；位置修正到球面。
- `bounce`：`vn = dot(velocity, normal)`；`vn < 0` 时 `velocity -= (1 + max(restitution, 0)) * vn * normal`；切向速度再乘 `1 - exp(-max(friction, 0) * dt)`。
- `stick`：位置修正到碰撞点，速度清零。
- `kill`：标记 `alive=false`，交给 update 尾部的 dead list 回收。
- 默认 `mode=bounce`、`restitution=0.1`、`friction=0.0`；与原 `CollisionBase` 的 Bounce/Friction 默认一致。
- 当前使用零粒子半径；原 `RadiusMode`、`BounceSpeedThreshold`、`LifetimeLoss`、roughSurface、碰撞事件和 sphere transform 尚未实现（D13）。

**参考**
- `Editor/Models/Blocks/Implementations/Collision/CollisionPlane.cs:34-56`：平面 signed distance、穿过检测、inside teleport。
- `CollisionSphere.cs:55-159`：单位球射线求交、inside 投影、法线。
- `CollisionBase.cs:214-269`：bounce/friction 响应、hitPos 与 `position` 修正。
- `CollisionBase.cs:123-147`：默认 radius/bounce threshold（当前未接入）。

**验收**
- [x] 粒子不穿平面/球：`collision_1024` 固定 dt 10 帧、1024 粒子通过；CPU 多帧稳定性单测通过。
- [x] bounce 后速度方向/大小符合 restitution：plane/sphere CPU 单测和 1e-3 reference 通过。
- [x] CPU/GPU 一致（epsilon）：D3D11/Vulkan collision reference 均通过。
- [x] `hbg_vfx_tests` 54 cases / 21170 assertions；runtime 8 / 190；compiler 11 / 143（无工具链）/ 241（带 shaderc）；adapter 4 / 274；`hbg_tests` 248 / 32525。
- [x] `vfxbake --check` 6 个资产通过；VFX 相关 CTest 13/13；全量 CTest 34/34。

---

### V1.1.4 Curve / Gradient + over-life 块

- [x] 编译期 LUT 烘焙（256×1，RGBA16F/RGBA8）。
- [x] `color_over_life`/`alpha_over_life`/`size_over_life`。
- [x] `sample_curve`/`sample_gradient` 表达式。
- [x] LUT 写入 `.vfxb`；运行时上传纹理；双线性采样。
- [x] 单测：控制点 → LUT 字节；采样值与 CPU 参考一致。
- [x] 参考 `Editor/Models/Blocks/Implementations/Color/ColorOverLife.cs`、`Editor/Models/Blocks/Implementations/Attribute/AttributeFromCurve.cs`；`Editor/Models/Operators/Implementations/SampleCurve.cs`/`SampleGradient.cs`。

**实现**
- `binary.cpp:265-296`：每个 Curve/Gradient property 烘焙 `256 × RGBA16F`（2048B）到 LUTS；curve 使用 `.x`，gradient 使用 RGBA，控制点线性插值。
- `codegen.cpp:186/195`：每个 property 生成 LUT 采样函数；`:228/231` 声明 compute sampler `s_vfxCurveLut` stage 9、`s_vfxGradientLut` stage 10；`:415` 生成 init/update 的 texture binding；`:1196/1251` 附加到 kernel。
- 采样坐标：`u = (saturate(t) * 255 + 0.5) / 256`，行坐标 `(row + 0.5) / rowCount`，`texture2DLod(..., 0)` 由 sampler 双线性/clamp 采样。同一类型的多个 property 运行时打包为 `256 × N` 纹理，row 为 property 顺序。
- `runtime.cpp:931-966`：每类型创建一张 LUT 纹理；`:778` 按 kernel binding 绑定 stage 9/10；`setCurve`/`setGradient` 经 `updateLutTexture` 重传整张类型纹理。
- `cpu_reference.cpp:526-605`：CPU reference 实现 over-life；按 `saturate(age / max(lifetime, 1e-6))` 从控制点求值，`color_over_life` 写 RGB，`alpha_over_life` 写 alpha，`size_over_life` 写 size。
- `codegen.cpp:366`：`set_velocity_direction`/`set_velocity_random` 的零方向改用 `vfxSafeNormalize`，避免 Vulkan `normalize(0)` 产生 NaN（curve reference 的零速度初始化暴露）。
- `vfx/tests/assets/curve_1024.vfx.json/.vfxb`：1024 粒子、10 帧、color/alpha/size over-life；D3D11/Vulkan reference 通过。
- `vfx/tests/assets/curve_unlit.vfx.json/.vfxb`：保留 runtime LUT 上传/绑定测试；`expressions.vfx.json` 覆盖 `sample_curve`/`sample_gradient` 表达式的 LUT 代码生成。

**原码对齐**
- `ColorOverLife.cs:93`：`SampleGradient(gradient, age / lifetime)`；`ColorOverLife.cs:20-87` 的 mode/composition 当前简化为 RGB/alpha 分别覆盖（D14）。
- `AttributeFromCurve.cs:332-358`：OverLife 用 `age / lifetime` 采样；当前只实现 OverLife 覆盖，不支持 BySpeed/Random/Custom 和 composition（D14）。
- `SampleCurve.cs:7-27`、`SampleGradient.cs:7-27`：表达式分别输出 float/Vector4；`VFXCommon.hlsl:536-558` 的 baked texture 采样概念对应本实现的 LUT + 双线性采样。

**限制/偏差**
- 原 `ColorOverLife` 默认 ColorAndAlpha + Multiply composition；当前 `color_over_life` 只覆盖 RGB，alpha 由独立 block 控制。
- 当前 LUT 固定 256×1 RGBA16F；原项目使用 baked texture + curve metadata（128 宽）。当前不支持 wrap mode、随机/按速度采样和逐组件曲线（D15）。
- CPU reference 仅对 over-life block 做求值；通用表达式 DAG 仍由 compiler/GPU 侧验证（沿用 V1.0b.8 的 reference 边界）。

**验收**
- [x] LUT 字节确定性；`--check` 0 差异。
- [x] GPU 采样与 CPU 参考一致（epsilon）：D3D11/Vulkan `curve_1024` 均通过。
- [x] curve/gradient 纹理 stage 不与光照冲突：compute texture stage 9/10；raster main texture 仍为 stage 0。
- [x] `hbg_vfx_tests` 55 cases / 21177 assertions；runtime 8 / 192；compiler 13 / 175（无工具链）/ 273（带 shaderc）；adapter 4 / 274；`hbg_tests` 248 / 32525。
- [x] `vfxbake --check` 7 个资产通过；VFX 相关 CTest 15/15；全量 CTest 36/36。

---

### V1.1.5 V1.1 验收

- [x] 双后端 golden：bursts、forces、collision、curves。
- [x] CPU reference 场景（固定 seed/dt）。
- [x] 性能记录（粒子数/耗时）。
- [x] 文档回填。

**实现**
- `vfx/tests/assets/vfx_bursts.gltf`、`vfx_forces.gltf`、`vfx_collision.gltf`、`vfx_curves.gltf` 及同名 `.hdrp.json`：固定 640×360 相机和 VFX 实例；分别渲染 `spawner_bursts`、`forces_1024`、`collision_1024`、`curve_visual`。
- `vfx/CMakeLists.txt`：新增 `add_vfx_feature_golden()`，为四个场景注册 D3D11/Vulkan 各自的 `hbg_vfx_golden_{bursts,forces,collision,curves}_{d3d11,vulkan}`；`run_vfx_golden.cmake` 支持 `HBG_FRAMES`，四个场景分别为 90/30/30/300 帧。
- `vfx/tests/golden/vfx_{bursts,forces,collision,curves}_{d3d11,vulkan}.png`：逐后端 golden；D3D11 与 Vulkan 的 bursts/collision/curves 像素一致，力场因浮点累积有 155 个像素 1 LSB 差异，因此不共用严格 0-diff 图。
- `vfx/tests/assets/curve_visual.vfx.json/.vfxb`：单粒子、local space、`playRate` 0.15–0.85 的 8 个实例；同一 color/alpha/size LUT 在不同 age 比例下形成可见的蓝→绿→棕渐变和尺寸变化，避免 `curve_1024` 全部重叠导致的纯白块。
- CPU reference：沿用 `reference_1024`、`forces_1024`、`collision_1024`、`curve_1024` 的固定 `seed=0`、`dt=0.1`、10 帧逐粒子 1e-3 对照；Burst 使用 `spawner_bursts` 的固定 dt 序列单测（首帧 14，后续 2,2,2,2,0,0,0）。
- 性能：`--render-test=<feature scene> --vfx-test --frames=300 --width=1920 --height=1080 --no-vsync`，从 `render performance` 读取 `avg_frame_ms/fps/vfx_*`；Debug 构建，仅记录，不设门槛。

**原码对齐**
- Burst：`VFXSpawnerBurst.cs`、`VFXSpawnerConstantRate.cs`；固定 dt 序列参考 `Tests/SRPTests/Packages/com.unity.testing.visualeffectgraph/Tests/Runtime/VFXSpawnerTests.cs:460`。
- 力场：`Gravity.cs:39-45`、`Drag.cs:50-64`、`Force.cs:61-67`、`Turbulence.cs:67-78`；V1.1.2 的 CPU/GPU 参数顺序与 curl noise 保持不变。
- 碰撞：`CollisionBase.cs:123-147`、`CollisionPlane.cs`、`CollisionSphere.cs`；V1.1.3 的 kill/bounce/stick、friction/restitution 语义不变。
- Curve/Gradient：`ColorOverLife.cs:93`、`AttributeFromCurve.cs:332-358`、`SampleCurve.cs:7-27`、`SampleGradient.cs:7-27`、`VFXCommon.hlsl:536-558`；V1.1.4 的 LUT 双线性采样不变。

**性能记录（1920×1080，300 帧，Debug）**

| 场景 | D3D11 | Vulkan | 粒子/绘制/派发 |
|---|---|---|---|
| bursts | 4.344 ms / 230.2 fps | 6.126 ms / 163.2 fps | 22 / 1 / 5 |
| forces | 3.765 ms / 265.6 fps | 5.698 ms / 175.5 fps | 1024 / 1 / 5 |
| collision | 4.255 ms / 235.0 fps | 5.300 ms / 188.7 fps | 1024 / 1 / 5 |
| curves（8 实例） | 4.454 ms / 224.5 fps | 7.129 ms / 140.3 fps | 8 / 8 / 40 |

**验收**
- [x] 四个 feature golden 在 D3D11/Vulkan 均逐字节稳定；`vfx_scene` 原有 golden 未回归。
- [x] 跨后端像素差异：bursts 0、collision 0、curves 0；forces 155 像素最大 1 LSB，已用每后端 golden 固定，并由 `forces_1024` CPU/GPU 1e-3 reference 保证运动正确性。
- [x] CPU reference 固定 seed/dt 通过；Burst 固定 dt 总数与逐帧序列通过。
- [x] 性能数据已记录；无崩溃/超时，未设硬门槛。
- [x] `vfxbake --check` 8 个资产通过；`hbg_vfx_*` CTest 22/22；全量 CTest 44/44。

**限制/偏差**
- Feature golden 采用后端各自 golden，不把 D3D11/Vulkan 截图强制 0-diff；跨后端运动正确性由 CPU/GPU reference 负责。
- `curve_visual` 用 8 个 `playRate` 实例展示 LUT，而不是在原粒子系统内随机化 age；`curve_1024` 仍覆盖同一系统的 1024 粒子 LUT 求值。
- 性能数据来自 Debug 构建和单场景，未覆盖 10k/100k/1M 或 Release；V1 只记录，不设门槛。
- `spawner_bursts` 截图容量为 64、1.5 秒时 22 粒子；1024 粒子 CPU reference 只在 `reference_1024` 等场景覆盖。

---

## V1.2 GPU 事件 + 排序

### V1.2.1 Rendering data / KVP / indirection

- [x] `renderKVP` buffer（counter + KVP）；`indirection` buffer。
- [x] `output_update`：alive 才注册 KVP；无排序时写顺序索引。
- [x] sort 后写 indirection；VS 读 `indirection[slot]`（V1.2.2 完成）。
- [x] 单测：KVP 内容/计数/索引范围。
- [x] 参考 `Shaders/Temp/Data/RenderingData.hlsl:4-61`、`RenderingDataDescriptionWriter.cs:54-96`。

**实现**
- `.vfxb` binary version 提升到 **2**；每 system 增加 buffer kind **7=renderKVP**：`(1 + capacity) * 8`B，word0=key、word1=counter，随后 `capacity` 个 `{key:f32, index:u32}`。counter 与 KVP 同 buffer，布局对齐原 `VFXSortingData`/`RenderKVP`；`indirection` 仍为每粒子 `u32`。
- `output_update` 绑定 attributes(0,RO)、renderKVP(1,RW raw)、indirection(2,RW raw)、renderCount(3,RW uint)：先 atomic 增加 renderCount，再写 KVP `{0.0, particleIndex}` 与顺序 indirection `[previousCount]=particleIndex`；只有 `alive` 粒子注册。
- `spawner` 清零 renderKVP counter；`fill_indirect` 在写完 32B args 后把 renderCount 复制到 KVP counter，保证 readback 时 `renderKVP.count == renderCount == alive`。KVP key 暂为 0，V1.2.2 接入 distance/custom 排序键后替换。
- VS 现有 `s_vfxIndirection[particleSlot]` 保持不变；无排序时 indirection 为顺序索引，因此输出路径不需要分支。
- 参考原码：`Shaders/Temp/Data/RenderingData.hlsl:4-61`（KVP/counter/indirection）、`Shaders/Temp/Templates/OutputUpdate.hlsl:14-33`（alive 才注册）、`Editor/NewCompiler/Data/DescriptionWriter/RenderingDataDescriptionWriter.cs:54-96`（counter/keys buffer 分离语义）、`Shaders/Sort.compute:65-120`（key 相等按 index 升序的 tie-break 留给 V1.2.2）。
- 验证：compiler 测试检查 8 个 buffer、binary v2、outputUpdate 4 个 binding、KVP/indirect 内容；runtime Mock 检查 8 个 buffer 与逐 kernel binding；`vfx_reference` 读回 KVP counter/全部 index，检查 `kvp_count == render_count == draw_count`、索引范围/唯一性且与 indirection 集合一致。

**验收**
- [x] `alive` 与 KVP count 一致；indirection 覆盖 [0,alive)。
- [x] 无排序时画面与 V1.0b 一致（22/22 VFX CTest 全部通过，feature golden 未重生成）。
- [x] `vfxbake --check` 8 个资产通过；全量 CTest 44/44。

---

### V1.2.2 GPU sort（bitonic + merge）

- [x] 迁移 `Sort.compute` 语义：KVP、tie-break、descending/ascending。
- [x] 128/1024/2048/4096 单 dispatch；更大走 pre-pass + merge。
- [x] 每 output 排序或全局排序（V1 共享 system 排序/indirection；output sort 必须与 system 一致）。
- [x] 单测：随机 KVP 排序与 CPU std::sort 比较；稳定性。
- [x] 参考 `Shaders/Sort.compute:1-120,240-345`。

**实现**
- `.vfxb` binary version **3**；每 system 固定 **9** 个 buffer，新增 **kind 8=sortScratch**（`capacity * 8`B）。`renderKVP` 保持 `(1 + capacity) * 8`B；KVP 仍为 `{key:f32, index:u32}`，key 相等按 index 升序 tie-break。
- `output_update` 生成 sort key：`distance` 用 world/effect 空间的平方相机距离（需 `u_vfx_world`、`u_global_cameraPos`）；`custom` 读取系统声明的 scalar `sortKey` 属性，缺失时编译报错。KVP key 不再固定 0。
- 排序 kernel：`capacity <= 4096` 用一次 4096-element bitonic dispatch，LDS `{key,value}`，直接写 indirection；`capacity > 4096` 每个 4096-KVP chunk 单独 dispatch bitonic pre-pass 写 sortScratch（chunk base 通过 `u_vfx_ids.z` 传入），随后按 4096→8192→… 做 merge；merge 用 64 线程、二分 partition 求 rank，支持 descending 与 tie-break，最终写 indirection。
- `spawner` 清 KVP counter；`fill_indirect` 继续同步 renderCount 到 KVP counter；`sortPasses` 诊断统计 bitonic/pre-pass/merge dispatch。
- KVP/sortScratch 在 bgfx 适配层以 `StorageVertex` 创建；大于 64KB 的 DynamicIndex storage 在 Vulkan 作为 compute storage 会触发 device lost。
- 测试：CPU `vfx::reference::sortKVP` 对 4096 随机 key/value 与 `std::sort` 升/降序比较；compiler 检查 binary v3、9 buffer、sort kernel、custom key；runtime Mock 检查 sorted 8192 的 pre-pass×2 + merge 调度和 `sortPasses`；`vfx_reference` 对 1024（单 dispatch）、8192（pre-pass+merge）、262144（最大容量）校验 KVP key 与 indirection。
- 资产：`reference_sorted_{1024,8192,262144}.vfx.json/.vfxb` 覆盖单 dispatch、pre-pass+merge、最大容量；`sort_visual.vfx.json/.vfxb` + `vfx_sort.gltf` 提供排序 golden，`sort_visual_off.vfx.json/.vfxb` + `vfx_sort_off.gltf` 提供 on/off 对照。
- 参考原码：`Shaders/Sort.compute:65-120`（KVP/tie-break）、`:159-238`（bitonic LDS）、`:240-345`（merge）、`Shaders/Temp/Data/RenderingData.hlsl:4-61`。

**验收**
- [x] 排序结果与 CPU 参考一致（key 1e-3；GPU KVP 排序后 indirection 完全一致）；1024/8192/262144 D3D11 与 Vulkan 通过。
- [x] 排序 on/off 画面可区分；`vfx_sort` 10 帧 D3D11 截图与无排序场景有 2304 像素差异，透明前后关系正确；`hbg_vfx_sort_on_off_d3d11` 自动比较两场景截图并拒绝相同输出。
- [x] 最大容量压力测试（262144）无越界/NaN；D3D11 与 Vulkan `vfx_reference` 均通过。
- [x] 全量 CTest **55/55**；`vfxbake --check` 13 个资产通过。
- 限制：V1 所有 output 共享 system 排序；custom key 使用 scalar `sortKey` 属性而非 output slot；>4096 pre-pass 为每 chunk 单 dispatch，优先正确性/稳定性，性能留到 V1.2.5。

---

### V1.2.3 GPU 事件 + prefix sum

- [x] GPU event list（counter/total/prefix/dropped/frameCount/index/attributes）。
- [x] 死亡生成/子发射器；事件属性传递。
- [x] 跨 system prefix sum（`VFXPrefixSum.compute` 语义）。
- [x] 事件上限/丢弃策略；诊断计数。
- [x] 单测：单/多 system 事件数/属性。
- [x] 参考 `Shaders/Temp/Data/EventListData.hlsl:37-84`、`Shaders/VFXPrefixSum.compute:27-143`、`Editor/NewCompiler/Builder/GpuEventBuilder.cs`、`ParticleSystemBuilder.cs:565-580`。

**实现**
- 事件连接：`emit`/`receive` 按名称一对一；V1 单层 producer→consumer，receiver 不能有 CPU spawner。
- event buffer 为每 effect 一份全局 Storage `vfx.events`；每 link 5 个 header uints（`elementCount`/accepted `totalCount`/`prefix`/`dropped`/`frameCount`）+ `capacity` 个 source index + `capacity*16` payload words；payload 最多 4 个 `float4` slot。
- producer `update`：无 `trigger_event` 时 alive→dead 自动写 1 个事件；`trigger_event { event, count?, condition? }` 支持 count，未给 condition 时仍按死亡触发；每个粒子写入受 receiver capacity 限制，超出写 `dropped`。
- consumer `init`：读 `frameCount`/`prefix`，不依赖 CPU spawnCount；payload 在 init blocks 后覆盖 consumer 属性；`particleId = prefix + eventIndex`。
- 每帧调度：首帧/reset `event_reset`（task 12）→ 无 event 输入的 producer simulation → `event_prefix_sum`（task 11，128-thread Sklansky；写入 frameCount/prefix 并清 elementCount）→ consumer simulation。
- binary v4：EVENTS section 存 link producer/consumer/capacity/offset/name/payload/receive 映射；KERNELS 支持 global task 11/12；BINDINGS kind 2 绑定 `vfx.events`。
- 限制：单 effect、单层事件图、无 CPU event、payload 经 float4（uint/int/bool 精度受限）、总 event buffer ≤64 KiB；`Diagnostics::eventCount` 仅事件链数。

**实现位置**
- `vfx/src/compiler/codegen.cpp`：`CompiledEventLink`、`makeEventResetSource`、`makeEventPrefixSource`、producer append、`makeEventInitSource`。
- `vfx/src/compiler/binary.cpp`：binary v4 EVENTS/global kernels。
- `vfx/src/runtime/runtime_asset.*`：event link/buffer 解析、global program。
- `vfx/src/runtime/runtime.cpp`：`vfx.events` 创建、reset/prefix 调度、event binding。
- `vfx/src/reference/cpu_reference.cpp`：`simulateEventCount`；`src/render/vfx/vfx_reference.cpp` 的 `--vfx-event-reference` 读回校验。
- `vfx/tests/assets/vfx_event.*`、`vfx_events_count.*`、`vfx_events_multi.*`、`vfx_events_overflow.*`。

**验证**
- `hbg_vfx_compiler_tests` 15/15、251 assertions；事件 codegen/binary roundtrip/trigger count/错误映射。
- `hbg_vfx_runtime_tests` 10/10、226 assertions；event reset/prefix/consumer 调度、19 buffers、16 programs。
- `--vfx-event-reference` D3D11 + Vulkan：
  - `vfx_event` 16 个死亡事件，payload position/color 与 producer 一致。
  - `vfx_events_count` count=2，32 事件。
  - `vfx_events_multi` 双链 8+16，prefix `0/8` 连续，particleId base 正确。
  - `vfx_events_overflow` producer 128 → receiver capacity 64，accepted 64、dropped 64。
- 全量 CTest **64/64**；`vfxbake --check` **17** 个资产通过。

**验收**
- [x] 事件链（A 死亡 → B 生成）数量/属性正确。
- [x] 双 system 事件并发时 prefix sum 连续。
- [x] CPU reference/读回一致（epsilon）。

---
### V1.2.4 CPU 事件 + 事件属性

- [x] `Effect::sendEvent(name, attributes)`。
- [x] CPU 事件缓冲/前缀和；下游 init 读取。
- [x] 事件属性类型/默认值校验。
- [x] 单测：事件顺序/属性映射。

**实现**
- 只有 `receive`、没有同名 `emit` 的 system 编译为 CPU event link；consumer 不能有 CPU spawner，`receive.attributes` 必须显式（最多 4 个 payload，consumer 属性 → CPU EventAttribute 名称）。
- `Effect::sendEvent` 按调用顺序入 per-effect pending queue；容量 = receiver 64 对齐 capacity，超出写 `dropped`；未知事件名/类型不兼容通过 error callback 报告且不入队。
- 缺少 event attribute 时使用 consumer 属性默认值；`float/int/uint/bool/float2/3/4` 严格匹配 payload 类型。
- runtime 将 pending 写入 `vfx.cpu.events` staging buffer（Storage + `initialData`/COMPUTE_READ，`updateBuffer` 每帧上传），dispatch `event_cpu_upload`（task 13）复制 CPU link 范围到全局 `vfx.events`；随后统一 `event_prefix_sum` + event init。
- binary v5：CPU link 的 `producerSystem=0xffffffff`；EVENTS payload 记录增加 `type/default`（28B）；BINDINGS kind 3 绑定 CPU staging。
- 限制：每 receive 单 CPU 事件名/单 payload；CPU receiver 不能有 CPU spawner；总 event buffer ≤64 KiB；精确诊断仍需读 event buffer。

**实现位置**
- `vfx/src/compiler/codegen.cpp`：CPU link 构建、`makeEventCopySource`、`EventCopy` task 13。
- `vfx/src/compiler/binary.cpp`、`vfx/src/runtime/runtime_asset.*`：binary v5 CPU link/payload metadata。
- `vfx/src/runtime/runtime.cpp`：`sendEvent`、CPU pending/staging upload、event binding/copy dispatch。
- `vfx/tests/assets/vfx_cpu_event.*`。
- `src/render/vfx/vfx_reference.cpp` 的 `--vfx-cpu-event-reference`。

**验证**
- `hbg_vfx_compiler_tests` 16/16、275 assertions（CPU link/copy/binary roundtrip）。
- `hbg_vfx_runtime_tests` 11/11、243 assertions（CPU pending、类型/未知事件错误、reset/upload/prefix/consumer 调度）。
- `hbg_vfx_tests` 56/56、37563；adapter 4/4、274。
- `--vfx-cpu-event-reference` D3D11 + Vulkan：发送 66 次，accepted 64、dropped 2；前 3 个事件属性/顺序正确，缺失 color 使用默认白色，错误类型被拒绝。
- 全量 CTest **66/66**；`vfxbake --check` **18** 个资产通过。

**验收**
- [x] 事件触发数量与调用次数一致（有上限时记录丢弃）。
- [x] 属性映射正确（CPU/GPU）。

---
### V1.2.5 V1.2 验收

- [x] 事件/排序 golden（D3D11/Vulkan）。
- [x] 确定性（固定 seed/dt/frame）。
- [x] 性能记录（sort/事件）。
- [x] 文档回填。

**实现**
- `vfx/tests/assets/vfx_events_visual.vfx.json/.vfxb`：连续 GPU 死亡事件样例；Emitter 以 24/s + 初始 8 个 Burst 生成，lifetime 0.1s，死亡自动 emit `position/color`；Receiver lifetime 0.5s，event init 后以 size 0.40/alpha 0.9 输出 alpha quad。30 帧时约 10-12 个接收粒子可见，避免一次性 Burst 在 golden 中只剩空场景。
- `vfx/tests/assets/vfx_events.gltf/.hdrp.json`：固定相机 `[0,1,8]`、`seed=0`、`playRate=1.0`；`vfx/tests/golden/vfx_events_{d3d11,vulkan}.png` 为 30 帧逐后端 golden，两后端当前逐字节一致。
- `vfx/tests/run_vfx_determinism.cmake`：同一场景、同一后端、同一 `--frames` 连续启动两次，固定 `--width=640 --height=360 --no-vsync`，逐字节比较两次 PNG；`vfx/CMakeLists.txt` 为 sort/events 各注册 D3D11/Vulkan 确定性测试。render-test 固定按 `frame/60` 推进时间，effect seed 来自 `.hdrp.json`，因此 seed/dt/frame 均固定。
- `src/render/vfx/vfx_reference.cpp`：reference 增加 `submit_ms_per_frame`、`steady_ms_per_frame`（跳过前 2 帧管线初始化）、`first_frame_ms`、`readback_ms`、`dispatches`、`sort_passes` 日志；用于 1024/8192/262144 大规模 sort 记录。

**性能记录（1920×1080，300 帧，Debug，full pipeline）**

| 场景 | D3D11 | Vulkan | VFX 粒子/绘制/派发 |
|---|---|---|---|
| events（2 system，连续事件） | 1.485 ms / 673.4 fps | 4.252 ms / 235.2 fps | 64 / 2 / 11 |
| sort（64 粒子，distance） | 1.529 ms / 653.9 fps | 4.067 ms / 245.9 fps | 64 / 1 / 6 |

**大规模 sort reference（10 帧固定 dt=0.1、64×64，Debug）**

| capacity | D3D11 steady | Vulkan steady | D3D11 readback | Vulkan readback | dispatches / sort passes |
|---|---|---|---|---|---|
| 1024 | 0.731 ms | 1.185 ms | 4.345 ms | 6.459 ms | 6 / 1 |
| 8192 | 1.113 ms | 1.100 ms | 15.827 ms | 7.645 ms | 8 / 3 |
| 262144 | 6.129 ms | 8.121 ms | 70.873 ms | 64.184 ms | 75 / 70 |

**验收**
- [x] sort 原 golden 未回归，events 新增 D3D11/Vulkan golden；`vfx_events` 两后端 0 字节差异；sort on/off 仍为 2304 像素差异。
- [x] `hbg_vfx_determinism_{sort,events}_{d3d11,vulkan}` 全部通过：同 seed/dt/frame 两次渲染逐字节一致。
- [x] `hbg_vfx_compiler_tests` 16/16、275；runtime 11/11、243；core 56/56、37563；adapter 4/4、274。
- [x] VFX 相关 CTest **47/47**；全量 CTest **73/73**；`vfxbake --check` **19** 个资产通过。
- [x] 性能数据已记录；Debug 构建，仅记录，不设硬门槛。

**原码对齐**
- Sort：`Shaders/Sort.compute:65-120` 的 KVP/等 key tie-break、`:159-238` bitonic、`:240-345` merge；本实现保持升/降序与 value 升序 tie-break。
- Event：`Shaders/Temp/Data/EventListData.hlsl:8-84` 的 `elementCount/totalCount/prefix/spawnCount` 语义与 source index 读取；`VFXGPUEvent.hlsl` 的 GPU event append 行为。
- 随机/确定性：`Shaders/VFXCommon.hlsl:396-424` 的 `WangHash`；本实现 CPU reference 与生成 HLSL 使用同一 hash 形制，固定 effect seed 后逐帧可重复。

**限制/偏差**
- events 性能样例是“连续死亡事件 + 单层 receiver”，不覆盖多层事件图；大规模 sort reference 排除前 2 帧管线初始化，且 readback 单独计时，不代表纯 GPU sort 时间。
- 事件 golden 是单帧 30 帧画面，事件正确性由 event reference（frameCount/prefix/payload/consumer attrs）和确定性测试共同保证；CPU event 无独立可视 golden，由 `--vfx-cpu-event-reference` 覆盖。
- 未设性能门槛；Release、10k/100k/1M 粒子和 GPU timer 分解留到 V1.4。

---

## V1.3 子图 / 自定义 HLSL / 图校验

### V1.3.1 子图展开

- [x] 纯函数子图：输入/输出、参数重映射。
- [x] 递归深度 ≤16；循环检测。
- [x] 子图内 block/表达式展开到父图。
- [x] 单测：展开前后 CPU reference/生成 shader 等价。
- [x] 参考 Unity `VFXSubgraph*`（按需复核）。

**实现**
- `vfx/src/core/subgraph.cpp/.h`：解析子图 `inputs/outputs`，校验名称/类型/默认值/重复项；表达式调用 `{"op":"subgraph",...}` 与 block 调用 `{"type":"subgraph",...}` 都在 `parseGraphJson` 阶段展开。
- 输入引用支持 `{"op":"input","name":"X"}`（`arg` 同义）和同名 `property` 优先绑定；未传 args 时使用 default，缺失参数报错。表达式 output 必须提供 `value`，多 output 通过 `output` 选择。
- block 子图按父 block 的 init/update stage 展开对应块列表，可递归嵌套；展开后子图结构从 `Graph::subgraphs` 清除，`.vfxb` 不保留子图。
- 递归深度上限 16；定义级 DFS 检测未使用子图的循环/缺失引用，展开时再次检测调用栈；错误路径覆盖 `subgraphs[i]`、`systems[i].init/update.blocks[j]`、args/output。
- `vfx/src/core/asset_json.cpp`：新增 `subgraph`/`input`/`arg` JSON op、`subgraph` block spec，并在表达式校验前调用 flatten；`vfx/src/compiler/codegen.cpp` 对手工构造的 Graph 再做一次 flatten 防护。

**验证**
- `hbg_vfx_tests` 61/61、37628 assertions：纯函数/block 子图、默认参数、嵌套、多 output 选择、循环/过深/缺失参数。
- `hbg_vfx_compiler_tests` 17/17、301 assertions：subgraph 与手写展开图的 kernel/output shader source 完全一致。
- CPU reference：同 seed/dt/frame 下 subgraph 展开与手写展开的 position/velocity/color/age/seed 一致。
- 参考 Unity：`VFXSubgraphBlock.cs:135-168`（RecreateCopy/嵌套）、`:196-216`（PatchInputExpressions/recursiveSubBlocks）；`VFXSubgraphOperator.cs:14-34`（TransferExpressionToParameters）、`:214-227`（input/output properties）。
- 临时 subgraph probe 经 `vfxbake` dual-backend 烘焙成功，两次输出二进制 0 差异；VFX CTest 47/47，`vfxbake --check` 19 个既有资产通过。
- V1.3.4 已补 checked-in 子图资产、D3D11/Vulkan golden 与 fixture 编译测试（见 V1.3.4）。

**验收**
- [x] 展开前后 CPU reference/生成 shader 一致（epsilon/0-diff）。
- [x] 循环/过深/缺参报错带 JSON 路径。

**限制/偏差**
- 子图在 parse 阶段一次性展开，不支持运行时热重载后的增量重展开；V1.3.3 处理重建。
- `inputs/outputs.type` 已解析但未强制约束表达式推断类型；自定义属性合并/Unity 的 context subgraph 尚未实现。
- block 子图的 `outputs` 不参与父 block；只展开 init/update blocks。

---

### V1.3.2 自定义 HLSL block

- [x] 字段：`code`/`reads`/`writes`。
- [x] 校验：禁用 include/define/buffer/uniform；只读写声明属性。
- [x] 注入宏：`VFX_ATTR`/`VFX_SET_ATTR`/`VFX_PROP`/`VFX_TIME`/`VFX_DT`。
- [x] 生成 kernel 作用域；shaderc 双后端门禁。
- [x] 单测：合法/非法代码；属性读写检查。
- [x] 参考 `Editor/Models/Blocks/Implementations/HLSL/CustomHLSL.cs`（语义/限制参考，不复制）。

**实现**
- `vfx/src/core/custom_hlsl.{h,cpp}`：解析/校验 `code`/`reads`/`writes`，属性存在性与重复检查，32 KiB 上限，HLSL 行/列错误；`VFX_ATTR` 读、`VFX_SET_ATTR` 写、`VFX_PROP`/`VFX_LIFETIME_RATIO` 引用检查；禁止 `#` 预处理、buffer/uniform/texture/sampler、`a_*`/`attributes.Load_*`/`Store_*`、`s_vfx*`/`u_vfx*`、`return`/`discard`/barrier 等。
- `vfx/src/core/asset_json.cpp`：init/update 均接受 `custom_hlsl`；parse 完成后在 `validateGraphExpressions` 前执行 `validateGraphCustomHlsl`。
- `vfx/src/compiler/codegen.cpp`：`VFX_SET_ATTR(name, value)` 宏；init/update 自定义代码包在显式 `{ ... }` 作用域；`collectBlockUsage` 合并声明 reads/writes；手写 Graph 也经同一校验。
- `vfx/tests/test_vfx_asset_json.cpp`：合法 init/update、默认参数/属性宏、注释中的 `#`；非法定义、未知属性、缺 reads/writes、直接 `a_*`、写只读 `VFX_ATTR`、`VFX_LIFETIME_RATIO` 缺声明、uniform/buffer/StructuredBuffer/return。
- `vfx/tests/test_vfx_compiler.cpp`：生成 shader 含宏/作用域/自定义代码；`compileGraph` 合法样例 dxbc+spirv 烘焙。
- 参考 Unity：`Editor/Models/Blocks/Implementations/HLSL/CustomHLSL.cs:11-42`（void/VFXAttributes/input 校验）、`:389-436`（`BuildSource` 初始化 `VFXAttributes`、调用函数、写回属性）、`:438-447`（`BuildCustomCode` 函数名后缀）；V1 采用宏化语句片段，不复制 Unity 的完整 HLSL parser/function editor。

**验证**
- `hbg_vfx_tests` 62/62, 37665 assertions；合法/非法样例通过。
- `hbg_vfx_compiler_tests` 19/19, 414 assertions；设置 `VFX_TEST_SHADERC`/`VFX_TEST_BGFX_SOURCE` 时合法样例 dxbc+spirv 烘焙通过。
- VFX CTest 47/47；全量 CTest 77/77；`vfxbake --check` 19 个既有资产通过；临时 custom HLSL probe 双后端烘焙成功且两次输出 0 差异。
- V1.3.4 已补 checked-in `custom_hlsl_visual` 资产、D3D11/Vulkan golden 与 fixture 编译测试（见 V1.3.4）。
- 自定义代码只通过声明的 `reads`/`writes` 访问属性；直接生成变量访问被拒绝。

**验收**
- [x] 合法样例双后端编译通过；非法样例明确报错。
- [x] 自定义代码只影响声明属性。

**限制/偏差**
- 没有完整 HLSL parser/type checker；语法与类型由 shaderc 双后端把关，校验器只覆盖安全性/声明约束。
- 不支持函数定义、include、平台特定 intrinsic、output stage 自定义块；`VFX_ATTR` 的写检测覆盖直接赋值/复合赋值/自增和 swizzle/索引左值；通过 HLSL 出参/内在函数间接写入不在 V1 保证范围。

---

### V1.3.3 图校验 / 错误定位 / hot reload

- [x] 所有错误带 JSON 路径 + 源码/hlsl 行号（如可得）。
- [x] `vfxbake --check` 在 CI 上校验测试资产。
- [x] 运行时 `Runtime::reloadEffect` 或 effect 重建（V1 支持销毁重建即可）。
- [x] 编辑器后续直接链接 `vfx_compiler`。

**实现**
- `vfx/include/vfx/asset.h:14-43`：`ErrorLocationKind`、`ParseError{path,message,source,line,column,location}`、`formatParseError`、`locateJsonPath`；JSON 错误按路径定位到源码行列，HLSL 错误使用代码内行列。
- `vfx/src/core/asset_json.cpp:117-411,2199-2233`：`JsonLocationFinder` 扫描合法 JSON 的对象/数组路径；`parseGraphJson` 为所有非 HLSL 错误补 `source/line/column`，缺失字段回退到根位置；语法错误从 nlohmann `parse_error` 提取行列。
- `vfx/src/core/custom_hlsl.cpp:499-588`：`CustomHlslError` 结构化错误，`.code` 路径 + HLSL 行列；`formatCustomHlslError` 供 codegen 使用。
- `vfx/src/core/asset_json.cpp:613-636`：自定义 HLSL 错误映射为 `ParseError`，非 `.code` 错误保留 JSON 路径并由定位器补行号。
- `vfx/src/compiler/compiler.cpp:47-59,309-343`：`compileGraph(json, options, sourceName)`，解析/代码生成错误进入 `CompileResult.errors`；`captureCompilerError` 将 codegen 的 `path: message` 补充 JSON 行列。
- `vfx/src/runtime/runtime.cpp:1365-1455`：`Runtime::reloadEffect(Effect*, Asset, ...)`、`reloadEffect(Effect*, path, ...)`；先创建新 effect，成功后才销毁旧 effect；`pruneAssets` 释放无 effect 引用的资产；`refreshDiagnostics` 统一统计。
- `vfx/include/vfx/runtime.h:77-96`：公开 reload API；`vfx/CMakeLists.txt:22` 提供 `vfx_compiler` alias，编辑器可直接链接 `hbg_vfx_compiler`/`vfx_compiler`。
- `vfx/CMakeLists.txt:78-97`：有 `HBG_SHADER_COMPILER` 时注册 `hbg_vfx_compiler_tests` 的双后端环境，并新增 `hbg_vfx_asset_check` 调用 `vfxbake --check`；CI 使用默认 `HBG_BUILD_SHADER_TOOLS=ON` 或设置 `HBG_SHADERC` 即会执行。
- `vfx/tests/test_vfx_asset_json.cpp:172-210`：JSON 行列/源码/格式化测试；`:564-654` 自定义 HLSL 行列测试。
- `vfx/tests/test_vfx_runtime.cpp:115-203`：成功 reload（旧资源销毁、新资源存活）、失败 reload（旧 effect 保留）测试。
- 验证：`hbg_vfx_tests` **63/63, 37701 assertions**；`hbg_vfx_runtime_tests` **13/13, 277**；`hbg_vfx_compiler_tests` **19/19, 421**；VFX CTest **48/48**、全量 CTest **79/79**；`vfxbake --check` **19** 个资产通过。

**参考**
- `Editor/Models/VFXErrorManager.cs:20-49,68-82,120-145`：`VFXErrorManager` 按 model 收集编译/失效错误；`ReportError` 持有 `model/type/error/description`；`RegisterError` 去重并更新 dirty model。本项目用 JSON 路径替代 model，用 `ParseError` 保留错误类型/位置。
- `Editor/Models/VFXGraph.cs:156-197`：`OnCompileResource` 后调用 `errorManager.RefreshCompilationReport()`；`:1447-1457` `Compile()` 调用编译器后 `errorManager.GenerateErrors()`；`:1664-1674` `CompileAndUpdateAsset` 先 `ClearPreviewAssets()` 再替换 asset desc。
- `Editor/Models/Blocks/Implementations/HLSL/CustomHLSL.cs:268-286`：自定义 HLSL 的 validator 错误逐条注册到 `VFXErrorReporter`；本项目映射为 `.code` 的 HLSL 行列。

**验收**
- [x] 错误路径测试覆盖 ≥10 类（根/版本/属性/重复名/block type/block field/表达式 op/表达式引用/容量/burst/output/sort/子图/custom HLSL 等）。
- [x] 热重载后状态/资源无泄漏：Success reload 创建 9 buffer/7 program/1 texture，旧 effect 销毁后 live 回到 9/7/1，最终 destroy 后 0；失败 reload 保持旧 effect 和原 live 资源。

**限制/偏差**
- V1 的 reload 是销毁重建，不保留旧 effect 的播放时间、pending 事件或属性覆盖；需要播放状态时由调用方在 reload 后重设。
- JSON 定位以字段 key 为位置；缺失 required 字段无可定位 key 时回退到根/首行。shaderc 生成 shader 的日志不含 JSON 行，无法反推到 `.vfx.json` 行号。
- Unity 通过 importer/reimport + `VisualEffectAssetDesc` 更新；本项目是显式 runtime API。编辑器当前不在仓库内，仅保证 `vfx_compiler` 静态库/CMake alias 可直接链接。

---

### V1.3.4 V1.3 验收

- [x] 子图/自定义 HLSL 的双后端 golden/编译测试。
- [x] 图校验测试。
- [x] 文档回填。

**实现**
- `vfx/tests/assets/subgraph_visual.vfx.json/.vfxb`：`Tint` 纯函数子图 + `SeedParticles` block 子图；block 子图调用纯函数子图，init 展开后生成 24 个 alpha quad；`vfx/tests/assets/custom_hlsl_visual.vfx.json/.vfxb`：custom HLSL 按 `position.x` 设置颜色并写 alpha/size，init/update 走正常 kernel。
- `vfx/tests/assets/vfx_subgraph.gltf/.hdrp.json`、`vfx/tests/assets/vfx_custom_hlsl.gltf/.hdrp.json`：固定相机 `[0,0,8]`、seed 0、30 帧。
- `vfx/tests/golden/vfx_subgraph_{d3d11,vulkan}.png`、`vfx/tests/golden/vfx_custom_hlsl_{d3d11,vulkan}.png`：逐后端 golden；两场景当前 D3D11/Vulkan 均逐字节一致。
- `vfx/CMakeLists.txt`：`add_vfx_feature_golden(subgraph ...)`、`add_vfx_feature_golden(custom_hlsl ...)` 共 4 个 CTest；`hbg_vfx_compiler_tests` 新增 fixture 双后端 `compileGraphFile` 测试；`hbg_vfx_asset_check` 批量校验 21 个 `.vfx.json`。
- `vfx/tests/test_vfx_asset_json.cpp`：新增 subgraph 声明校验（未知 input type、重复 input/output、缺 output value）与 custom HLSL 字段校验（缺/非字符串 `code`、`reads` 非数组、`writes` 元素非字符串）。
- 参考 Unity 原码：`Editor/Models/Blocks/VFXSubgraphBlock.cs:135-168,196-216`（子图复制/嵌套/PatchInputExpressions）、`Editor/Models/Operators/VFXSubgraphOperator.cs:14-34,214-227`（输入/输出重映射）、`Editor/Models/Blocks/Implementations/HLSL/CustomHLSL.cs:11-42,389-447`（自定义 HLSL 校验与 source 生成）。

**验证**
- `hbg_vfx_tests` **65/65, 37725 assertions**；`hbg_vfx_runtime_tests` **13/13, 277**；`hbg_vfx_compiler_tests` **20/20, 439**（带 shaderc 双后端）。
- VFX CTest **52/52**、全量 CTest **84/84**；`vfxbake --check` **21** 个资产通过。
- `hbg_vfx_golden_subgraph_{d3d11,vulkan}`、`hbg_vfx_golden_custom_hlsl_{d3d11,vulkan}` 均通过；两组 golden 的 D3D11/Vulkan 文件 SHA-256 相同（0 字节差异）。
- 子图 fixture 与自定义 HLSL fixture 均通过 shaderc dxbc+spirv 双后端重新烘焙；`--check` 与 checked-in `.vfxb` 逐字节一致。

**验收**
- [x] 子图/自定义 HLSL 双后端 golden、编译 fixture 与 asset-check 覆盖。
- [x] 图校验测试覆盖子图声明、参数、循环/深度与 custom HLSL 字段/安全约束。
- [x] 文档、golden、测试资产与实现位置回填。

**限制/偏差**
- V1.3.4 golden 只验证视觉链路；custom HLSL 没有独立 CPU reference，语义安全性仍由 HLSL 声明校验 + 双后端编译 + golden 共同保证。
- 子图仍不支持 context subgraph/自定义属性合并；custom HLSL 仍是语句片段子集，不支持函数/include/完整 HLSL parser。
- golden 对驱动/shader 变化敏感；重新生成时必须同时保留 D3D11/Vulkan 两张图并记录跨后端差异。

---

## V1.4 Lit quad + 发布

### V1.4.1 Lit shading pack

- [x] `shaders/vfx/vfx_lit_quad.{vs,fs}.sc`：
  - [x] `SurfaceData` 填充；
  - [x] `hbgEvaluateLightLoop`；
  - [x] `hbgSampleDirectionalShadow`；
  - [x] contact shadow/环境/cluster 可选绑定。
- [x] Adapter `bindLighting`：复用 `PassUniforms`/`GlobalUniforms`/`ObjectUniforms`/`ClusterUniforms`/`EnvironmentUniforms`、stage 5/6/7/8/9/10/11/14/15；VFX attribute/indirection buffer 使用 stage 12/13。VFX lit variant **不得定义** `HBG_SHADOW_POINT_SAMPLER`/`HBG_SHADOW_AREA_SAMPLER`，否则 point/area depth sampler（12/13）与 buffer 冲突；需要时先重排 stage。
- [x] 提取共享 `bindForwardLighting()`，forward 与 VFX 共用（避免漂移）。
- [x] Lit output 属性：metallic/roughness；默认值。
- [x] 单测：stage/绑定表；lit program 存在。
- [x] 参考 `shaders/lighting/light_loop.sc:339`、`shaders/material/surface_data_types.sc:6-37`、`shaders/passes/forward.fs.sc:1-36`、`forward_renderer.cpp:6270-6310`。

**实现**
- `shaders/vfx/vfx_lit_quad.vs.sc/.fs.sc/.def.sc`：参考/编译检查模板；VS 使用 attribute/indirection stage 12/13，FS 填充 `SurfaceData`、采样 contact shadow、执行 `hbgSampleDirectionalShadow` + `hbgEvaluateLightLoop`。`vfx/src/compiler/codegen.cpp` 的 `makeOutputFragmentSource` 生成同结构的 lit FS；`shaders/vfx/vfx_lit_quad.*` 已用 shaderc 手动验证 dxbc/spirv（D3D 走 skip-optimization，避免 FXC 对 light loop 的保守未初始化告警）。
- `vfx/include/vfx/graph.h`：`OutputDesc` 增加 `metallic`/`roughness`（默认 0/0.5）及可选 float property 名；`asset_json.cpp` 校验范围/property 类型；`codegen.cpp` 在 lit FS 中生成常量或 `u_vfxProp_*` 读取，`OutputProgram` 保留解析结果。
- `src/render/passes/forward_lighting.h`：新增 `ForwardLightingContext` + 共享 `bindForwardLighting()`；`forward_renderer.cpp` 的 `drawForwardBatch`/`renderForwardPass` 改用该 helper，VFX adapter 复用同一函数。
- `src/render/vfx/hbg_vfx_backend.cpp`：`bindLighting` 从 `FrameContext::lighting` 读取 forward lighting context，绑定 stage 5/6/7/8/9/10/11/14/15；VFX lit 变体不声明 point/area shadow sampler，12/13 留给 attribute/indirection。VFX context 关闭 GPU light-list buffer 与 cluster，`pass.lightListParams.w=0`，使用全局 light fallback；V1.4.3 已切透明 lit cluster。
- `vfx/src/runtime/runtime.cpp`：lit output 上传 float property uniforms（供 metallic/roughness 覆盖），并调用 `ShadingProvider::bindLighting`；unlit 路径保持原直接 `u_global_*` 绑定，避免 bgfx uniform 重复设置。
- 资产/测试：`vfx/tests/assets/lit_visual.vfx.json/.vfxb`（lit quad、metallic/roughness property）、`vfx_lit.gltf/.hdrp.json`（directional sun + shadow caster triangle）、`vfx/tests/golden/vfx_lit_{d3d11,vulkan}.png`、`vfx_lit_deferred_{d3d11,vulkan}.png`；`vfx/CMakeLists.txt` 注册 4 个 golden，`hbg_vfx_asset_check` 覆盖 22 个资产（V1.4.2 增至 24）。lit golden 使用 4 像素/通道 1 LSB tolerance，forward D3D/VK 有 3 像素、deferred 有 1 像素差异；普通 feature golden 仍严格逐字节。
- `vfxbake`/`compileGraphFile`：lit shader include 需要 `shaders` 根；`compileGraphFile` 自动从资产路径向上寻找 `shaders/material/surface_data_types.sc`，显式 `--shader-pack` 优先。
- `vfx/tests/test_vfx_compiler.cpp`：lit shader source、binding 12/13、无 point/area macro、双后端烘焙；`test_vfx_asset_json.cpp`：metallic/roughness 常量/property/范围；`test_vfx_hbg_backend.cpp`：lit effect 创建/输出、lightingBind 调用与 stage 常量。

**验证**
- `hbg_vfx_tests` **66/66, 37741 assertions**；`hbg_vfx_adapter_tests` **6/6, 298**；`hbg_vfx_compiler_tests` **21/21, 453**；`hbg_vfx_runtime_tests` **13/13, 277**。
- `vfxbake --check` **22** 个资产；VFX CTest **56/56**、全量 CTest **89/89**（外部 shader-build 负载下 asset-check 超时阈值调至 900s）。
- Lit scene: 4 directional shadow requests、8 shadow draws；forward/deferred D3D11/Vulkan 均通过；D3D vs Vulkan forward PSNR **101.76 dB** / 3 px max 1 LSB，deferred **106.53 dB** / 1 px max 1 LSB。
- 共享 `bindForwardLighting()` 后既有 bursts/forces/collision/curves/sort/events/subgraph/custom_hlsl 双后端 golden 全部通过（0 字节差异）。

**验收**
- [x] Lit VFX 在 forward/deferred 下被场景光照照亮；阴影方向正确（lit scene directional shadow requests/draws，shadow caster 阴影可见）。
- [x] 与 lit mesh 视觉趋势一致（lit VFX + lit triangle shadow caster 同场景 golden）。
- [x] 无 stage 冲突/validation error；VFX 12/13 buffer 与 forward point/area sampler 不重叠。
- [x] 共享 `bindForwardLighting()` 重构后，普通 forward/full VFX golden 与重构前一致。

**限制/偏差**
- V1.4.1 VFX lit 使用**全局 light list fallback**，未启用 tile/cluster light list；V1.4.3 已接入透明 lit cluster。
- VFX lit 不启用 point/area shadow sampler（12/13 被 attribute/indirection 占用）；directional/punctual 阴影可用，point/area 阴影留到后续 stage 重排。
- lit output 的 metallic/roughness 只支持常量或 float property；完整 Unity output slot/材质图留到 V2。
- lit golden 允许极小的 GPU 阴影/浮点 LSB 差异；lit 与 lit mesh 的严格逐像素对齐不成立。

---

### V1.4.2 Soft particles

- [x] FS 采样 scene depth（stage 4）；线性化；fade。
- [x] Output 属性 `softParticles`/`softDistance`。
- [x] 输出 pass 显式 `readTexture(depth)`。
- [x] 单测/截图：关闭时与 V1.3 一致；开启时边缘淡出。
- [x] 参考 `VFXCommonOutput.hlsl:108-159`、`VFXAbstractParticleOutput.cs:108,280-343,460-461,573-574`、HDRP `Runtime/VFXGraph/Shaders/VFXCommon.hlsl:180-183`、`ParticlePlanarPrimitives/PassForwardCommon.template:20-21,68`、`VFXParticleCommon.template:15-17`。

**实现**
- `vfx/include/vfx/graph.h`：`OutputDesc` 增加 `softDistance`（默认 0.1，最小 0.001）；`asset_json.cpp` 解析/校验 `softParticles`、`softDistance` 并纳入 graph hash。
- `vfx/src/compiler/codegen.cpp`：soft output 生成 `s_vfxSceneDepth`（stage 4）和 `u_vfxSoftParams`；FS 用 `gl_FragCoord` 取 raw depth，经 `u_global_invViewProj`/`u_global_view` 重建 scene/particle eye depth，`fade = smoothstep(saturate((sceneZ-selfZ)/softDistance))`，对齐 Unity `VFXGetSoftParticleFade`。`softParticles=false` 时保持原 unlit/lit shader 字节路径不变。
- `vfx/src/compiler/codegen.h`、`vfx/src/compiler/binary.cpp`、`vfx/src/runtime/runtime_asset.*`：`.vfxb` version **6**，OUTPUTS 32B/条，新增 soft flag/softDistance；scene depth binding 使用 `index=0xfffffffe`（`kSceneDepthBindingIndex`）。
- `vfx/src/runtime/runtime.cpp`：output VS 仍绑定 attribute/indirection stage 12/13；FS soft 绑定 `frame.sceneDepth` 到 stage 4、上传 `u_vfxSoftParams`（x=distance，yz=1/viewport），unlit soft 额外上传 `u_global_view`/`u_global_invViewProj`；pass desc 标记 `readsSceneDepth`。无 depth 时绑定 1×1 白纹理降级。
- `src/render/passes/forward_renderer.cpp`：VFX output pass 继续显式 `readTexture(depth)`；`makeVfxFrameContext` 导入 `resources.depth`。输出 pass 使用 `setDepthAttachment(depth, 0, true)`。
- `src/render/graph/render_graph.*`、`src/gfx/framebuffer_cache.*`：新增 read-only depth attachment flag（`BGFX_ATTACHMENT_READ_ONLY_DEPTH|STENCIL`），避免 D3D 采样同时绑定的 depth 返回 0；FrameBufferKey 纳入 attachment flags。
- `vfx/tests/assets/soft_visual.vfx.json/.vfxb`、`soft_visual_off.vfx.json/.vfxb`、`vfx_soft*.gltf/.hdrp.json`：前向场景，背景 triangle 写深度，softDistance=2.0；off fixture 用于零输入回归。

**验证**
- `hbg_vfx_tests` **67/67, 37754 assertions**；`hbg_vfx_compiler_tests` **22/22, 471**；`hbg_vfx_runtime_tests` **14/14, 293**；`hbg_vfx_adapter_tests` **6/6, 298**；`hbg_tests` **252/252, 32578**。
- `vfxbake --check` **24** 个资产；VFX CTest **60/60**、全量 CTest **93/93**。
- 新增 `vfx_soft_{d3d11,vulkan}.png` 与 `vfx_soft_off_{d3d11,vulkan}.png`；soft off 保持原 unlit shader 路径，soft on/off 的 D3D11 图像差异 max 56 / mean 2.46 / 42,341 px；同后端重复渲染 0 diff。D3D11 vs Vulkan soft 仅 721 px / max 1 LSB（PSNR 77.95 dB），按后端分别保存 golden。
- runtime 单测确认 stage 4、`s_vfxSceneDepth`、`u_vfxSoftParams` 和 `readsSceneDepth`；compiler 单测覆盖 lit/unlit soft shader、stage 4、双后端 shaderc 烘焙；FrameBufferKey 覆盖 read-only depth flag hash/equality。

**验收**
- [x] 关闭时与原 unlit/lit shader 路径一致；`soft_off` golden 通过。
- [x] 开启时粒子随场景深度连续淡出；输出 pass 显式读 depth 且 depth attachment 为 read-only，无深度反转/validation error。
- [x] 无 depth 绑定时不崩溃，降级为不淡出。

**限制/偏差**
- 当前 quad 为 camera-facing billboard，单个 quad 深度近似常量；soft fade 主要呈逐粒子距离淡出，完整屏幕空间边缘依赖后续非 billboard mesh/sphere 输出。
- soft 使用全局 `softDistance` 常量；Unity 的 per-output slot/property 曲线留到 V2。additive/premultiply 在当前 blend 公式下同时乘 RGB/alpha，alpha 模式只乘 alpha。

---

### V1.4.3 Forward/Deferred 帧顺序与排序

- [x] Forward：仿真在 sky 后/opaque 前；输出在 opaque 与 transparent 之间。
- [x] Deferred：仿真在 sky/decals 后、GBuffer 前；输出在 deferred lighting/SSR 与 forward-only opaque 后、transparent 前。
- [x] 透明 lit 使用 cluster light list。
- [x] VFX 输出与透明物体排序的记录（D8）；评估并入 DrawList 的成本。
- [x] 参考 `Packages/com.unity.visualeffectgraph/Editor/Models/Contexts/VFXSortingUtility.cs:9-56`、`Editor/NewCompiler/Builder/OutputUpdateTaskBuilder.cs:79-127`、`Editor/Models/Contexts/VFXAbstractParticleOutput.cs:110-120`、HDRP `HDRenderPipeline.cs:3465-3498`、`HDRenderPipeline.RenderGraph.cs:1899-1911`。

**实现**
- `src/render/passes/forward_renderer.cpp:5370`：Forward 顺序保持 `sky → VFX Simulation → contact/opaque → VFX Output → transparent`；Deferred 在 `:5462` 仿真、`:5481` 输出。按 HDRP 实际顺序，forward-only **opaque** 先于 VFX 输出（否则无深度写入的粒子会被其后的 opaque 覆盖），VFX 输出仅保证在 transparent 前。
- `Runtime::hasLitOutputs()`（`vfx/include/vfx/runtime.h:90`、`vfx/src/runtime/runtime.cpp:1547`）让 renderer 只在存在 lit output 时准备 cluster。
- `ForwardRenderer::prepareVfxClusterLighting()`（`forward_renderer.cpp:5179`）：texture light-list 路径直接复用现有 cluster texture；GPU buffer 路径每帧构建 CPU cluster-texture mirror 并上传 `m_clusterTexture`，输出 pass 使用 `clusterLightList` + `u_cluster_*`，不再强制 `lightListParams.w=0`。
- `addVfxOutputPass` 显式 `readTexture(clusterLightList)`；`bindForwardLighting` 绑定 stage 7 cluster texture / `u_cluster_*`，D3D/Vulkan 不依赖 buffer 变体。
- 透明排序仍由 `kTransparentSortingCriteria` 管理（`src/render/culling/sort_key.h:48`、`sort_key.cpp:19`）；VFX 内部粒子继续使用 V1.2.2 KVP sort。D8 保留：VFX 是 effect pass，不与透明 mesh 逐对象交错。
- 新增 `vfx_order.gltf` / `vfx_order.hdrp.json`：opaque、unlit forward-only、alpha transparent 三个 panel + lit VFX + directional/point light；`vfx_order_off.gltf` 为无 VFX baseline。
- `vfx/CMakeLists.txt`：`order` forward/deferred D3D11/Vulkan golden、`order_off` baseline、`run_vfx_order_off.cmake` 验证 `--no-vfx` 与 baseline 逐字节一致。

**验证**
- `hbg_vfx_tests` **67/67, 37754 assertions**；`hbg_vfx_compiler_tests` **22/22, 471**；`hbg_vfx_runtime_tests` **15/15, 299**；`hbg_vfx_adapter_tests` **6/6, 298**；`hbg_tests` **252/252, 32578**。
- `vfxbake --check` **24** 个资产；VFX CTest **68/68**；全量 CTest **101/101**（664.24s）。
- `vfx_order` D3D11/Vulkan forward/deferred golden 通过；deferred pass order 为 `Forward Only Opaque → VFX Output → Forward Transparent`。D3D/VK order 图像 0 diff；VFX on/off 差异 29,014 px / max 181；forward vs deferred max 2 LSB。`vfx_order_off` 的 `--no-vfx` 输出与 baseline 0 diff（D3D11/Vulkan）。
- `vfx_lit` 旧 golden 在 cluster 接入后保持 0 diff（仅有 directional light 的场景 cluster/global 结果一致）。
- 已知限制：VFX 输出在所有透明物体之前，透明物体无法与 VFX 粒子逐对象交错；当前 `vfx_order` 透明 panel 在 VFX 前方，视觉正确，但透明 panel 位于 VFX 后方时仍会覆盖 VFX（D8）。合并 DrawList 需要 indirect batch 或逐粒子 CPU 深度键，成本高于 V1 收益。

---

### V1.4.4 诊断 / 性能 / 资源生命周期

- [x] `FrameDiagnostics`：`vfx_effects/systems/particles/draws/dispatches/sort_passes/events/vfx_ms`。
- [x] `render performance` 输出 VFX 指标。
- [x] 资源：effect 创建/销毁、resize、shutdown 泄漏检查。
- [x] 10k/100k/1M quad 基准；记录 sim/draw/sort/CPU。
- [x] 失败降级：shader 缺失/容量超限/绑定不足时明确日志，不崩溃。
- [x] 参考 `Editor/GraphView/Profiling/VFXProfilingBoard.cs:470-485`（Full Update、Evaluate Expressions、逐 system CPU 采样）、`:566-582`（effect/system recorder）、`:605-621`（平均 CPU ms、GPU memory/time）；`Editor/GraphView/Profiling/VFXSystemProfilerUI.cs:200-212,294-301`（alive/capacity）；`Editor/Debug/VFXUIDebug.cs:882-896`（alive/maxAlive/efficiency）；`Editor/Models/Contexts/Implementations/VFXBasicInitialize.cs:92-100`（capacity 超限报错）、`Editor/Data/VFXDataParticle.cs:246-247`（capacity 丢弃语义）。

**实现**
- `vfx/include/vfx/runtime.h`：`Diagnostics` 新增 `simulationMs`、`outputMs`、`totalMs`；`updateMs`/`encodeMs` 保留。`Runtime::update` 重置相位计时，`encodeSimulation`/`encodeOutput` 分别记录，`encodeMs=sim+output`、`totalMs=update+encode`（`vfx/src/runtime/runtime.cpp:1546-1612`）。
- `src/render/passes/forward_renderer.h` / `.cpp`：`FrameDiagnostics` 新增 `vfxSortPasses`、`vfxEvents`、`vfxUpdateMs`、`vfxSimulationMs`、`vfxOutputMs`、`vfxMs`；从 `vfx::Diagnostics` 映射（`forward_renderer.cpp:3093-3103`）。
- `src/app/render_test.cpp`：`render performance` 输出 `vfx_sort_passes`、`vfx_events`、`vfx_ms`、`vfx_update_ms`、`vfx_sim_ms`、`vfx_output_ms`；保留 effects/systems/particles/draws/dispatches。
- 资源生命周期：`createEffect` 失败时 `pruneAssets()`，避免失败 shader/绑定创建留下未引用资产；`EffectImpl::releaseResources` 继续在 destroy/reload/shutdown 释放 program/buffer/texture。新增 `--resize-test`：render-test 逐帧在 full/half 分辨率间切换，覆盖 `ForwardRenderer::resize` 后 shutdown `ResourceManager` 0 live resource 检查（`src/app/backend.cpp`、`render_test.cpp`）。
- 失败降级：`EffectImpl::validateDeviceLimits` 在分配前检查 system capacity（`1..kMaxParticleCapacity`）、当前后端 shader blob、compute/vertex/fragment binding stage 对 `Device::Limits` 的上限；错误包含 program/stage/limit（`runtime.cpp:497-549`）。非法 buffer/stride 仍由 `HbgVfxBackend::createBuffer` 明确报错并返回 invalid handle。
- 新增性能 fixture：`perf_10k`/`perf_100k`（distance sort）与 `perf_1m`（sort 关闭，V1 sort 上限 262144）、`vfx_perf_*.gltf/.hdrp.json`；`vfx/CMakeLists.txt` 增加双后端 3 帧 perf smoke 和 resize CTest。

**验证**
- `hbg_vfx_tests` **67/67, 37754 assertions**；`hbg_vfx_compiler_tests` **22/22, 471**；`hbg_vfx_runtime_tests` **19/19, 489**；`hbg_vfx_adapter_tests` **7/7, 328**；`hbg_tests` **255/255, 32598**。
- `vfxbake --check` **27** 个资产；VFX CTest **76/76**；全量 CTest **109/109**（1078.67s）。
- 多次 create/destroy（16 次）与 4 次 HbgVfxBackend/Runtime session 后 live buffer/program/texture 为 0；`--resize-test` D3D11/Vulkan 通过，shutdown 无泄漏。
- 失败测试覆盖：绑定 stage 超限返回 `"compute/fragment binding stage ... exceeds the device ... limit"`；capacity 超限返回 `"exceeds the runtime limit"`；清空 shader blob 返回 `"missing the selected backend shader"`；均不创建 effect、不保留资源。
- 性能基准（Debug，640×360，60 帧，`--no-vsync`，playRate 0.1；`vfx_*_ms` 为最后一帧 CPU 记录，`gpu_frame_ms` 含 sim/draw/sort）：

| capacity | backend | avg_frame_ms | fps | gpu_frame_ms | draws | dispatches | sort_passes | update_ms | sim_ms | output_ms | vfx_ms |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 10240 | D3D11 | 6.171 | 162.0 | 0.901 | 1 | 10 | 5 | 0.005 | 0.153 | 0.058 | 0.216 |
| 102400 | D3D11 | 14.317 | 69.8 | 3.851 | 1 | 35 | 30 | 0.006 | 0.187 | 0.051 | 0.244 |
| 1048576 | D3D11 | 9.329 | 107.2 | 2.934 | 1 | 5 | 0 | 0.014 | 0.045 | 0.034 | 0.093 |
| 10240 | Vulkan | 18.623 | 53.7 | 0.597 | 1 | 10 | 5 | 0.008 | 0.090 | 0.036 | 0.133 |
| 102400 | Vulkan | 18.891 | 52.9 | 2.903 | 1 | 35 | 30 | 0.009 | 0.249 | 0.035 | 0.293 |
| 1048576 | Vulkan | 20.341 | 49.2 | 3.799 | 1 | 5 | 0 | 0.008 | 0.061 | 0.036 | 0.106 |

- 10k/100k 使用 distance sort，1M 使用 sort off（V1 compiler 对 sort capacity >262144 明确拒绝）；sort 的 GPU 成本计入 `gpu_frame_ms`，未单独做 per-pass GPU timer。1M sim 低于 100k 是因为少了 sort dispatch。

**验收**
- [x] 多次 init/shutdown 无泄漏。
- [x] 性能数据记录到 checklist。
- [x] 容量/绑定超限有明确错误。

**限制/偏差**
- `vfx_events` 仍是**事件链数量**（`Diagnostics::eventCount`）；精确 GPU `frameCount/totalCount/dropped` 仍由 `--vfx-event-reference`/`--vfx-cpu-event-reference` 读回验证。默认帧循环不读回 event buffer，避免同步 `bgfx::read` 停顿；异步计数环留给 V2。
- `vfx_ms` 是 CPU update + command encode 时间，不是 GPU 执行时间；GPU 总时看 `gpu_frame_ms`。Debug 构建、单场景、不设性能门槛。

---

### V1.4.5 V1 发布验收

- [x] `ctest` 全绿；`py -3 tools/idl_gen.py --check` 通过；`vfxbake --check` 通过。
- [x] D3D11/Vulkan：unlit/lit、alpha/additive、soft particles、bursts、collision、events、sort、subgraph、custom HLSL 的 golden。
- [x] VFX off / empty effect 与基线 **0 diff**。
- [x] CPU reference（1024 粒子 10 帧）通过。
- [x] 资源泄漏/validation error 清零。
- [x] 文档回填：实现位置、偏差、已知限制、性能数据。
- [x] `README.md` 状态改为“V1 完成”。
- [x] 参考 `Editor/NewCompiler/Passes/OutputPass.cs:229-235`（无 task 的 spawner system 跳过，不产生系统描述）；`Tests/Editor/VisualEffectGraphEditorTests.cs:17-31`（10 次 AddComponent/Undo 后对象与 VFXRenderer 均清除）；HDRP `HDRenderPipeline.RenderGraph.cs:1937,1959-1961,2062-2069,2092-2097`（仅当 `VFXManager.IsCameraBufferNeeded` 请求时发送 normal/depth/color buffer，空/VFX off 不增加 buffer 或 pass）。

**实现**
- 新增 `vfx/tests/assets/empty_visual.vfx.json` / `.vfxb`：`capacity=16`、`rate=0`、无 burst，输出为 additive quad，运行 30 帧仍为零 alive；这是“已加载但零发射”的 empty effect。
- 新增 `vfx/tests/assets/vfx_empty.gltf` 与 `vfx_empty.hdrp.json`：复用 order 场景的 opaque/transparent/point light，只挂 EmptyVisual。
- 新增 `vfx/tests/run_vfx_empty.cmake`：同一场景先以 empty effect 渲染，再以 `--no-vfx` 渲染，逐字节比较输出。
- `vfx/CMakeLists.txt` 新增 `hbg_vfx_empty_d3d11` / `hbg_vfx_empty_vulkan`，共 2 个 release gate CTest。

**验证**
- 全量 CTest **111/111**（563.66s，`build\vs2022\v143_final_ctest_145c.log`）；VFX CTest **78/78**；`hbg_idl_check` 通过；`hbg_vfx_asset_check` **28** 个资产通过。
- 单测：`hbg_vfx_tests` **67/67, 37754**；`hbg_vfx_compiler_tests` **22/22, 471**；`hbg_vfx_runtime_tests` **19/19, 489**；`hbg_vfx_adapter_tests` **7/7, 328**；`hbg_tests` **255/255, 32598**。
- Feature golden 双后端全部通过：

| 功能 | CTest |
|---|---|
| unlit additive | `hbg_vfx_golden_d3d11/vulkan` |
| lit alpha forward | `hbg_vfx_golden_lit_d3d11/vulkan` |
| lit alpha deferred | `hbg_vfx_golden_lit_deferred_d3d11/vulkan` |
| soft particles on/off | `hbg_vfx_golden_soft*`、`hbg_vfx_golden_soft_off*` |
| bursts | `hbg_vfx_golden_bursts_*` |
| collision | `hbg_vfx_golden_collision_*` |
| events | `hbg_vfx_golden_events_*` |
| sort | `hbg_vfx_golden_sort_*`、`hbg_vfx_sort_on_off_d3d11` |
| subgraph | `hbg_vfx_golden_subgraph_*` |
| custom HLSL | `hbg_vfx_golden_custom_hlsl_*` |
| order/VFX off | `hbg_vfx_golden_order_*`、`hbg_vfx_order_off_*` |

- VFX off / empty effect：`hbg_vfx_order_off_*` 与 `hbg_vfx_empty_*` 均为同后端 **0 diff**；empty 用例同时检查 no-VFX baseline 的像素完全一致。
- CPU reference：`vfx CPU reference simulates 1024 particles for 10 frames` 通过（1024/1024 spawnIndex、age=1.0、alive、属性范围检查）。
- 资源/validation：runtime 16 次 create/destroy、4 次 backend/runtime session、`--resize-test` 双后端均为 0 live resource；full CTest 无泄漏或 validation failure。D3D11 `--bgfx-debug` 对 order/empty 场景各 3 帧 exit 0，日志无 `ERROR`/`Validation`。
- 验证边界：Vulkan `--bgfx-debug` 在当前无 Vulkan validation layer 的环境会在 bgfx init 后崩溃，`--no-vfx` 同样复现，属于调试层环境限制；Vulkan release gate 以默认路径 CTest/golden 为准。
- 结论：V1 发布门禁通过；V1 完成。已知限制保持 §7，无新增未登记偏差。

---

## 6. V1 发布门禁

| 门禁 | 条件 |
|---|---|
| 功能 | V1.0b–V1.4 全部勾选；无未登记的偏差 |
| 正确性 | CPU reference + 双后端 golden；VFX off / empty effect 0-diff |
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
