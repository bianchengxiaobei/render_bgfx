# Shader Graph 节点库规划

> 关联：[codegen.md](codegen.md)、[nodes-inventory.md](nodes-inventory.md)、[targets.md](targets.md)。
> 目标：全量覆盖 `Editor/Data/Nodes` 下的节点（208 个 `[Title]` 节点 + 内部节点），分 A/B/C 三批落地。
> 对齐原则：节点语义、槽位、默认值、动态类型与精度对齐 Unity；HLSL 由我们重写，不搬运 Unity 源码。

---

## 1. 节点框架

```cpp
struct SlotDesc {
    int id;                       // 稳定 id，写入 JSON
    std::string name;             // 显示名，如 "A"
    SlotDirection dir;            // Input/Output
    ValueType type;               // 固定或 Dynamic*
    std::string displayName;      // Inspector 显示名（可空）
    std::optional<Value> defaultValue;
    bool allowConnection = true;
    bool supportsLiteral = false;
    std::vector<ValueType> allowedConcreteTypes; // 动态槽可接受的具体类型
};

struct NodeDef {
    std::string type;             // "Multiply"
    int typeVersion = 1;
    std::string title;            // "Multiply"
    std::string category;         // "Math/Basic"
    std::vector<std::string> synonyms;
    std::vector<SlotDesc> slots;
    std::vector<SettingDesc> settings;   // 精度、模式、枚举、按钮
    std::vector<Requirement> requirements;
    bool hasPreview = true;
    bool isTargetSpecific = false;
    std::vector<std::string> allowedTargets; // 空 = 全部

    // 行为回调（或数据驱动模板）
    SlotUpdateFn updateSlots;     // 动态槽类型/数量变化
    EmitBodyFn emitBody;          // 生成节点体
    EmitFunctionFn emitFunction;  // 生成辅助函数（可空）
    ValidateFn validate;          // 节点级校验
};

struct NodeInstance {
    int id;
    const NodeDef* def;
    std::vector<SlotState> slots; // §asset-format §3
    Json settings;
    Vec2 position;
};
```

- 节点注册由 `NodeRegistry` 静态注册；导出 JSON 元数据给 Web（`/api/catalog/nodes`）。
- 节点类型名稳定，不随 C++ 类名变化；删除类型保留 legacy 映射。
- 动态槽：`updateSlots` 在具体化阶段反复调用，直到槽位集合与类型稳定（对齐 Unity `DynamicValueMaterialSlot`，`Editor/Data/Graphs/DynamicValueMaterialSlot.cs:14/80`）。

## 2. 节点 spec 格式

优先用数据驱动 spec 覆盖简单节点，减少 C++ 代码。

```json
{
  "type": "Multiply",
  "typeVersion": 1,
  "title": "Multiply",
  "category": "Math/Basic",
  "synonyms": ["multiplication", "times", "x"],
  "slots": [
    { "id": 0, "name": "A", "dir": "in",  "type": "dynamic", "allowed": ["float","vector2","vector3","vector4","matrix2","matrix3","matrix4"] },
    { "id": 1, "name": "B", "dir": "in",  "type": "dynamic", "allowed": ["float","vector2","vector3","vector4","matrix2","matrix3","matrix4"] },
    { "id": 2, "name": "Out", "dir": "out", "type": "dynamic" }
  ],
  "emit": {
    "body": "Out = A * B;",
    "matrixBody": "Out = mul(A, B);"
  },
  "dynamic": {
    "rule": "max-dimension",
    "matrixIfAny": true
  },
  "preview": true
}
```

无法用模板表达的节点（约 30-40 个）用手写 C++ 节点：

- 动态数学：Add/Subtract/Multiply/Divide/Power/Modulo/Matrix*；
- 纹理采样族：SampleTexture2D/2DArray/3D/Cubemap/LOD/Gather + SamplerState；
- Channel：Split/Combine/Append/Swizzle/Flip；
- Utility：SubGraph/CustomFunction/Branch/Switch/Comparison/Keyword/Dropdown；
- Master：BlockNode/SubGraphOutputNode；
- UI/Terrain/MeshDeformation 等 target 专属节点。

## 3. 分类与批次

全量清单见 [nodes-inventory.md](nodes-inventory.md)（自动从 Unity 源生成，含批次、Unity 源文件、计划 C++ 文件、状态）。

| 批次 | 范围 | 目标 | 数量（约） |
|---|---|---|---|
| A（核心） | Input/Basic、Input/Geometry、Input 下的 Property、Channel、Math/Basic/Advanced/Range/Round/Interpolation、Utility/Logic、Utility 下的 Sub-graph/Custom Function/Keyword/Dropdown/Preview、基础 Texture | SG-2 完成核心 60+，SG-6A 收尾 | 83 |
| B（常用扩展） | Math/Trigonometry/Vector/Wave/Matrix/Derivative、Artistic、UV、Procedural、Input/Lighting/Scene/Gradient/PBR/Matrix、剩余 Texture（VirtualTexture/Gather/Array/3D/Cubemap 资产等） | SG-6 前半 | 105 |
| C（长尾/平台） | UI、Input/UI、Terrain、Mesh Deformation、Custom Interpolators | SG-6 后半；不可用目标节点 UI 置灰 | 20 |

批次规则：

- 批次只是实现顺序，不是能力裁剪；最终必须 100% 覆盖清单。
- 目标专属节点（如 UI/Terrain）在对应 Target 可用前不进入节点搜索，或显示"当前 Target 不支持"。
- 节点 spec 与 Unity 源文件的对应关系写入 inventory，便于逐项核对。

## 4. 特殊节点清单

| 类别 | 节点 | 关键点 |
|---|---|---|
| 动态向量/矩阵 | Add、Subtract、Multiply、Divide、Power、Modulo、Minimum、Maximum、Lerp、Clamp、Remap、Matrix Construction/Split/Transpose/Determinant | 动态类型传播、矩阵乘法语义、精度 |
| 通道 | Split、Combine、Append、Swizzle、Flip | 通道数、>=4 截断、动态输入 |
| 纹理 | SampleTexture2D/2DArray/3D/Cubemap、SampleTexture2DLOD、GatherTexture2D、CalculateLevelOfDetail、SamplerState、Texture Assets、TexelSize、SplitTextureTransform | sampler 绑定、mip 模式、stage 预算、默认纹理 |
| 属性/关键字 | PropertyNode、KeywordNode、DropdownNode | Blackboard 引用、枚举/布尔、材质 override |
| 子图 | SubGraphNode、SubGraphOutputNode | 展开、属性重命名、递归检测、精度切换 |
| 自定义函数 | CustomFunctionNode | HLSL 文本/文件、输入输出槽、include 限制、错误定位 |
| 控制流 | Branch、Comparison、Switch、BranchOnInputConnection、Is Front Face | 动态类型、bool 输出、关键字条件 |
| 法线 | Normal Blend/From Height/From Texture/Reconstruct Z/Strength/Unpack | 空间转换、归一化、切线空间 |
| UV | TilingAndOffset、Rotate、PolarCoordinates、Twirl、Spherize、RadialShear、Flipbook、Parallax Mapping、Parallax Occlusion Mapping、Triplanar | 视差/三平面需要切线/视线输入；POM 为片段光线步进 |
| 程序化 | Checkerboard、Noise（Simple/Gradient/Voronoi）、Shape（Ellipse/Polygon/Rectangle/Rounded*） | 函数注册表去重、参数默认值 |
| 光照/场景 | Ambient、Baked GI、Main Light Direction、Reflection Probe、Scene Color/Depth、Camera、Fog、Object、Screen、Eye Index | 引擎输入绑定；Scene Color 透明副本与 Fog 为 v1 近似 |
| 顶点 | Position、Normal、Tangent、Bitangent、Vertex Color、Vertex ID、Instance ID、View Direction、View Vector、Screen Position、UV | varying/属性扩展；顶点 hook |
| Mesh Deformation | Linear Blend Skinning、Compute Deform | 需要额外顶点流/骨骼数据；v1 target-gated preview 回退 |
| Gradient | Gradient、Sample Gradient、Blackbody | 代码生成期 key 采样、blend/fixed/perceptual |
| UI/Terrain | Element*、SDF Text、Default*、Terrain* | 依赖对应 Target；当前 target-gated preview v1，不支持时禁用 |
| 内部 | BlockNode、RedirectNode、LegacyUnknownTypeNode、PreviewNode | 编辑器专用/兼容 |

## 5. 移植流程（逐节点）

1. **读源码**：定位 Unity 节点类与生成逻辑，记录 `file:line`（例如 `Editor/Data/Nodes/Math/Basic/MultiplyNode.cs:13`）。
2. **写 spec**：标题、分类、同义词、槽位（id/name/type/default）、settings、requirements、允许的 Target。
3. **实现行为**：
   - 简单节点：填 `emit.body`/`matrixBody`；
   - 动态节点：实现 `updateSlots`/`emitBody`；
   - 函数型节点：实现 `emitFunction`（注册到 FunctionRegistry）。
4. **单测**：
   - 槽位/默认值/类型推断；
   - 与 Unity 测试用例对齐（如 `Tests/Editor/DynamicSlotTests.cs`、`LiteralFloatTests.cs`、`MaterialNodeTests.cs`）；
   - 生成 HLSL 片段断言。
5. **图 fixture + golden**：至少一个包含该节点的最小图；关键节点加入双后端 golden。
6. **回填 inventory**：状态从 TODO → 实现 → 测试 → golden。

禁止事项：

- 不直接复制 Unity C# 代码；HLSL 语义可以对照，但实现需重写。
- 不引入 Unity 类型/命名（`MaterialSlot`、`AbstractMaterialNode`）到 C++ 公共 API；仅文档中引用。
- 不在节点内硬编码 Target/pass 名称；通过 requirements/block 声明。

## 6. 节点预览

- 触发：编辑器请求某节点预览；或选中节点 300ms 后自动请求。
- 生成：构造临时图（节点输出 → `preview` Target 的 BaseColor），只编译一个 unlit preview pass。
- 渲染：128×128 离屏；使用默认输入值（可被用户 Literal 值覆盖）。
- 缓存：`(graphRevision, nodeId, literalValuesHash)`；编辑后失效。
- 限制：`hasPreview=false` 的节点不生成；纹理节点默认使用内置白/灰/法线纹理。

## 7. 测试要求

| 级别 | 要求 |
|---|---|
| spec | 每个节点必须有 spec 校验（id 唯一、槽位类型合法、默认值类型匹配） |
| 类型 | 动态槽至少覆盖：float/vec2/vec3/vec4、matrix、错误连接 |
| 生成 | 每个节点至少有 HLSL 片段断言；复杂节点有完整函数断言 |
| 图 | 每个类别至少一个组合图（UV→采样→混合→Surface） |
| 渲染 | 代表性节点进入 golden：纹理、法线、噪声、分支、子图、自定义函数、Gradient |
| 兼容 | 未知类型/旧 typeVersion 加载不崩溃，可删除 |

## 8. SG-6 Scene/Lighting/Gradient/Math/Matrix/Transform/UV/Procedural/Artistic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 批次状态

- 已实现 208 个 Input/Basic/Property/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Trig/Wave/Derivative/Vector/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic 与 Utility/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 节点，覆盖动态向量宽度传播、标量 `dot/reflect` 特化、`Rotate About Axis` 单位设置、`Refract` 模式设置、常用空间转换、10 个 UV 变形/视差/三平面节点、6 个 Procedural 形状/棋盘节点、3 个 Noise 节点、7 个 Artistic Adjustment、6 个 Artistic Normal、3 个 Gradient 节点、1 个 Artistic Blend、2 个 Artistic Filter、2 个 Artistic Mask、1 个 Artistic Utility 节点、子图展开与自定义 HLSL 函数。
- `Fresnel Effect` 未连接时走 `gi.normalWS`/`gi.viewDirWS`，连接或 Literal 时走输入表达式。
- UV/Procedural 节点未连接 UV 输入时使用 `gi.uv0`；Rotate 支持角度单位，Flipbook 支持 invert X/Y，Rectangle 支持 fastest/nicest。
- Noise 节点默认 Deterministic hash（Simple/Gradient/Voronoi），兼容 LegacySine/LegacyMod；Simple 3 octaves，Voronoi 输出 Out/Cells。
- Artistic Adjustment：Hue 支持 Degrees/Normalized；Invert Colors 支持动态通道；Channel Mixer 支持 3x3 通道矩阵；White Balance 使用 Unity LMS 矩阵。
- Artistic Normal：Normal Blend 支持 Default/Reoriented；Normal From Height 支持 Tangent/World；Normal Unpack 支持 Tangent/Object；Normal From Texture 的 Texture 端口支持 Property/Asset，未连接时通过 `texture` setting 引用属性。
- Artistic Blend/Filter/Mask/Utility：Blend 支持 22 种模式；Dither/Fade Transition 用于屏幕抖动与噪声过渡；Channel Mask 支持 bitmask；Colorspace Conversion 支持 RGB/Linear/HSV 9 种组合。
- UV Parallax/Triplanar：Parallax Mapping 单步偏移；POM 线性搜索+Secant 细化并输出 PixelDepthOffset；Triplanar 支持 Default/Normal、输入空间与法线输出空间；三者 Texture 端口支持 Property/Asset，Parallax/POM 应用 Texture2D 属性 Tiling/Offset。
- SG-6C Platform：UI 9、Input/UI 6、Terrain 2、Mesh Deformation 2、Custom Interpolators 1；非对应 Target 不进入搜索/validation，`preview` 使用回退表达式。
- Core Math/Logic 50：Channel 5、Math 33、Utility/Logic 12；Swizzle mask 驱动输入/输出宽度、Comparison settings 已补，Modulo 归回 `Math/Advanced`。
- Input/Basic 10：Boolean/Color/Float/Integer/Slider/Time/Vector 2/3/4/Constant；值型节点使用 `value` setting，Vector 2/3/4 同时展开 X/Y/Z/W 可连接 Float 输入，未连接分量从 `value` setting 继承默认值；Constant 使用 `constant` setting。
- Input/Property 1：Property 通过 graph property 引用输出 Boolean/Float/Integer/Vector 2/3/4/Color/Matrix 2/3/4、Gradient、Texture2D/Texture2DArray/Texture3D/Cubemap、SamplerState 与 VirtualTexture 值；解析 `property`/`propertyRef`/`refName` 别名，输出槽显示属性名并继承属性精度；矩阵属性从 mat4 uniform 重建 mat2/mat3；Gradient 默认 key 在代码生成期烘焙为 `HbgSGGradient`，可接入 Sample Gradient；纹理属性可接入对应 Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 的 Texture/Cube 输入并沿用属性 sampler；Texture2D 属性可设置 `useTilingAndOffset`/`tiling`/`offset`，Sample Texture 2D/LOD、Normal From Texture、Parallax Mapping/POM 使用变换后的 UV，Split Texture Transform 输出实际 Tiling/Offset；`_ST` 默认值随 `.sgb` 发布，sidecar 可用 `<refName>_ST` propertyOverride 覆盖。SamplerState 属性输出可连接 Sampler/HeightmapSampler 并驱动目标纹理属性的 sampler；Texture2D 属性的 `defaultType`（white/black/grey/bump/linearGrey/red）写入 `.sgb`，无纹理覆盖时绑定对应 1x1 fallback；VirtualTexture 属性按 `vtLayers` 展开为隐式 Texture2D layer，并可用 `vtLayerRefNames`/`vtLayerTextureTypes` 自定义层引用与 `default`/`normalTangentSpace`/`normalObjectSpace` 类型；Gradient 运行期 LUT/材质覆盖未接。
- Utility/Keyword 1：Boolean keyword 展开 On/Off 输入，Enum keyword 按 entries 动态展开输入槽；代码生成使用 `#if defined(<ref>_<entry>)`/`#ifdef <ref>` 选择对应表达式，支持 `keyword`/`keywordRef`/`refName` 引用别名。
- Utility/Dropdown 1：Graph 可声明独立 `dropdowns`（`name`/`refName`/`value`/`entries`），节点按 `dropdown` 引用展开 A/B… 输入槽；代码生成选择 `value` 指向的 entry（越界时 clamp），v1 使用活动选择而非按变体生成完整 `if` 链；子图展开时提升 dropdown 定义并重命名冲突引用，SubGraph 节点可用 `dropdown.<refName>` setting 为该实例选择 entry（displayName 或 0-based index），非法值回退到图级 `value`。输入槽 ID 对应 entry ID，最多 16 项；嵌套子图逐实例选择与 Unity 主图限制尚未实现。
- Utility/Switch 1：Predicate/Fallback/Out 加最多 26 个 case 输入；`cases` setting 使用 `名称|比较|阈值` 分号分隔编码，手工模式槽位按 Unity 显示 A-Z；Predicate 直连 `floatMode=enum` 的 Property 时自动读取 `enumNames/enumValues` 生成等值 case 标签与阈值；`floorPredicate` 默认对 Predicate 取 floor；生成 `hbgSG_SwitchNode_<id>` 比较函数并先写 Fallback。
- Utility/Preview 1：动态向量 In→Out 直通，保留 `hasPreview` 与 Unity 的 150-1000 尺寸编辑元数据扩展位。
- Utility/Sub-graph 1：`Sub-graph`/`SubGraph`/`Subgraph` 均识别；编译期通过 `subgraph` setting 加载子图并内联，按声明顺序映射属性输入与子图输出，支持外部输入重连、属性提升、嵌套展开与递归检测。节点库提供 1 个可见 In/Out 与 3 个隐藏占位输入/输出（v1 最多 4 输入/输出），编辑器动态槽位仍待接子图资产接口。
- Utility/Custom Function 1：支持 HLSL 字符串函数体与 `.hlsl`/`.cginc`/`.cg` 文件 include；`inputSlots`/`outputSlots` 使用 `名称|类型` 分号编码，输入 ID 0..15、输出 ID 100..115，最多 16 组；函数名/槽位名必须为标识符，生成 `void <name>(...)` 并调用 out 参数；v1 仅覆盖数值/矩阵槽位，纹理/SamplerState 槽位和 include 白名单待后续。
- Input/Geometry 10：位置/法线/切线/副切线/视线方向/视线向量/屏幕位置/顶点色/Vertex ID/Instance ID/UV；支持 Object/View/World/Tangent 空间与 Screen Position 5 种模式，Vertex ID/Instance ID 读取 bgfx `gl_VertexID`/`gl_InstanceID` 并经 flat varying 传入片段。
- Input/Texture 17：Sample Texture 2D/LOD/2D Array/3D、Sample Cubemap、Sample Reflected Cubemap、Sampler State、Texture Size、Gather Texture 2D、Calculate Level Of Detail Texture 2D、Texture 2D/2D Array/3D Asset、Cubemap Asset、Split Texture Transform、Procedural Virtual Texture、Sample Virtual Texture；Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 使用 Unity 槽位；Texture/Cube 输入以及 Texture Size/Gather/Calculate LOD/Normal From Texture 的 Texture 端口可连接 Property/Asset 属性，Texture2D 属性的 Tiling/Offset 会作用于 Sample Texture 2D/LOD 与 Normal From Texture，Split Texture Transform 输出该变换并让 Texture Only 回到恒等；变换使用 `u_sg_<refName>_ST`，默认值由 `.sgb` 携带，材质 sidecar 可通过 `<refName>_ST` propertyOverride 覆盖；2D 采样支持法线解包，Parallax Mapping/POM/Triplanar 的 Texture 端口也支持 Property/Asset，Parallax/POM 应用纹理 Tiling/Offset，Triplanar 使用 node Tile，其他纹理节点仍通过 `texture` setting 引用属性；Sampler State 的 filter/wrap/anisotropic 写入纹理属性并驱动运行期 sampler flags；Sample Texture 2D 支持 `mipSamplingMode` 的 Standard/LOD/Bias/Gradient，Sample Texture 2D Array 支持 LOD 与基于导数的 Bias/Gradient 近似，Sample Texture 3D 支持 Standard/LOD；Bias 在 fragment 使用 `texture2DBias`，vertex 回退到 LOD；`enableGlobalMipBias` 已接（ForwardRenderer 以 `log2(renderScale)` 生成 `u_sg_globalMipBias`，2D 默认 true、2D Array 默认 false，LOD 不加 bias）；纹理属性 `isHDR` 时声明 `u_sg_<refName>_HDR`，Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 在采样后按 Unity `AppendHDRDecodeOperation`/`DecodeHDRSample` 条件解码，sidecar 可用 `<refName>_HDR` 覆盖 decode instructions；Sample Virtual Texture 按 Unity `TextureStack.hlsl` fallback 选择 `texture2D`/`texture2DLod`/`texture2DBias`/`texture2DGrad`，支持 Property 的自定义层引用及 `normalTangentSpace`/`normalObjectSpace` 解包；`sampleQuality=low` 时 Bias/Derivatives 在 fragment 由 ddx/ddy 或显式导数计算 LOD 后走 `texture2DLod`，`high` 走 `texture2DBias`/`texture2DGrad`（对齐 Granite `VirtualTexturing.hlsl` 的 low/high 采样路径）；`noFeedback=true` 且 Lod Level 模式允许输出进入顶点位移，其他组合在输出连接到 Vertex Position 时报错；真实 feedback buffer 未接。`enableGlobalMipBias` 对 Automatic/Bias/Derivatives 生效。
- Input/Matrix 4：Matrix 2x2/3x3/4x4 与 Transformation Matrix；行值构造 GLSL 矩阵，变换矩阵映射引擎全局 uniform；InverseModel/WorldToObject 使用 `u_sg_modelInverse`。
- Transform：支持 Object/World/View/Tangent/AbsoluteWorld/Screen 的 Position/Direction/Normal 转换；Screen 使用 `u_global_invViewProj`/`u_global_viewProj` 做 clip-space 往返，UV 沿用 `gi.screenPos` 的 [0,1] 约定。
- Input/PBR 2：Dielectric Specular 与 Metal Reflectance；支持 Unity 预设常量、Common range 与 Custom IOR。
- Input/Scene 8：Camera/Eye Index/Object/Screen/Scene Color/Scene Depth/Scene Depth Difference/Fog；Camera 输出位置、朝向、正交标志、近远平面、Z 符号与正交宽高；Object 输出模型基向量位置/缩放与单位立方体 bounds；Screen 读取全局屏幕尺寸；Scene Color/Depth 使用图级动态纹理 stage（0-4 内），Scene Depth 支持 raw/linear01/eye；Scene Depth Difference 支持 raw/linear01/eye；Fog 从 `u_fog_*` 输出 v1 高度雾颜色/密度。
- Input/Lighting 4：Ambient/Baked GI/Main Light Direction/Reflection Probe；Ambient 组合 ambient color 与 SH9，Baked GI 使用 SH9 fallback，Reflection Probe 采样环境/探针。
- Input/Gradient 3：Gradient/Sample Gradient/Blackbody；Gradient 最多 8 个合并 color/alpha key，Sample Gradient 支持 blend/fixed/perceptual（OkLab），Blackbody 使用 Mitchell Charity 公式。
- Checkerboard 与形状抗锯齿依赖 `ddx/ddy`/`fwidth`，按片元阶段使用。
- Input/Texture 的 2D Array/3D/Cubemap 声明对应 SAMPLER 类型；运行期 `GraphBinding` 从 sidecar 的纹理路径数组组装真实 layered texture（array layers / 3D slices / cube 6 faces），缺失或非法时绑定同维度白色 fallback；v1 只支持同尺寸/同格式/单 mip 的 sidecar 组装，尚未直接解析 KTX2 多层/立方体容器。HDR 解码由纹理属性 `isHDR` 控制，`GraphBinding` 上传 `u_sg_<refName>_HDR`，sidecar 可用 `<refName>_HDR` 覆盖。虚拟纹理 v1 走 Unity TextureStack fallback：Bias 的 high quality 使用 `texture2DBias`、low quality 计算 LOD 后使用 `texture2DLod`（vertex 回退 LOD），Derivatives 的 high quality 使用 `texture2DGrad`、low quality 计算 LOD 后使用 `texture2DLod`，addressMode 写入隐式层 sampler，自定义层纹理类型在采样后解包；`enableGlobalMipBias` 对 Automatic/Bias/Derivatives 生效；`sampleQuality` 的 low/high 按 Granite 采样路径近似（low 计算 LOD，high 使用 bias/grad）；`noFeedback` 与 Lod Level 组合允许 VT 输出用于顶点位移，其他组合限制为 fragment；本地仍无真实 feedback buffer。真实 Granite VT stack/streaming/feedback 仍未接入。
- 状态由 `docs/shadergraph/node-status.json` 回填到 inventory；当前 `208 TEST / 0 TODO`。
- 剩余：真实 Granite VT stack/streaming/feedback 接入，以及每节点 fixture/golden；详见 `checklists/sg6-nodes.md` §8。

## 9. 对齐参考

- 节点基类/生成接口：`Editor/Data/Nodes/AbstractMaterialNode.cs:16`、`Editor/Data/Interfaces/IGeneratesBodyCode.cs:3`、`IGeneratesFunction.cs:3`。
- 动态槽：`Editor/Data/Graphs/DynamicValueMaterialSlot.cs:14/80`、`Editor/Data/Implementation/NodeUtils.cs:22/494`。
- 函数注册：`Editor/Data/Util/FunctionRegistry.cs:17`。
- 代表节点：`Editor/Data/Nodes/Math/Basic/MultiplyNode.cs:13`、`Editor/Data/Nodes/Input/Texture/SampleTexture2DNode.cs:18`、`Editor/Data/Nodes/Input 下的 PropertyNode.cs:14`、`Editor/Data/Nodes/Utility/CustomFunctionNode.cs:16`、`Editor/Data/Nodes/Utility/SubGraphNode.cs:13`、`Editor/Data/Nodes/Utility/PreviewNode.cs:6`、`Editor/Data/Nodes/Utility/Logic/SwitchNode.cs:12`、`Editor/Data/Nodes/BlockNode.cs:12`、`Editor/Data/Nodes/Channel/SplitNode.cs:9`、`Editor/Data/Nodes/Channel/CombineNode.cs:8`、`Editor/Data/Nodes/Utility/Logic/BranchNode.cs:7`。
- 子图：`Editor/Data/SubGraph/SubGraphOutputNode.cs:14`。
- 节点搜索/分类：`Editor/Drawing/SearchWindowProvider.cs:26`、`Editor/Data/Nodes/NodeClassCache.cs`。
- Unity 测试：`Tests/Editor/DynamicSlotTests.cs`、`LiteralFloatTests.cs`、`MaterialNodeTests.cs`、`PropertyChunkTests.cs`、`ShaderGeneratorTests.cs`、`FixedSlotTests.cs`。
