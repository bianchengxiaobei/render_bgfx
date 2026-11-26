# Shader Graph 代码生成设计

> 关联：[architecture.md](architecture.md)、[asset-format.md](asset-format.md)、[targets.md](targets.md)、[nodes.md](nodes.md)。
> 目标：把 `sg::Graph` 稳定地生成为 bgfx `.sc` 兼容的 HLSL，并产出确定性 `.sgb`。
> 对齐原则：语义对齐 Unity `Generator`/`GenerationUtils`，但输出物不是 ShaderLab，而是每个 pass 的 `.sc` 源 + shaderc 产物。

---

## 1. 输入与输出

```cpp
namespace sg {

struct CompileOptions {
    std::filesystem::path shaderc;          // 必填
    std::filesystem::path bgfxSource;       // shaderc include 需要
    std::filesystem::path cacheDirectory;   // 生成 include / 中间文件
    std::vector<Backend> backends{Backend::Dxbc, Backend::Spirv};
    VariantPolicy variants = VariantPolicy::Used;
    bool keepHlsl = false;                  // 写入 .sgb 的调试源
    int maxVariants = 64;
};

struct CompileResult {
    std::vector<std::byte> binary;          // .sgb
    std::vector<Diagnostic> diagnostics;    // nodeId/slotId/severity/message
    std::string error;                      // 致命错误（空=成功）
    Stats stats;                            // 节点数、pass 数、变体数、耗时
};

CompileResult compile(const Graph& graph, const CompileOptions& options);

} // namespace sg
```

生成产物：

- 每个 pass 一份 HLSL（`vertex`/`fragment` 分文件），写入 `cacheDirectory/<graphHash>/<pass>.<variant>.<backend>.<stage>.hlsl`。
- shaderc 输出 dxbc/spirv blob，最终打包进 `.sgb`（见 `asset-format.md` §10）。
- 诊断的行/列尽量映射回图节点；无法映射时仅带 nodeId。

## 2. 类型系统

对齐 Unity：

- `SlotValueType`（`Editor/Data/Nodes/SlotValue.cs:8`）：`SamplerState、DynamicMatrix、Matrix4、Matrix3、Matrix2、Texture2D、Texture2DArray、Texture3D、Cubemap、Gradient、DynamicVector、Vector4、Vector3、Vector2、Vector1、Dynamic、Boolean、VirtualTexture、PropertyConnectionState、External`。
- `ConcreteSlotValueType`（`:32`）：去掉 Dynamic*，给出具体类型。
- `Precision`（`Editor/Data/Enumerations/Precision.cs:23`）：`Inherit/Single/Half/Graph`。
- `ConcretePrecision`（`:47`）：`Single/Half`；具体化后每个节点必须落到两者之一。

C++ 表示：

```cpp
enum class ValueType {
    Float, Vector2, Vector3, Vector4,
    Matrix2, Matrix3, Matrix4,
    Boolean, Integer,
    Texture2D, Texture2DArray, Texture3D, Cubemap,
    Gradient, SamplerState, VirtualTexture,
    DynamicVector, DynamicMatrix, Dynamic, External
};

enum class Precision { Inherit, Single, Half, Graph };
enum class ConcretePrecision { Single, Half };
```

类型规则：

| 规则 | 说明 |
|---|---|
| 动态槽 | 输入/输出声明为 `DynamicVector/DynamicMatrix`；由连接或 `concreteType` 决定具体类型 |
| 固定点迭代 | 反复沿边传播具体类型，直到不再变化；无解/冲突报错 |
| 未连接动态槽 | 使用 JSON 持久化的 `concreteType`；缺省按节点 spec 的默认类型 |
| 数值提升 | `float` 可提升为任意 vector；vector 可补 0/1 或截断；vector→matrix 按 Unity `NodeUtils` 语义（实现前逐节点核对） |
| 布尔/整数 | 仅在与 float 运算时转换；节点 spec 声明是否允许 |
| 精度继承 | 按 Unity 规则：`single(0) < graph(1) < half(2)` 取 min；`Inherit` 由输入推导，`Graph` 回退图默认 |
| Literal 模式 | 未连线输入可显式写值；生成字面量而不是 uniform；动态类型按字面量推断 |
| 矩阵 | HLSL `float2x2/3x3/4x4`；乘法统一 `mul(A,B)`；转置/行列语义逐节点测试 |
| 纹理 | `SAMPLER2D/2DARRAY/3D/CUBE`；采样函数按节点生成 `texture2D/texture2DLod/texture2DGrad/texture2DBias` |

## 3. 具体化（Concretization）

对应 Unity `Editor/Data/Graphs/GraphConcretization.cs:8` 与 `AbstractMaterialNode.UpdateNodeAfterDeserialization`（`Editor/Data/Nodes/AbstractMaterialNode.cs:1098`）。

步骤：

1. **收集活跃节点**：从 Target 的 active blocks 出发，深度优先收集上游节点（对应 `Generator.cs:256` 的 `NodeUtils.DepthFirstCollectNodesFromNode`）。
2. **建立槽位表**：按 `NodeDef` 生成 slot id/name/dir/ValueType；应用 JSON 中的 `concreteType`。
3. **固定点类型传播**：
   - 遍历每条边，尝试把源输出具体类型写入目标动态输入。
   - 若目标输入类型变化，重新触发下游节点（DynamicVector 节点的输出类型可能随之改变）。
   - 迭代上限（如 1024 次）后报"类型无法收敛"。
4. **精度传播**：先按节点设置计算 `GraphPrecision`，再回退图默认，最后落到 `ConcretePrecision`。
5. **字面量判定**：未连线且 `mode=literal` 的槽位生成字面量；否则生成 uniform/默认值。
6. **校验**：未连接且无默认值的必需输入、非法转换、矩阵维度不匹配、纹理类型不匹配。

输出：`ConcretizedGraph`（节点 → 具体 slot 类型/精度/值模式），供后续生成使用。

## 4. 需求与活跃字段

- 需求集合（对齐 `Editor/Data/Interfaces/IMayRequire*.cs`）：`UV0/UV1`、`Normal`、`Tangent`、`Bitangent`、`Position`、`PositionPredisplacement`、`ViewDirection`、`ScreenPosition/NDC/Pixel/Clip`、`Time`、`FaceSign`、`VertexColor`、`VertexID`、`InstanceID`、`VertexSkinning`、`DepthTexture`、`CameraOpaqueTexture`、`Transform`。
- 生成时只声明实际使用的 GraphInputs 字段；Target 模板据此决定 varying 集合（使用固定超集 + 未用字段裁剪）。
- 活跃字段（ActiveFields）用于 pass 裁剪：例如只有接了 Alpha/AlphaClip 才启用 `ALPHA_CLIP` 分支；对应 Unity `Editor/Generation/Processors/ActiveFields.cs:64`。
- Scene Color/Scene Depth 需求写入 `.sgb` 的 `requiredFeatureMask`（bit0/bit1），Fog 写入 bit2；系统采样器按属性纹理之后占用 graph stage 0-4 的空闲槽，超出 `stageBudget` 直接编译失败。

## 5. 属性与关键字

### 5.1 属性收集

对齐 `PropertyCollector`（`Editor/Generation/Processors/PropertyCollector.cs:10`）：

- 图属性（Blackboard）。
- 活跃节点隐式属性（如 SampleTexture2D 自动生成的纹理/采样器属性）；Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap、Texture Size/Gather/Calculate LOD/Normal From Texture 的 Texture/Cube 输入可直接连接 Property/Asset 属性，采样时解析到对应 graph property 的 `s_sg_<refName>` sampler；Texture2D 属性的 `useTilingAndOffset`/`tiling`/`offset` 声明 `u_sg_<refName>_ST` 并在 Sample Texture 2D/LOD、Normal From Texture、Parallax Mapping/POM 中生成 `uv * _ST.xy + _ST.zw`，Split Texture Transform 输出 `_ST.xy/_ST.zw`，Texture Only 恢复恒等；默认值由 `.sgb` 携带，sidecar 的 `<refName>_ST` property override 在运行期覆盖；Parallax/POM/Triplanar 的 Texture 端口可连接 Property/Asset，Triplanar 使用 node Tile 且不应用属性变换；Texture2DArray/Texture3D/Cubemap 节点按 `texture` setting 校验并声明 `SAMPLER2DARRAY`/`SAMPLER3D`/`SAMPLERCUBE`，运行期由 sidecar 图像数组组装 layered texture；Sampler State 节点或 SamplerState 属性连接采样节点的 Sampler/HeightmapSampler 输入时，filter/wrap/anisotropic 覆盖对应纹理属性的 sampler 元数据；Texture2D 属性的 `defaultType`（white/black/grey/bump/linearGrey/red）写入 `.sgb` 属性 reserved byte，运行期无纹理覆盖时 `GraphBinding` 绑定对应 1x1 fallback；Procedural Virtual Texture 连接 Sample Virtual Texture 时按 `vtName`/`layers` 展开隐式 Texture2D 层属性；VirtualTexture Property 按自身 refName/`vtLayers` 展开 layer，并支持 `vtLayerRefNames`/`vtLayerTextureTypes` 自定义引用与法线类型；Sample Texture 2D 按 `mipSamplingMode` 生成 Standard/LOD/Bias/Gradient（Bias 在 fragment 用 `texture2DBias`，vertex 回退 LOD），2D Array 支持 LOD 与导数 Bias/Gradient 近似，3D 支持 Standard/LOD；`enableGlobalMipBias` 打开时 Standard/Bias 使用 `u_sg_globalMipBias.x`，Gradient 将 DDX/DDY 乘以 `u_sg_globalMipBias.y`，LOD 模式不加全局 bias；2D 默认 true、2D Array 默认 false，运行期 ForwardRenderer 将 `log2(renderScale)` 传给 `GraphMaterialSystem`，再由 `GraphBinding` 上传该 uniform；纹理属性 `isHDR` 声明 `u_sg_<refName>_HDR`，Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 在采样后按 `if (_HDR.x > 0)` 调用 `hbgSGDecodeHDRSample`（对齐 Unity `AppendHDRDecodeOperation`/`DecodeHDRSample`），默认 `(1,1,0,0)` 恒等值，sidecar `<refName>_HDR` 覆盖；Sample Virtual Texture 按 Unity TextureStack fallback 生成 `texture2D`/`texture2DLod`/`texture2DBias`/`texture2DGrad`，`enableGlobalMipBias` 对 Automatic/Bias/Derivatives 生效（Automatic/Bias 加 `u_sg_globalMipBias.x`，Derivatives 乘 `u_sg_globalMipBias.y`）；VT `sampleQuality=low` 时 Bias/Derivatives 在 fragment 由 ddx/ddy 或显式导数计算 LOD 后走 `texture2DLod`，`high` 使用 `texture2DBias`/`texture2DGrad`；`noFeedback+Lod Level` 允许 VT 输出进入顶点位移，其他组合在连接到 Vertex Position 时报 `SG_TARGET_UNSUPPORTED`；真实 feedback buffer 未接。Gradient 节点 v1 直接生成常量结构，Blackboard Gradient LUT 待补。
- Target 声明属性（材质开关、渲染状态覆盖）。
- 排序：按 `refName` 升序，去重后锁定（后续不得再增）。

### 5.2 属性声明与布局

- 每个属性生成 uniform：`u_sg_<refName>`。
- 类型映射：

| Graph 类型 | HLSL 声明 | bgfx uniform |
|---|---|---|
| float | `float` | Vec4（x） |
| vector2/3/4 | `float2/3/4` | Vec4（未用分量 0） |
| color | `float4` | Vec4 |
| boolean | `bool`（生成时按 0/1） | Vec4（x） |
| integer | `int` | Vec4（x） |
| matrix2/3/4 | `float2x2/3x3/4x4` | Mat4（零填充） |
| texture* | `SAMPLER2D/2DARRAY/3D/CUBE` | Sampler |
| gradient | `SAMPLER2D`（LUT） | Sampler |
| samplerstate | 不声明，仅决定采样器状态 | Sampler flags |

- 矩阵上传：JSON 行主序 → 按 bgfx/HLSL 约定转换（默认列主序，SG-0 实测确认；不得凭记忆）。
- 逆模型：需要 World->Object、Object Normal 或 InverseModel 矩阵时声明 `u_sg_modelInverse`，由图材质绘制路径按对象模型矩阵求逆并上传；未使用的图不声明该 uniform，避免改变已有资产哈希。
- 预算：默认 5 纹理（0-4）、32 标量/向量、4 矩阵；`TargetDesc.uniformBudget` 可覆盖；超限报错并给出属性名。

### 5.3 关键字与变体

- 关键字定义对齐 `Generation/Enumerations/KeywordType.cs:6`、`KeywordDefinition.cs:6`。
- 变体键：`(engineVariantMask | graphKeywordMask)`；图关键字从 bit8 起分配，最多 8 个布尔/枚举位。
- 枚举关键字：每个 entry 一位；无 entry 表示默认分支。
- 变体数 = 引擎变体（按 Target 声明，如 16）× 2^N（受 cap 限制）；默认 cap 64。
- 编译策略：
  - `Used`：只编译目标图形实际使用的组合（预览/编辑器默认）。
  - `All`：编译全部组合（发布/CI）。
  - 超 cap：Error 诊断，不生成 `.sgb`。
- 生成条件代码：`#if defined(KEYWORD)` / `#elif`；关键字引用名必须是合法 HLSL 标识符。

## 6. HLSL 生成

### 6.1 变量与函数命名

| 项 | 规则 |
|---|---|
| 节点槽变量 | `n{nodeId}_s{slotId}` |
| 临时变量 | `t{n}`（节点内部） |
| 函数名 | Unity 风格语义名 + 类型签名哈希，如 `SG_Multiply_f4_f4` |
| Surface 输入 | `GraphInputs` 字段：`uv0/uv1/positionWS/normalWS/tangentWS/bitangentWS/viewDirWS/screenPos/time/faceSign/vertexColor` |
| Surface 输出 | `SurfaceDescription` 字段（对目标声明） |

- 函数注册表按签名去重（对齐 `Editor/Data/Util/FunctionRegistry.cs:17`）；输出顺序按首次注册顺序，保证确定性。
- 节点体代码按拓扑序生成；先函数、后 body（对齐 Unity `IGeneratesFunction`/`IGeneratesBodyCode`，`Editor/Data/Interfaces/`）。
- 生成的 HLSL 必须可被 shaderc 接受：不使用 `Texture2D` 对象、`SamplerState` 对象、`cbuffer` 自定义语法；使用 bgfx `SAMPLER2D` 与 `uniform`。

### 6.2 Surface 生成（材质 Target）

```hlsl
// generated surface include
void hbgGraphSurface(in GraphInputs gi, inout SurfaceData sd)
{
    // 节点体
    sd.baseColor = ...;
    sd.metallic = ...;
    sd.perceptualRoughness = 1.0 - smoothness;
    sd.roughness = max(sd.perceptualRoughness * sd.perceptualRoughness, 0.001);
    sd.normalWS = ...;
    sd.ambientOcclusion = ...;
    sd.emissive = ...;
    sd.alpha = ...;
}
```

- 目标模板负责：TBN 构建、`DOUBLE_SIDED` 法线翻转、`ALPHA_CLIP` discard、调用光照。
- 图模板只包含 `material/surface_data_types.sc`，不得包含 `material/surface_data.sc`（后者占用 0-4 材质 sampler）。
- 未连接的 block 使用 Target 默认值（与 Unity 默认 block 值一致，逐项记录）。
- 法线空间：
  - Tangent：`mul(normalTS, tbn)`；
  - Object：`mul(ObjectToWorld, normalOS)` 后归一化；
  - World：直接归一化。
- `AlphaClipThreshold`：写入 target 提供的变量，不在 surface include 内 discard。

### 6.3 Vertex 生成

```hlsl
vec3 hbgGraphVertexPosition(in GraphVertexInputs gi, vec3 positionOS)
{
    // 无 Vertex Position 连接时返回 positionOS
}
```

- 仅当 Target 声明 Vertex Blocks 且节点连接时生成；否则模板直接使用原位置。
- 顶点动画节点（Mesh Deformation/Skinning）在 v1 只支持 Position 偏移，不支持任意 varying 输出。

### 6.4 GraphInputs 与 varying

- 固定超集（按 pass 裁剪）：
  - `v_texcoord0`（UV0）、`v_texcoord1`（UV1）
  - `v_worldPos`、`v_normal`、`v_tangent`、`v_bitangent`、`v_color`
  - `v_vertexID`、`v_instanceID`（`flat`，VS 从 `gl_VertexID`/`gl_InstanceID` 写入）
- `screenPos` 在 FS 用 `gl_FragCoord` + `u_global_screenSize` 计算；`viewDir` 用 `u_global_cameraPos - worldPos`。
- `time` 来自 `u_global_time`（检查具体分量定义后再用，禁止猜）。
- `gl_VertexID`/`gl_InstanceID` 由 bgfx shaderc 映射到 `SV_VertexID`/`SV_InstanceID`（`tools/shaderc/shaderc.cpp:2134-2259`），只能在 VS 使用；图模板在 VS 写入 flat varying，片段读取。
- 需要新 varying 时，必须扩展 `shaders/shadergraph/varying_graph.def.sc` 并同步 VS/FS；不得临时在某个 pass 单独加。

## 7. Pass 模板与 stage 映射

### 7.1 模板清单

| 文件 | 用途 |
|---|---|
| `shaders/shadergraph/forward_graph.vs.sc` | 图材质 forward 顶点（含 vertex hook） |
| `shaders/shadergraph/forward_graph.fs.sc` | 图材质 forward 片元（surface + light loop） |
| `shaders/shadergraph/gbuffer_graph.fs.sc` | deferred GBuffer |
| `shaders/shadergraph/depth_graph.fs.sc` | depth prepass |
| `shaders/shadergraph/shadow_graph.fs.sc` | shadow caster |
| `shaders/shadergraph/motion_graph.vs.sc` | motion vectors |
| `shaders/shadergraph/baked_gi_graph.fs.sc` | 烘焙 GI |
| `shaders/shadergraph/debug_graph.fs.sc` | Albedo/Roughness debug |
| `shaders/shadergraph/unlit_graph.*` | Unlit forward/depth |
| `shaders/shadergraph/varying_graph.def.sc` | 固定超集 varying |

模板中通过 `#include "<graphCache>/surface.sc"` 引入生成代码；`-i <graphCache>` 由编译流程提供。

### 7.2 Stage 映射

v1 **不做引擎 stage 重映射**：

- 图模板省略 `material/surface_data.sc`，图贴图使用 `0-4`（默认 5 张）。
- 光照 include 保持默认 stage：light data 5、方向阴影 6、light list 7、shadow context 8（Buffer，占 t8）、punctual 9、environment 10、contact 11、point/IMS 12、point/area 13、probe 14、cookie 15（见 `architecture.md` §3.3）。
- 因此不需要修改现有光照 include 的字面量 stage；只需保证图模板不声明 0-4 的材质 sampler。
- 生成器输出 `stageMap`（语义 → stage），bake 时校验唯一性、预算与 runtime `requiredFeatureMask`。
- 若未来要支持 12/13/14/15 扩展或 Fullscreen 重排，再把现有 include 的 stage 字面量宏化；该改造必须单独回归，不得与 v1 混做。

### 7.3 渲染状态

- 由 Target/材质共同决定：`ZTest/ZWrite/Cull/Blend/RenderQueue`。
- 现有状态函数：`src/render/passes/forward_renderer.cpp:149/164/200`（forward/bakedGI/shadow state）。
- 图材质复用同样的状态组合；透明/AlphaClip/DoubleSided 必须与现有材质语义一致。
- 图材质 v1 默认不参与 instancing（`drawForwardBatch` 的 `batch.itemCount < 2` 限制）；后续是否支持写入 SG-7。

## 8. shaderc 编译

对齐 `vfx/src/compiler/compiler.cpp:201`：

```
shaderc -f <pass>.<variant>.<stage>.hlsl -o <blob>
        --type vertex|fragment
        -i <graphCache> -i shaders -i shaders/include -i <bgfxSrc>/src
        --varydef shaders/shadergraph/varying_graph.def.sc  (VS/FS)
        --define "KEY1=1;KEY2=1"
        --platform windows -p s_5_0           (dxbc)
        --platform linux   -p spirv           (spirv)
        -O 3 --Werror
```

- 编译缓存：`(graphContentHash, pass, variant, backend, generatorVersion, shadercVersion)`。
- 并行：按 pass/backend 并发，线程数可配；同一源文件加锁避免重复。
- 失败诊断：解析 shaderc stderr，尽量提取 `file(line): error`，映射到 pass/变体；图级错误无法定位时只带 pass 名。
- 生成源保留：`--keep-hlsl` 写入 `.sgb` HLSL 段，便于排查。

## 9. `.sgb` 打包

按 `asset-format.md` §10：

- 属性表来自 §5，pass 表来自 §7，变体表来自 §5.3，blob 来自 §8。
- 生成后重新读取并验证：
  - 每个变体所有 pass 的 vertex/fragment 齐全；
  - backend 覆盖请求的集合；
  - stage 表无冲突；
  - 属性/关键字引用均存在。
- 输出 `graphs/manifest.json` 条目（可选，由 `sgbake` 统一生成）。

## 10. 诊断模型

```cpp
enum class Severity { Info, Warning, Error };

struct Diagnostic {
    Severity severity;
    int nodeId = -1;      // -1 = 图级
    int slotId = -1;
    std::string code;     // 稳定错误码，如 "SG_TYPE_MISMATCH"
    std::string message;
    std::string pass;     // 可选
    std::string variant;  // 可选
};
```

- 错误码稳定，前端据此做本地化/跳转。
- 校验错误必须给出节点/槽位；shaderc 错误给出 pass/变体与原始行号。
- 同一错误去重；按 `(severity, nodeId, code, message)` 排序后输出。

## 11. 接口与阶段

```cpp
// 伪代码
CompileResult compile(const Graph& graph, const CompileOptions& opt) {
    auto g = resolveSubgraphs(graph);                 // §2/§8
    auto d = validate(g);                             // 类型/边/属性/关键字/Target
    auto c = concretize(g);                           // §3
    auto active = collectActiveNodes(g, c);           // §4
    auto props = collectProperties(g, active);        // §5.1
    auto keywords = collectKeywords(g, props);        // §5.3
    auto plan = planPasses(g.target, props, keywords, opt); // §7
    for (pass : plan.passes)
        for (variant : plan.variants)
            emitAndCompile(pass, variant);            // §6/§8
    return packSgb(...);                              // §9
}
```

阶段边界必须可单测：`validate/concretize/collect/emit/pack` 各自有 fixture，不依赖 GPU。

## 12. 对齐 Unity 参考

| 主题 | Unity 参考 |
|---|---|
| 活跃节点/构建 | `Editor/Generation/Processors/Generator.cs:256`（`BuildShader`）、`:415`（`GenerateSubShader`）、`:576`（`GenerateShaderPass`） |
| 属性声明 | `Editor/Generation/Processors/GenerationUtils.cs:907`（`GeneratePropertiesBlock`） |
| Surface 结构/函数 | `Editor/Generation/Processors/GenerationUtils.cs:1081/1131` |
| Vertex 结构/函数 | `Editor/Generation/Processors/GenerationUtils.cs:1287/1306` |
| 函数注册表 | `Editor/Data/Util/FunctionRegistry.cs:17` |
| 变量名/槽位取值 | `Editor/Data/Nodes/AbstractMaterialNode.cs:425/431/887` |
| 动态槽类型 | `Editor/Data/Graphs/DynamicValueMaterialSlot.cs:80` |
| 精度规则 | `Editor/Data/Enumerations/Precision.cs:23/47` |
| 关键字排列 | `Editor/Data/Util/KeywordUtil.cs:178/196/201`、`Editor/Generation/Processors/KeywordCollector.cs:8` |
| 变体上限 | `Editor/Data/Graphs/ShaderKeyword.cs:13`（`kVariantLimitWarning`） |
| 结构体字段 | `Editor/Generation/TargetResources/StructFields.cs:6/42/96/200` |
| Block 字段 | `Editor/Generation/TargetResources/BlockFields.cs:9/21` |
| Pass 描述 | `Editor/Generation/Descriptors/PassDescriptor.cs:4`、`SubShaderDescriptor.cs:6` |

## 13. 测试要求

| 测试 | 内容 |
|---|---|
| 类型单测 | 动态槽传播、精度、字面量、矩阵转换；移植 Unity `Tests/Editor/DynamicSlotTests.cs`、`LiteralFloatTests.cs` 的预期 |
| 校验单测 | 环、缺失输入、非法转换、属性重名、关键字超限 |
| 生成 fixture | 每个节点至少一个"最小图 → HLSL 片段"断言（字符串包含/结构） |
| 编译 fixture | 关键图生成 `.sgb` 并验证 section/变体/属性表 |
| 确定性 | 同输入两次编译字节一致；`sgbake --check` 通过 |
| 双后端 | 所有 fixture 至少 dxbc + spirv 编译通过 |
| 回归 | 修改 stage 映射/预算后，现有 shader golden 0 diff |
