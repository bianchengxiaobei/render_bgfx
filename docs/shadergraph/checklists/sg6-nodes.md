# SG-6 全量节点

> 目标：完成 208 个 `[Title]` 节点 + 内部节点（Block/Redirect/SubGraphOutput/Legacy），并让每个节点有可执行验证。
> 前置：SG-2（节点框架/编译器）、SG-3（渲染验证）、SG-5（编辑器/节点预览）。
> 清单：[nodes-inventory.md](../nodes-inventory.md)（自动生成，状态列在这里回填）。

---

## 1. 批次

| 子阶段 | 范围 | 数量 | 出口 |
|---|---|---:|---|
| SG-6A | 批次 A 收尾（SG-2 已做 60+） | 23 | inventory A 全绿 |
| SG-6B | 批次 B：Math/Trig/Vector/Wave/Matrix/Derivative、Artistic、UV、Procedural、Input 扩展、剩余 Texture | 105 | inventory B 全绿 |
| SG-6C | 批次 C：UI、Input/UI、Terrain、Mesh Deformation、Custom Interpolators | 20 | inventory C 全绿（不可用 Target 置灰） |
| SG-6D | 特殊节点：SubGraph、CustomFunction、Keyword/Dropdown/Property/Switch/Preview、Block/Redirect/Legacy | - | 端到端用例 |
| SG-6E | 节点预览 | - | 视口内按需生成/缓存 |
| SG-6F | 覆盖审计 | - | `TODO` 清零；每节点至少一个测试 |

> 数量来自 inventory 自动统计（A 83 / B 105 / C 20 / 合计 208）。批次只是顺序，不是裁剪。

## 2. 任务

| ID | 任务 | 产物 | 参考 | 验收 |
|---|---|---|---|---|
| SG6-1 | 批次 A 收尾 | `shadergraph/src/nodes/...` | 见 inventory A 行 | A 组单测/生成断言 |
| SG6-2 | 批次 B：Math 三角函数/向量/波形/矩阵/导数 | 节点实现 | `Editor/Data/Nodes/Math/**` | 动态类型/矩阵语义测试 |
| SG6-3 | 批次 B：Artistic/UV/Procedural | 节点实现 | `Editor/Data/Nodes/Artistic/**`、`UV/**`、`Procedural/**` | 代表性 golden |
| SG6-4 | 批次 B：Input/Lighting/Scene/Gradient/PBR/Matrix、Texture 扩展 | 节点实现 | `Editor/Data/Nodes/Input/**` | 引擎输入绑定/占位策略 |
| SG6-5 | 批次 C：UI/Input/UI/Terrain/MeshDeformation/CustomInterpolators | 节点实现 + Target gating | `Editor/Data/Nodes/UI/**`、`Terrain/**`、`MeshDeformation/**` | 不支持 Target 时 UI 禁用 |
| SG6-6 | 子图：展开/属性提升/递归/精度切换 | `subgraph.cpp`/节点 | `Editor/Data/Nodes/Utility/SubGraphNode.cs:13`、`SubGraphOutputNode.cs:14` | 嵌套/冲突/循环用例 |
| SG6-7 | Custom Function：HLSL 文本/文件、槽位、include 白名单 | 节点 + 编辑器 | `Editor/Data/Nodes/Utility/CustomFunctionNode.cs:16` | 编译/错误定位/安全 |
| SG6-8 | Keyword/Dropdown/Property/Switch/Preview 节点 | 节点 + Blackboard 联动 | `Editor/Data/Nodes/Utility/KeywordNode.cs:14`、`DropdownNode.cs:13`、`PreviewNode.cs:6`、`Logic/SwitchNode.cs:12`、`Input/PropertyNode.cs:14` | 增删属性/关键字/dropdown 同步；Switch case 比较；Preview 直通 |
| SG6-9 | Block/Redirect/SubGraphOutput/Legacy 节点 | 内部节点 | `Editor/Data/Nodes/BlockNode.cs:12`、`RedirectNode.cs`、`LegacyUnknownTypeNode.cs` | 加载/删除/错误恢复 |
| SG6-10 | Gradient/Blackbody/SampleGradient v1（代码生成期 key 采样；Blackboard LUT 待补） | 节点 | `Editor/Data/Nodes/Input/Gradient/**` | 双后端编译/公式断言 |
| SG6-11 | 节点预览：临时图/缓存/异步/性能模式 | 宿主+前端 | `Editor/Drawing/PreviewManager.cs:24` | 视口内按需；可关闭 |
| SG6-12 | 覆盖审计/状态回填/文档 | inventory + `nodes.md` | - | TODO=0；测试覆盖表 |

> 状态：节点预览宿主 v1 已由 SG-4 提供（临时上游图、202→PNG、2D/3D 选择、缓存）；SG6-11 剩余前端视口接入、性能模式和更多节点视觉对齐。

## 3. 逐节点执行模板

每个节点完成后必须提交：

1. **spec**：type/title/category/synonyms/slots/defaults/settings/requirements/targets。
2. **实现**：body/function/updateSlots/validate。
3. **单测**：
   - 槽位与默认值；
   - 动态类型传播（至少 float/vec2/vec3/vec4；适用时 matrix）；
   - 错误输入（类型不匹配/缺输入）。
4. **生成断言**：HLSL 片段包含关键表达式/函数。
5. **图 fixture**：最小图（节点 + Surface）可编译。
6. **inventory 状态**：`TODO → IMPL → TEST → GOLDEN`。

禁止：

- 未实现节点不得静默生成默认值；必须报 `SG_NODE_NOT_IMPLEMENTED`（或从搜索中隐藏）。
- 不允许复制 Unity C#/HLSL；只对齐语义。
- 不允许节点内部硬编码 Target/stage；通过 requirements/target 声明。

## 4. 特殊节点验收

| 节点 | 验收 |
|---|---|
| SubGraph | 嵌套 3 层；属性重名冲突；递归检测；展开后与内联图等价 |
| CustomFunction | 文本/文件两种模式；输入输出槽动态；include 白名单；shaderc 错误定位 |
| Keyword/Dropdown | 布尔/枚举；变体数正确；删除关键字后节点报错可修复 |
| Switch | 0/2/26 case；comparison/threshold；floorPredicate；Fallback；超 26 报错 |
| Preview | 动态 In→Out；类型宽度不变；hasPreview 元数据 |
| Property | 类型/精度/默认值；与 Blackboard 同步；材质 override |
| Block/SubGraphOutput | 必需 block 缺失自动补默认；删除/恢复；Target 切换 |
| Legacy/Unknown | 旧 JSON 加载；显示为 Unknown；可删除不崩溃 |
| Gradient | keys 排序/重复报错；LUT 生成/采样；默认渐变 |

## 5. 验收

```powershell
# 覆盖清单（实现时提供脚本）
py -3 tools/sg_node_inventory.py
py -3 tools/sg_node_coverage.py --fail-on-todo   # 计划新增

# 节点测试
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "sg_nodes"

# 代表性 golden
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\nodes\*.gltf --frames=3
```

通过标准：

- inventory 208 行全部 `TEST`/`GOLDEN`，无 `TODO`。
- 每个节点至少一个单测/生成断言；代表性类别有双后端 golden。
- 子图/CustomFunction/Dropdown/Keyword 的端到端用例通过。
- 不支持 Target 的节点在 UI 中禁用并有原因；直接 JSON 使用时报 `SG_TARGET_UNSUPPORTED`。

## 6. Known limits（初始）

- UI/Terrain/MeshDeformation 节点在对应 Target 落地前只保证编译/禁用行为，不保证渲染效果。
- 自定义插值器 v1 仅基础支持；复杂打包/布局在 SG-7。
- 节点预览的视觉与 Unity 可能有差异（预览 mesh/光照不同）。
- Virtual Texture 节点 v1 走 Unity TextureStack fallback（普通 Texture2D 层、层类型解包、LOD/Bias/Derivatives）；真实 Granite VT stack/streaming/feedback 未接，避免误当作硬件 VT。

## 7. 对齐参考

- 节点框架/动态槽：`Editor/Data/Nodes/AbstractMaterialNode.cs:16`、`Editor/Data/Graphs/DynamicValueMaterialSlot.cs:14/80`、`Editor/Data/Implementation/NodeUtils.cs:22/494`。
- 代表节点：`Editor/Data/Nodes/Math/Basic/MultiplyNode.cs:13`、`Input/Texture/SampleTexture2DNode.cs:18`、`Input/PropertyNode.cs:14`、`Utility/CustomFunctionNode.cs:16`、`Utility/SubGraphNode.cs:13`、`Utility/KeywordNode.cs:14`、`Utility/DropdownNode.cs:13`、`Utility/PreviewNode.cs:6`、`Utility/Logic/SwitchNode.cs:12`、`BlockNode.cs:12`、`Channel/SplitNode.cs:9`、`Channel/CombineNode.cs:8`、`Utility/Logic/BranchNode.cs:7`。
- 子图：`Editor/Data/SubGraph/SubGraphOutputNode.cs:14`。
- 预览：`Editor/Drawing/PreviewManager.cs:24`。
- 测试：`Tests/Editor/DynamicSlotTests.cs`、`LiteralFloatTests.cs`、`MaterialNodeTests.cs`、`PropertyChunkTests.cs`、`ShaderGeneratorTests.cs`、`FixedSlotTests.cs`、`GradientUndoTests.cs`。

---

## 8. SG-6 Input/Basic/Property/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 节点 v1（2026-11-25）

已落地 208 个 Input/Basic/Property/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 节点（inventory `TODO` 清零）：

- Trigonometry 12：Arccosine/Arcsine/Arctangent/Arctangent2/Cosine/Degrees To Radians/Hyperbolic Cosine/Hyperbolic Sine/Hyperbolic Tangent/Radians To Degrees/Sine/Tangent。
- Wave 4：Sawtooth Wave/Triangle Wave/Square Wave/Noise Sine Wave（v1）。
- Derivative 3：DDX/DDXY/DDY（动态向量，片段阶段）。
- Vector 11：Dot Product/Cross Product/Distance/Fresnel Effect/Projection/Reflection/Rejection/Rotate About Axis/Sphere Mask/Refract/Transform。
- Matrix 4：Matrix Construction/Matrix Determinant/Matrix Split/Matrix Transpose；Matrix Split 输出宽度随矩阵维度 2/3/4 切换。
- Transform：Object/World/View/Tangent/AbsoluteWorld/Screen 的 Position/Direction/Normal 转换，支持 normalize；World/View/Tangent->Object 使用 engine inverse model，Object Normal 使用 Unity 的 model/inverse-model 语义；Screen 通过 `u_global_invViewProj`/`u_global_viewProj` 做 clip-space 往返，Screen UV 沿用 `gi.screenPos` 的 [0,1] 约定。
- UV 10：Tiling And Offset/Rotate/Polar Coordinates/Radial Shear/Spherize/Twirl/Flipbook/Parallax Mapping/Parallax Occlusion Mapping/Triplanar；未连接 UV 输入使用 `gi.uv0`，Rotate 支持 radians/degrees，Flipbook 支持 invert X/Y。
- Procedural 6：Checkerboard/Ellipse/Polygon/Rectangle/Rounded Polygon/Rounded Rectangle；Checkerboard 与形状抗锯齿使用 `ddx/ddy`/`fwidth`，Rectangle 支持 fastest/nicest。
- Procedural Noise 3：Simple Noise/Gradient Noise/Voronoi；默认 Deterministic hash，支持 LegacySine/LegacyMod；Simple 3 octaves，Voronoi 输出 Out/Cells。
- Artistic Adjustment 7：Contrast/Saturation/Hue/White Balance/Replace Color/Invert Colors/Channel Mixer；Hue 支持 Degrees/Normalized，Invert 支持动态通道，Channel Mixer 支持 3x3 通道矩阵。
- Artistic Normal 6：Normal Blend/Normal Strength/Normal Reconstruct Z/Normal From Height/Normal From Texture/Normal Unpack；支持 Default/Reoriented、Tangent/World、Tangent/Object。
- Artistic Blend 1：Blend，支持 22 种混合模式与 Opacity。
- Artistic Filter 2：Dither/Fade Transition；Dither 支持默认屏幕坐标或自定义 Screen Position。
- Artistic Mask 2：Channel Mask/Color Mask；Channel Mask 支持动态通道与 bitmask，Color Mask 支持 Range/Fuzziness。
- Artistic Utility 1：Colorspace Conversion，支持 RGB/Linear/HSV 的 9 种组合。
- UV Parallax/Triplanar 3：Parallax Mapping/Parallax Occlusion Mapping/Triplanar；POM 实现线性搜索+Secant 细化的光线步进，Triplanar 支持 Default/Normal 与输入/输出空间转换。
- SG-6C target-gated 20：UI 9、Input/UI 6、Terrain 2、Input/Mesh Deformation 2、Custom Interpolators 1；仅 `preview` 与对应 `ui`/`terrain`/`mesh-deformation` Target 可见，非对应 Target 由 validation 拒绝。
- Core Math/Logic 50：Channel 5、Math/Advanced 10、Math/Basic 6、Math/Interpolation 3、Math/Range 8、Math/Round 6、Utility/Logic 12；补充 Swizzle `order` 与 Comparison `comparison` settings，Modulo 归回 `Math/Advanced`。
- Input/Basic 10：Boolean/Color/Float/Integer/Slider/Time/Vector 2/3/4/Constant；值型节点使用 `value` setting，Vector 2/3/4 同时展开 Unity 的 X/Y/Z/W 可连接 Float 输入，未连接分量从 `value` setting 继承默认值；Constant 使用 `constant` setting，Time 读取 `gi.time`/`gi.deltaTime`；`input_vector_components` fixture 验证 4 个 Float 分量接入 Vector 4。
- Input/Property 1：Property 引用 graph property 输出 Boolean/Float/Integer/Vector 2/3/4/Color/Matrix 2/3/4/Gradient/Texture2D/Texture2DArray/Texture3D/Cubemap/SamplerState/VirtualTexture；`preparePropertyNodes` 解析 `property`/`propertyRef`/`refName` 并归一化为 `property`，输出槽类型/显示名/精度继承属性；矩阵属性从 mat4 uniform 按 packed 布局重建 mat2/mat3；Gradient 属性在代码生成期烘焙默认 key 为 `HbgSGGradient`，可连接 Sample Gradient；纹理属性可连接对应 Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 的 Texture/Cube 输入；SamplerState 属性可连接 Sampler/HeightmapSampler；VirtualTexture 属性按 `vtLayers` 展开 layer fallback，并支持 `vtLayerRefNames`/`vtLayerTextureTypes`；`property_float`/`property_matrix`/`property_gradient`/`property_texture` fixture 覆盖动态属性读取、Matrix Split、Gradient 采样与纹理采样连接。
- Utility/Keyword 1：Boolean keyword 展开 On/Off 动态输入，Enum keyword 按 entries 展开输入槽；`updateSlots` 在 concretize 阶段同步槽位，代码生成按 `_MODE` / `_MODE_<entry>` 预处理器条件选择输入表达式；`keyword_bool` fixture 覆盖 On/Off 分支，`keyword_enum` fixture 覆盖 Enum 条件分支。
- Utility/Dropdown 1：Graph 独立 `dropdowns`（`name`/`refName`/`value`/`entries`），节点以 `dropdown` 引用展开 entry 输入槽，输出动态向量；entry id 同时作为稳定 slot id，最多 16 项；代码生成按活动 `value` 选择输入并在越界时 clamp；子图展开时提升定义并重命名冲突 refName；SubGraph 节点可用 `dropdown.<refName>` 为每个实例选择 entry（displayName 或 0-based index），非法值回退到图级 `value`，展开期直接特化；`dropdown_values` fixture 验证 A/B entry 与选中值。尚未覆盖嵌套子图逐实例选择，也未生成 Unity 运行时 `if (dropdown == n)` 链。
- Utility/Switch 1：Predicate/Fallback/Out 与 A-Z 最多 26 个 case 输入；`cases` setting 使用分号分隔的 `名称|比较|阈值` 编码，手工模式槽位显示 A-Z；Predicate 直连 `floatMode=enum` 的 Property 时自动使用 `enumNames/enumValues` 生成等值 case 槽，`floorPredicate` 默认 true；生成 `hbgSG_SwitchNode_<id>` 函数，先赋 Fallback，再按 case comparison/threshold 生成 if/else if；`switch_values`/`switch_property` fixture 覆盖双后端编译。
- Utility/Preview 1：动态向量 In→Out 直通；保留 `hasPreview`，Unity 的 width/height 编辑器元数据仅占位，未进入 Web Inspector。
- Utility/Sub-graph 1：`Sub-graph`/`SubGraph`/`Subgraph` 类型均可加载；`subgraph` setting 在编译期通过 `SubGraphLibrary` 展开，按声明顺序映射属性输入与输出，支持外部输入重连、属性提升、嵌套展开、递归检测；节点库提供 1 个可见 In/Out 与 3 个隐藏占位输入/输出（v1 最多 4 输入/输出），编辑器动态槽位尚未绑定子图资产。
- Utility/Custom Function 1：支持字符串 HLSL 函数体与 `.hlsl`/`.cginc`/`.cg` include；`inputSlots`/`outputSlots` 分号编码 `名称|类型`，输入 ID 0..15、输出 ID 100..115，最多 16 组；生成 `void <name>(...)` 并按槽位顺序传参；`custom_function` 与 `custom_function_file` fixture 双后端编译通过；v1 不覆盖纹理/SamplerState 槽位、include 白名单和 Unity GUID 源引用。
- Input/Geometry 10：Position/Normal Vector/Tangent Vector/Bitangent Vector/View Direction/View Vector/Screen Position/Vertex Color/Vertex ID/Instance ID/UV；Position/方向节点支持 Object/View/World/Tangent（绝对世界退回 World），Screen Position 支持 Default/Raw/Center/Tiled/Pixel，UV 支持 channel 0/1；Vertex ID/Instance ID 分别读取 VS 的 `gl_VertexID`/`gl_InstanceID`，并经 `flat` varying 传到片段（Unity Vertex ID 本身仅 vertex stage）。
- Input/Texture 17：Sample Texture 2D/LOD/2D Array/3D、Sample Cubemap/Reflected Cubemap、Sampler State、Texture Size、Gather Texture 2D、Calculate Level Of Detail Texture 2D、Texture 2D/2D Array/3D Asset、Cubemap Asset、Split Texture Transform、Procedural Virtual Texture/Sample Virtual Texture；2D/LOD 采用 Unity 槽位（2D：RGBA 0/Texture 1/UV 2/Sampler 3/R..A 4..7；LOD：RGBA 0/Texture 1/UV 2/Sampler 3/LOD 4/R..A 5..8；Array：RGBA 0/Texture 1/UV 2/Sampler 3/R..A 4..7/Index 8；3D：RGBA 0/Texture 1/UV 2/Sampler 3/R..A 5..8；Cubemap：Out 0/Cube 1/Dir 2/Sampler 3/LOD 4；Reflected：Out 0/Cube 1/ViewDir 2/Normal 3/LOD 4/Sampler 5）；上述 Texture/Cube 输入以及 Texture Size/Gather Texture 2D/Calculate Level Of Detail Texture 2D/Normal From Texture 的 Texture 端口支持 Property/Asset 属性；Texture Size 采用 Width 0/Texture 1/Height 2/Texel Width 3/Texel Height 4，Gather 采用 RGBA 0/Texture 1/UV 2/Sampler 3/Offset 4/R..A 5..8，Calculate LOD 采用 LOD 0/Texture 1/UV 2/Sampler 3；2D 采样支持 Normal/Tangent/Object 法线解包，2D Array/3D/Cubemap 使用 `texture2DArray`/`texture3D`/`textureCubeLod` 采样，资产节点声明对应 SAMPLER 类型，运行期由 sidecar 图像数组组装 layered texture；Sampler State 将 filter/wrap/anisotropic 写入纹理属性；Texture2D 属性的 `useTilingAndOffset`/`tiling`/`offset` 会作用于 Sample Texture 2D/LOD 与 Normal From Texture，Split Texture Transform 输出该变换并让 Texture Only 恢复恒等；虚拟纹理 v1 将 PVT 层展开为隐式 Texture2D；LOD 走 `texture2DLod`，Bias/Derivatives 按 `sampleQuality` 的 high/low 选择 `texture2DBias`/`texture2DGrad` 或计算 LOD 后走 `texture2DLod`。
- Input/Matrix 4：Matrix 2x2/Matrix 3x3/Matrix 4x4/Transformation Matrix；行设置转 GLSL 列主序矩阵，Transformation Matrix 映射 Model/InverseModel/View/InverseView/Projection/InverseProjection/ViewProjection/InverseViewProjection。
- Input/PBR 2：Dielectric Specular/Metal Reflectance；Dielectric 支持 Common/Rusted Metal/Water/Ice/Glass/Custom 与 IOR，Metal 支持 10 种金属常量。
- Input/Scene 8：Camera/Eye Index/Object/Screen/Scene Color/Scene Depth/Scene Depth Difference/Fog；Camera 从 `u_global_invView`/`u_global_cameraPos`/`u_global_frustumPlanes`/`u_global_proj` 输出位置、朝向、投影类型、近远平面、Z 符号与正交宽高；Object 从 `u_model[0]` 基向量输出位置、缩放与单位立方体包围盒；Screen 读取 `u_global_screenSize`；Eye Index 当前为 0；Scene Color/Depth 在编译期占用 graph stage 0-4 的空闲槽（`requiredFeatureMask` 记录 SceneColor/SceneDepth/Fog），Scene Depth 支持 raw/linear01/eye，Scene Depth Difference 支持 raw/linear01/eye，Fog 从 `u_fog_*` 输出 v1 高度雾颜色/密度。
- Input/Lighting 4：Ambient/Baked GI/Main Light Direction/Reflection Probe；Ambient 组合 `u_pass_ambientColor` 与 SH9；Baked GI 走 SH9 fallback；Main Light Direction 读取主方向光；Reflection Probe 通过 `lighting/environment.sc` 组合探针与天空环境。
- Input/Gradient 3：Gradient/Sample Gradient/Blackbody；Gradient 按 time 排序最多 8 个 key，生成 Unity 兼容的 `HbgSGGradient`/`hbgSGNewGradient` 常量结构；Sample Gradient 支持 blend/fixed/perceptual（OkLab）三种模式；Blackbody 使用 Mitchell Charity 公式；Web Inspector 以 JSON 编辑 keys 并显示渐变预览。
- 动态槽沿用 Unity `DynamicValueMaterialSlot` 的宽度规则；Scalar `dot/reflect` 生成标量表达式，避免非法 `dot(float,float)`。
- UV/Procedural 节点默认 UV 输入绑定 `gi.uv0`（连接或显式 Literal 时使用输入值），与 Unity `Binding.MeshUV0` 一致。
- `Fresnel Effect` 未连接输入时使用 `gi.normalWS`/`gi.viewDirWS`，连接或显式 Literal 时使用输入值。
- `Rotate About Axis` 支持 `unit=radians|degrees`；`Refract` 支持 `mode=safe|critical`，Intensity 为当前近似 Fresnel 项。
- `Noise Sine Wave` typeVersion=1；默认 `MinMax=(-0.5,0.5)`。

验证：

- `hbg_sg_tests`：**174 cases / 8124 assertions**（`tests/compiler/test_math_nodes.cpp`、`tests/compiler/test_core_math_logic_nodes.cpp`、`tests/compiler/test_input_basic_nodes.cpp`、`tests/compiler/test_input_property_node.cpp`、`tests/compiler/test_keyword_node.cpp`、`tests/compiler/test_dropdown_node.cpp`、`tests/compiler/test_utility_switch_preview_nodes.cpp`、`tests/compiler/test_utility_subgraph_node.cpp`、`tests/compiler/test_utility_custom_function_node.cpp`、`tests/compiler/test_input_geometry_nodes.cpp`、`tests/compiler/test_input_texture_nodes.cpp`、`tests/compiler/test_input_matrix_pbr_nodes.cpp`、`tests/compiler/test_input_scene_lighting_nodes.cpp`、`tests/compiler/test_input_gradient_nodes.cpp`、`tests/compiler/test_uv_nodes.cpp`、`tests/compiler/test_uv_parallax_triplanar_nodes.cpp`、`tests/compiler/test_procedural_nodes.cpp`、`tests/compiler/test_procedural_noise_nodes.cpp`、`tests/compiler/test_artistic_nodes.cpp`、`tests/compiler/test_artistic_normal_nodes.cpp`、`tests/compiler/test_artistic_filter_mask_nodes.cpp`、`tests/compiler/test_ui_target_nodes.cpp`）。
- `hbg_tests`：**325 cases / 33719 assertions** 全绿；新增 `texture loader composes layered 2D array, 3D, and cubemap sources`、`sgb runtime parses advanced texture property kinds`、`sgb runtime parses texture default types`、`sgb runtime parses custom virtual texture stack layers`、`sgb runtime parses texture transform defaults and material overrides`，覆盖 layered 组装/维度解析、`.sgb` `defaultType` 默认值、`_ST` 默认值和自定义 VT 层引用，以及 `<ref>_ST` 材质覆盖。
- `sgbake --keep-hlsl` 对 Math/Matrix/Transform（含 Screen 链）、7 个基础 UV 节点串联/默认图、Parallax Mapping、Parallax Occlusion Mapping、Triplanar Default/Normal、6 个 Procedural 节点（含 Rectangle fastest/nicest）、3 个 Noise 节点（Deterministic/Legacy 两组）、7 个 Artistic Adjustment、6 个 Artistic Normal，以及 Blend 22 模式、Dither、Fade Transition、Channel Mask、Color Mask、Colorspace Conversion 9 组合在 dxbc/spirv 均生成 8 shaders；SG-6C 20 节点在 `preview` Target 生成 dxbc/spirv 各 2 shaders；Core Math/Logic 50 节点图、Input/Basic 10 节点图、Input/Basic Vector Components 16 shaders、Property Gradient 16 shaders、Property Texture 16 shaders、Input/Geometry 10 节点图（16 shaders）、Geometry IDs 2 节点图（16 shaders）、Input/Texture 5 节点图、Input/Matrix+PBR 6 节点图、Input/Scene+Lighting 8 节点图、Input/Scene 4 节点图、Input/Gradient 3 节点图与 Utility/Switch 2 case 图在 dxbc/spirv 各生成 8 shaders；Input/Texture 高级 4 节点图、Sampler State 1 节点图、Virtual Texture 2 节点图、Virtual Texture Stack 3 层图、Texture Mip Sampling 4 模式图与 Texture Mip Sampling Array/3D 图各生成 16 shaders，确认双后端 shaderc 语法可编译；`enableGlobalMipBias` 的 2D 默认/关闭、Array opt-in 与 VT 路径由 source 断言覆盖。
- `sgbake --check tests/shadergraph/assets`：**48 assets** 全部匹配（默认 dxbc+spirv）；新增 `input_vector_components` fixture 验证 Vector 4 的 X/Y/Z/W 分量输入与 16 shaders 双后端编译，`property_gradient` fixture 验证 Gradient 属性输出接入 Sample Gradient 与 16 shaders 双后端编译，`property_texture` fixture 验证 Texture2D 属性输出接入 Sample Texture 2D Texture 输入与 16 shaders 双后端编译，`texture_size` fixture 验证 Property Texture 输入与 Width/Height/Texel Size 输出，`texture_transform` fixture 验证 Texture2D 属性 Tiling/Offset 通过 `_ST` 作用于直接采样与 Split/Texture Only 链路，默认值写入 `.sgb`，sidecar `<ref>_ST` 覆盖可改变渲染结果，`transform_spaces` fixture 验证 World->Object Position/Normal 的 inverse-model uniform 与双后端编译，`transform_screen` fixture 验证 Screen->World->Object->Screen 的 clip-space/inverse-model 链与 16 shaders 双后端编译，`scene_access` fixture 验证 SceneColor/SceneDepth/Fog feature mask 与 stage contract，`texture_advanced` fixture 验证 Property 的 2D Array/3D/Cubemap Texture/Cube 输入、双后端采样语法与运行期 layered sidecar 绑定，`texture_defaults` fixture 验证 white/black/grey/bump/red 的 `.sgb` defaultType 编码与运行期 1x1 fallback 选择，`sampler_state` fixture 验证 filter/wrap/anisotropy 写入 `.sgb` 属性表，`virtual_texture` fixture 验证 PVT 层属性展开与 clamp 采样，`virtual_texture_stack` fixture 验证 VirtualTexture Property 的自定义层引用/法线类型、Bias+Derivatives 采样、sampleQuality low/high 路径与 16 shaders 双后端编译，`texture_mip_sampling` fixture 验证 Sample Texture 2D 的 Standard/LOD/Bias/Gradient 四模式，`texture_mip_sampling_array` fixture 验证 Sample Texture 2D Array 的 LOD/Bias/Gradient 与 Sample Texture 3D 的 LOD，`geometry_ids` fixture 验证 `gl_VertexID`/`gl_InstanceID` 及 flat varying 双后端编译，`property_float` fixture 验证 Property 节点读取 `_Amount` 浮点属性，`property_matrix` fixture 验证 Matrix4 属性输出接入 Matrix Split 与 16 shaders 双后端编译，`channels_flip` fixture 验证 Flip 逐通道取反与 16 shaders 双后端编译，`channels_swizzle` fixture 验证 mask 驱动的输入/输出宽度与 16 shaders 双后端编译，`switch_property` fixture 验证上游 Float Enum Property 自动生成 case 槽与 16 shaders 双后端编译，`keyword_bool` fixture 验证 Boolean Keyword 的 On/Off 分支，`keyword_enum` fixture 验证 Enum Keyword 条件分支，`switch_values` fixture 验证 Predicate/Fallback/case 比较函数，`dropdown_values` fixture 验证 Dropdown A/B entry 与活动选择，`custom_function`/`custom_function_file` fixture 验证字符串与 include 两种 Custom Function 生成。`subgraph_passthrough` 保持 Sub-graph 占位直通；真实子图展开由 `test_utility_subgraph_node.cpp` 覆盖。`switch_values` 在 D3D11/Vulkan forward render smoke 通过；`ctest -R hbg_sg` 2/2。
- 纹理变换运行期：`texture_transform.sgb` 在 D3D11 forward、`lit_graph.gltf`、`--frames=3 --camera-orbit` 下，默认 `_MainTex_ST=(2,3,0.25,0.5)` 与 sidecar `_MainTex_ST=(4,5,0.1,0.2)` 截图 **97,791 px / max115** 差异；无 GraphBinding/unknown override 告警。
- 高级纹理运行期：`texture_advanced.sgb` 在 D3D11 forward、`lit_graph.gltf`、`--frames=3 --camera-orbit` 下，sidecar 用 2 张 2D Array、2 张 Texture3D slice、6 张 Cubemap face 组装；日志仅有 graph material enabled，无 layered invalid/upload fallback 告警；真实 layered vs 同维度白色 fallback 截图 **97,933 px / max12** 差异。
- Texture defaultType 运行期：`texture_defaults.sgb` 在 D3D11 forward、`lit_graph.gltf`、1280×720、`--frames=3` 下，无 textureOverrides；`_Red` 的 `defaultType=red` vs 临时改为 `white` 的同源 fixture 截图 **97,682 px / max197** 差异，中心像素 `(244,56,40)` vs `(227,227,228)`；D3D11 vs Vulkan **0 diff**，确认 `.sgb` defaultType 与 `GraphBinding` 1x1 fallback 选择已生效。
- VT fallback stack 运行期：`virtual_texture_stack.sgb` 在 D3D11/Vulkan forward、`lit_graph.gltf`、640×360、`--frames=3 --camera-orbit` 下，sidecar 用自定义层 `_VTStack_Base`/`_VTStack_Normal`/`_VTStack_Data` 覆盖；日志仅有 graph material enabled，无缺失层告警；自定义层 vs 白色 fallback 截图 **24,483 px / max125**（640×360）差异；D3D11 vs Vulkan **0 diff**。
- 纹理 mip 采样运行期：`texture_mip_sampling.sgb` 在 D3D11/Vulkan forward、`lit_graph.gltf`、640×360 下通过，sidecar `_MainTex=grid.bmp` 采样四种模式；D3D11 vs Vulkan **14 px / max1** 差异。
- 全局 mip bias 运行期：临时关闭 `enableGlobalMipBias` 的同源 fixture 与 1024×1024 checker DDS 对比，`--render-scale=0.5` 时 enabled vs disabled **25,424 px / max39**，scale=1 时 **0 diff**；D3D11 vs Vulkan scale=0.5 **6 px / max1**，确认 `u_sg_globalMipBias=(log2(renderScale), 2^bias)` 已绑定。
- HDR decode 运行期：临时 `isHDR=true` fixture 使用 640×360 `grid.bmp`，sidecar `_MainTex_HDR=(2,0,0,0)` vs `(1,1,0,0)` 在 D3D11/Vulkan 均 **24,483 px / max48**，确认 `u_sg__MainTex_HDR` 与 Unity 等价解码已生效；D3D11 vs Vulkan decode 图 **0 diff**。
- `docs/shadergraph/nodes-inventory.md` 状态由 `node-status.json` 回填：**208 TEST / 0 TODO**。

仍缺口：

- 批次 C 已完成 target-gated v1；Core Math/Logic 50、Input/Basic 10、Input/Property 1、Utility/Keyword 1、Utility/Dropdown 1、Utility/Switch 1、Utility/Preview 1、Utility/Sub-graph 1、Utility/Custom Function 1、Input/Geometry 10、Input/Texture 17、Input/Matrix 4、Input/PBR 2、Input/Scene 8、Input/Lighting 4 与 Input/Gradient 3 已回填 TEST，inventory 无 TODO。剩余为真实 Granite VT stack/streaming/feedback 接入与 fixture/golden。
- 未生成每节点图 fixture/双后端 golden；DDX/DDY 在 vertex displacement 图中仍有限制。
- Custom Function v1 尚未支持纹理/SamplerState 槽位、Unity GUID 源引用与 include 白名单。
- Dropdown v1 使用 Graph 级活动 `value` 选择输入；SubGraph 节点可用 `dropdown.<refName>` 为实例选择 entry，非法值回退到图级 `value`。嵌套子图逐实例选择尚未覆盖，本地也尚未生成 Unity 运行时 `if (<ref> == n)` 链；本地编译器允许主图使用（Unity `allowedInMainGraph=false`）。
- SG-6C 节点当前只在 `preview` Target 生成回退表达式；`ui`/`terrain`/`mesh-deformation` Target 尚未实装，主图搜索/validation 会按 Target 过滤或拒绝。
- `Sample Element Texture` v1 固定 4 个采样输出，`sampleCount`/`mipSamplingMode` 暂未驱动动态槽；`Instance` 固定 vec4 输出。
- `Matrix Split` 的 row/column 索引在当前 mat 表示下只保证生成可编译代码，数值语义待 golden。
- `Transform` 的 Screen 转换依赖 `gi.screenPos` 的 [0,1] UV/depth 约定；顶点阶段若上游为 Screen Position 仍受其零值限制（既有阶段限制）。
- Parallax Mapping/POM/Triplanar 的 Texture 端口支持 Property/Asset，Sampler/HeightmapSampler 支持 Sampler State；Parallax/POM 应用 Texture2D 属性的 Tiling/Offset，Triplanar 仍使用 node Tile/Blend 且不应用纹理变换。
- POM 的 `GetDisplacementObjectScale` 用 `u_model` 行长度近似世界到对象逆缩放；Triplanar 的 Object/View/Tangent 法线输出转换部分为近似或未支持组合。
- Input/Matrix 的行设置使用 `matN` 列主序参数；Transformation Matrix 直接映射 bgfx 全局 uniform，Unity 旧 ModelView/TransposeModelView 仅做 Model 兼容。
- Input/PBR 为 v1 常量/解析式：Dielectric 未做材质资产与能量守恒扩展，Metal Reflectance 只映射 Unity 内置 10 种金属。
- Input/Scene 的 Camera 近远平面来自 `u_global_frustumPlanes[4/5]`，Z Buffer Sign 固定 1；`preview` 未填 frustum planes 时近远平面为 0。Object 的 World Bounds 目前按单位立方体近似，未上传逐对象真实 bounds；Eye Index 无立体渲染时为 0。
- Input/Scene 的 Scene Depth/Difference 在 G-Buffer/Forward 读取 `depthPyramid`，prepass/shadow 使用白色 fallback；Scene Color 当前绑定黑色 fallback，透明 pass 的 opaque-color 副本尚未接入；Fog 使用 `u_fog_*` 的上一帧高度雾近似。Scene 系统纹理占用 graph stage 0-4 的空闲槽，5 纹理属性 + Scene 节点的图会被拒绝；未做 golden。
- Input/Lighting 的 Baked GI 仅生成 SH9 fallback，未接 lightmap SH 纹理，`applyScaling` 暂不生效；Ambient 为 `u_pass_ambientColor` 与 SH9 的近似组合；Reflection Probe 的 LOD 映射为 perceptual roughness，对象空间输入使用 model 矩阵近似。
- Input/Gradient 的 Gradient key 将颜色与 alpha 合并为同一组 time（Unity 为独立 color/alpha keys）；`Sample Gradient` 当前固定 v1 语义；Property 节点已支持 Gradient 属性默认值的编译期烘焙，Blackboard Gradient 属性的运行期 LUT/材质覆盖尚未实现。
- Input/Basic 的 Vector 2/3/4 已提供 X/Y/Z/W 可连接 Float 输入，未连接分量从 `value` setting 继承；Color 未实现 gamma/HDR 转换，Time 的 Smooth Delta 暂等于 Delta。
- Input/Property 已支持 Boolean/Float/Integer/Vector 2/3/4/Color/Matrix 2/3/4、Gradient 默认值与 Texture2D/Texture2DArray/Texture3D/Cubemap/SamplerState/VirtualTexture 输出；纹理属性可连接对应 Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 的 Texture 输入并沿用属性 sampler 设置；SamplerState 属性可连接 Sampler/HeightmapSampler 并覆盖目标纹理 sampler；Texture2D 属性 `defaultType` 支持 white/black/grey/bump/linearGrey/red，写入 `.sgb` reserved byte，运行期无纹理覆盖时绑定对应 1x1 fallback；VirtualTexture 属性按 `vtLayers` 展开隐式 Texture2D layer，并可用 `vtLayerRefNames`/`vtLayerTextureTypes` 自定义层引用与法线类型后连接 Sample Virtual Texture；Gradient 运行期材质覆盖/LUT 未接。
- Utility/Keyword 目前按 `VariantPolicy::Used` 生成单个默认变体，使用 `#ifdef`/`#if defined` 选择表达式；`DynamicBranch` 的运行时 uniform 分支、Keyword 枚举超过 8 个 entry 的槽位以及全局 keyword 尚未支持。
- Utility/Switch 的 cases 通过 `cases` 字符串保存，尚未接入 Unity 的 `List<EntryCase>` JSON 序列化；已支持 Unity 的 upstream Float Enum Property 自动模式：Predicate 直连 `Property`（`floatMode=enum`）时用属性 `enumNames/enumValues` 生成 case 标签与等值阈值；case 上限 26，生成函数未加 `[branch]` hint；`floorPredicate` 仅对 Predicate 生效。
- Utility/Preview 为动态向量直通 v1；Unity `PreviewNode` 的 width/height 编辑器尺寸、`UsePreviewPref` 和节点预览合图未接入，`hasPreview` 仅保留元数据。
- Input/Geometry 的 AbsoluteWorld 暂等同 World；Position Tangent 输出零；View Direction/View Vector 的 Object 空间使用 `u_sg_modelInverse`；UV 仅 UV0/UV1，channel 2-7 回退 UV0，且输出仍为 Vector2（Unity 为 Vector4）；Screen Position 的 Raw/Tiled/Pixel 为基于 `gi.screenPos` 的近似；Vertex ID 在片段通过 flat varying 取 provoking vertex（Unity 仅允许 vertex stage），Instance ID 取 `gl_InstanceID`，当前 graph forward 非实例化绘制时为 0。
- Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap、Texture Size/Gather/Calculate LOD/Normal From Texture 的 Texture/Cube 端口支持 Property/Asset 属性；Texture2D 属性的 Tiling/Offset 已应用于 Sample Texture 2D/LOD 与 Normal From Texture，Split 输出实际 Tiling/Offset 且 Texture Only 恢复恒等；Tiling/Offset 使用 `u_sg_<refName>_ST`，默认值写入 `.sgb`，材质 sidecar 可用 `<refName>_ST` propertyOverride 覆盖；Parallax Mapping/POM/Triplanar 的 Texture 端口支持 Property/Asset，Parallax/POM 应用属性 Tiling/Offset，Triplanar 仍使用 node Tile/Blend 且不应用属性变换；Sample Texture 2D 支持 Standard/LOD/Bias/Gradient（Bias 在 fragment 用 `texture2DBias`，vertex 回退 LOD），2D Array 支持 LOD 与基于导数的 Bias/Gradient 近似，3D 支持 Standard/LOD；`enableGlobalMipBias` 已接：ForwardRenderer 以 `log2(renderScale)` 生成 `u_sg_globalMipBias`，2D 默认打开、2D Array 默认关闭，LOD 模式不加 bias；纹理属性 `isHDR` 时声明 `u_sg_<refName>_HDR`，Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 在采样后按 Unity `AppendHDRDecodeOperation`/`DecodeHDRSample` 条件解码，sidecar `<refName>_HDR` 覆盖 decode instructions；Gather 使用 4 次 LOD0 fallback 而非原生 Gather；Calculate LOD 仅 fragment 可用，clamp 的 mip 上限由尺寸近似。高级采样 2D Array/3D/Cubemap 运行期由 sidecar 图像数组组装并绑定真实 layered texture（array layers / 3D slices / cube 6 faces），缺失或非法回退同维度白色；v1 仅支持同尺寸/同格式/单 mip 的 sidecar 组装，KTX2 多层/立方体容器未直接解析；Sampler State 已支持 filter/wrap/anisotropic（bgfx 非零等级映射为全局 max anisotropy）；虚拟纹理 v1 走 Unity TextureStack fallback，支持 Property 自定义层引用、`default`/`normalTangentSpace`/`normalObjectSpace` 类型，Bias/Derivatives 按 sampleQuality 选择 `texture2DBias`/`texture2DGrad` 或计算 LOD 后 `texture2DLod`（vertex 回退 LOD），addressMode 写入隐式层 sampler；`enableGlobalMipBias` 对 Automatic/Bias/Derivatives 生效（Bias 加 `u_sg_globalMipBias.x`，Derivatives 乘 `u_sg_globalMipBias.y`）；VT `sampleQuality=low` 时 Bias/Derivatives 在 fragment 由 ddx/ddy 或显式导数计算 LOD 后走 `texture2DLod`，`high` 使用 `texture2DBias`/`texture2DGrad`；`noFeedback+Lod Level` 允许 VT 输出进入顶点位移，其他组合在输出连接到 Vertex Position 时报 `SG_TARGET_UNSUPPORTED`；真实 Granite VT stack/streaming/feedback 未接入。
- Normal From Texture 的 Texture 端口支持 Property/Asset 属性，未连接时通过 node setting `texture` 引用；Sampler 端口仍由属性 sampler 驱动，Normal From Height 依赖 ddx/ddy，仅适用于片段阶段。
- Dither 依赖屏幕坐标/屏幕尺寸，按片段阶段使用；Channel Mask 的 `channelMask` 为 bitmask（R=1/G=2/B=4/A=8）。
- Noise 节点使用 Deterministic hash 的位运算与 Legacy hash；Legacy 仅用于兼容旧图，不保证跨平台一致。
- 形状节点依赖 `fwidth`，顶点位移路径会受阶段限制；Noise 无导数依赖，可用于顶点位移。
- Flipbook 的 `fmod`/负数 tile 行为沿用 Unity 源码，未做额外 wrap。
- Refract Intensity 不是完整 Unity `F_Transm_Schlick`/`F_FresnelDielectric` 实现。
- `Flip` 按 Unity 的 `(Flip * -2 + 1) * In` 逐通道取反，Red/Green/Blue/Alpha 默认关闭；超出输入宽度的通道禁用；`Swizzle` 解析 `order`/`rgba` 别名，按 mask 长度驱动输出宽度、按最大通道驱动输入宽度，非法 mask 报 `SG_INVALID_VALUE` 并输出零。
- `Comparison` 支持 6 种 operator；`Random Range` 使用 sin hash 近似，不是完整 Unity hash；`Inverse Lerp` 未做零区间保护。
