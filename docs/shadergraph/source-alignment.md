# 源码对齐清单（Unity / hdrp-bgfx）

> 用途：把每个新模块映射到原实现入口，作为实现与 review 的核对表。
> 规则：只对齐**行为与数据语义**；不复制 Unity C#/HLSL；每个任务在 checklist 记录实际参考的 `file:line`。
> 参考根：Unity `E:\Learn\Graphics\Packages\com.unity.shadergraph`（17.7.0）；渲染 `E:\Learn\hdrp-bgfx`。

---

## 1. Unity → 新实现

| 新模块 | Unity 参考（包根） | 对齐内容 | 不对齐 |
|---|---|---|---|
| `sg::Graph` | `Editor/Data/Graphs/GraphData.cs:28/759/2056/2998` | 属性/关键字/节点/边/分类/组/便签的容器与生命周期 | `MultiJson`、ScriptableObject、GUID 资产引用 |
| 节点基类 | `Editor/Data/Nodes/AbstractMaterialNode.cs:16/75/425/431/821/887/1098` | 槽位、脏标记、取值、变量名、校验、反序列化后更新 | C# 反射、`JsonObject`、Preview 回调 |
| 槽位 | `Editor/Data/Graphs/MaterialSlot.cs:14/314`、`DynamicValueMaterialSlot.cs:14/80` | 类型/方向/默认值/连接测试/动态具体类型 | Unity 序列化字段 |
| 类型系统 | `Editor/Data/Nodes/SlotValue.cs:8/32`、`Editor/Data/Enumerations/Precision.cs:23/47` | `SlotValueType`/`ConcreteSlotValueType`/精度流 | Unity 类型转换 API 的具体实现 |
| 具体化 | `Editor/Data/Graphs/GraphConcretization.cs:8`、`Editor/Data/Implementation/NodeUtils.cs:22/494` | 动态类型、精度、字面量、矩阵/向量转换语义 | 依赖 `GraphData` 内部缓存的结构 |
| 校验 | `Editor/Data/Graphs/GraphValidation.cs`、`GraphData.cs:2056` | 节点/槽位/边/子图/Target 校验 | Unity 的日志/消息 UI |
| 节点注册 | `Editor/Data/Nodes/NodeClassCache.cs`、`TitleAttribute.cs` | 标题/分类/同义词/搜索元数据 | C# 反射扫描 |
| 函数注册/生成 | `Editor/Generation/Processors/Generator.cs:35/256/415/576`、`GenerationUtils.cs:907/1081/1131/1287/1306`、`ShaderStringBuilder.cs:19/167` | 活跃节点、属性块、Surface/Vertex 函数、字符串构建 | ShaderLab/Pass 模板拼接 |
| 属性/关键字 | `Editor/Generation/Processors/PropertyCollector.cs:10/126`、`Editor/Data/Util/KeywordCollector.cs:8`、`KeywordUtil.cs:178/196/201` | 收集/排序/声明/排列/上限 | Unity 的 `PropertyType` ↔ 材质 Inspector |
| Target/SubTarget | `Editor/Generation/Target.cs:10`、`SubTarget.cs:10` | pass/block/field/variant/render state 描述 | ScriptableObject 注册、SRP 资产 |
| Lit/Unlit | `Editor/Generation/Targets/BuiltIn/.../BuiltInLitSubTarget.cs:14/45/97`、`BuiltInUnlitSubTarget.cs:14/27` | Surface blocks/默认值/激活条件 | Built-in RP 的 ShaderLab 细节 |
| Fullscreen/Canvas/CRT/UITK | `FullscreenSubTarget.cs:99/132`、`CanvasSubTarget.cs:26/87/96`、`CustomTextureSubTarget.cs:10/24/37`、`UISubTarget.cs:20/74/85` | Blocks/pass/render state/目标设置 | UGUI/UITK 运行时语义 |
| Block 字段 | `Editor/Generation/TargetResources/BlockFields.cs:9/21`、`StructFields.cs:6/42/96/200` | 顶点/表面字段与结构 | Unity 的宏/模板生成细节 |
| 导入/错误着色器 | `Editor/Importers/ShaderGraphImporter.cs:20/173` | 生成失败时的可读错误/占位思路 | ScriptedImporter/AssetDatabase |
| 编辑器画布 | `Editor/Drawing/Views/MaterialGraphView.cs:26`、`MaterialNodeView.cs:18`、`GraphEditorView.cs:47` | 节点/端口/连线/选择/组/便签交互 | GraphView/UIElements 实现 |
| Blackboard | `Editor/Drawing/Blackboard/SGBlackboard.cs:34` | 属性/关键字/分类的增删改/拖拽 | UI Toolkit 布局 |
| Inspector | `Editor/Drawing/Inspector/InspectorView.cs:13` | 节点/属性/Target 编辑 | PropertyDrawer |
| 搜索 | `Editor/Drawing/SearchWindowProvider.cs:26` | 分类/同义词/键盘交互 | Searcher 包 |
| 预览 | `Editor/Drawing/PreviewManager.cs:24`、`MasterPreviewView.cs:19` | 主预览/节点预览/材质参数 | PreviewRenderUtility/Unity 场景 |
| 节点测试 | `Tests/Editor/DynamicSlotTests.cs`、`LiteralFloatTests.cs`、`MaterialNodeTests.cs`、`PropertyChunkTests.cs`、`ShaderGeneratorTests.cs` | 行为预期 | NUnit 运行时 |

## 2. hdrp-bgfx → 适配层

| 新模块 | hdrp-bgfx 参考 | 对齐内容 |
|---|---|---|
| 表面函数 | `shaders/material/surface_data_types.sc:6`、`shaders/material/surface_data.sc:15` | `SurfaceData` 字段与现有光照输入 |
| Pass 调用 | `shaders/passes/forward.fs.sc:17`、`gbuffer.fs.sc:17`、`prepass.fs.sc:3`、`shadowcaster.fs.sc:7` | 替换点与 alpha/depth 语义 |
| Stage | `src/render/passes/forward_lighting.h:10-18/66`、`shaders/common/light_data.sc:20/37`、`shaders/lighting/shadows.sc:12/26` | 现有绑定槽与 stage 契约 |
| 材质 | `src/asset/material.h:39`、`src/render/material/material_system.h:25/42`、`material_system.cpp:168` | 属性/贴图/变体/采样器 |
| 程序/清单 | `src/shader/shader_key.h:8/119/136`、`src/shader/shader_library.cpp:52/1045` | pass/variant/program cache/manifest |
| 运行时 blob | `vfx/include/vfx/device.h:37/43`、`src/render/vfx/hbg_vfx_backend.cpp:366` | 从 dxbc/spirv 创建 program |
| 编码器 | `src/gfx/encoder.h:36-38/49`、`src/gfx/resource_manager.h:56` | uniform/texture/storage buffer/提交 |
| RenderGraph | `src/render/graph/render_graph.h:104/121/122` | 自定义 pass 接入 |
| 状态 | `src/render/passes/forward_renderer.cpp:149/164/200` | forward/bakedGI/shadow 状态 |
| 材质 sidecar | `src/asset/scene_loader.cpp:1506/1542` | `materials` 扩展 |
| 后处理 | `src/render/postprocess/post_process_chain.h:71/86/353` | Fullscreen 自定义 pass 插入 |
| 编辑器先例 | `vfx/include/vfx/editor.h:60`、`vfx/src/editor/document.cpp:563/795/2061/2221` | Document/命令/undo/layout |
| 编译/CLI 先例 | `vfx/src/compiler/compiler.cpp:201/296/314`、`vfx/tools/vfxbake.cpp:27/240`、`tools/shader_build.py:49/55/74` | shaderc/参数/缓存/CLI |
| 测试/流程 | `docs/vfx/README.md`、`docs/vfx/architecture.md`、`AGENTS.md:3/18/54` | checklist/验收/代码约定 |

## 2.1 SG-1 实现测试回填

| 新模块 | 实现/测试 | 覆盖 |
|---|---|---|
| 动态槽 | `shadergraph/src/core/concretize.cpp`、`tests/core/test_model.cpp` | Unity `DynamicSlotTests` 的 V1..V4/Boolean 36 组合；动态矩阵 |
| 固定槽/类型 | `shadergraph/src/core/validate.cpp`、`tests/core/test_model.cpp` | Unity `FixedSlotTests` 的数值/布尔/纹理兼容 |
| 精度 | `shadergraph/src/core/concretize.cpp`、`tests/core/test_model.cpp` | Inherit/Graph/Single/Half 回退 |
| Literal | `shadergraph/src/core/validate.cpp`、`tests/core/test_model.cpp` | Literal 值类型 + literal source 检查；跨 Redirect/Property 传播为已知限制 |
| 序列化 | `shadergraph/src/core/serialize.cpp`、`tests/core/test_validate_serialize.cpp` | canonical round-trip、layout 与内容哈希、v0 迁移、typeVersion 回调、非法 JSON |
| 子图 | `shadergraph/src/core/subgraph.cpp`、`tests/core/test_subgraph.cpp`、`tests/compiler/test_utility_subgraph_node.cpp` | 属性命名空间、输出重连、外部输入重连、嵌套、递归/冲突；`Sub-graph` 类型与多输入/输出占位槽映射 |
| 注册表 | `shadergraph/src/core/registry.cpp`、`tests/core/test_validate_serialize.cpp` | JSON spec、重复类型、unknown fallback |

## 2.2 SG-2 编译器回填

| 新模块 | 实现/测试 | 覆盖 |
|---|---|---|
| 节点注册/代码生成 | `shadergraph/src/nodes/node_library.cpp`、`shadergraph/tests/compiler/test_compiler.cpp` | SG-2 104 + SG-6 Input/Basic/Property/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 208 canonical NodeDef + 归一化别名；槽位 id/输出/动态数学/纹理/Block |
| Channel / Core Math / Logic | `Editor/Data/Nodes/Channel/*.cs`、`Editor/Data/Nodes/Math/{Advanced,Basic,Interpolation,Range,Round}/*.cs`、`Utility/Logic/*.cs` | `shadergraph/src/nodes/node_library.cpp`、`tests/compiler/test_core_math_logic_nodes.cpp` | 50 节点单图 codegen；Comparison 6 operator；Swizzle mask 驱动输入/输出宽度；Flip 逐通道取反 |
| Input/Basic | `Editor/Data/Nodes/Input/Basic/{Boolean,Color,Constant,Vector1,Integer,Slider,Time,Vector2,Vector3,Vector4}Node.cs` | `shadergraph/src/nodes/node_library.cpp`、`tests/compiler/test_input_basic_nodes.cpp` | 10 节点值 setting；Vector 2/3/4 的 X/Y/Z/W 可连接 Float 输入；Constant 5 常量；Time 5 输出 |
| Input/Property | `Editor/Data/Nodes/Input/PropertyNode.cs:14/80/154/260/288` | `shadergraph/src/compile/generator.cpp`、`shadergraph/src/nodes/node_library.cpp`、`shadergraph/tests/compiler/test_input_property_node.cpp` | 1 节点；property/propertyRef/refName 归一化；输出类型/显示名/精度随 graph property；值/Matrix2/3/4/Gradient/Texture2D/Texture2DArray/Texture3D/Cubemap/SamplerState/VirtualTexture 类型 v1；Gradient 默认 key 编译期烘焙；纹理属性接入对应 Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 的 Texture/Cube 输入；Texture2D 属性 defaultType 写入 `.sgb` 并由 GraphBinding 选择 1x1 fallback；SamplerState 属性接入 Sampler/HeightmapSampler；VirtualTexture 属性按 vtLayers 展开 layer fallback，并支持 vtLayerRefNames/vtLayerTextureTypes |
| Utility/Keyword | `Editor/Data/Nodes/Utility/KeywordNode.cs:14/91/137/168` | `shadergraph/src/nodes/node_library.cpp`、`shadergraph/src/compile/emit.cpp`、`shadergraph/tests/compiler/test_keyword_node.cpp` | 1 节点；Boolean On/Off / Enum entry 动态槽；`#ifdef`/`#if defined` 选择；keyword/property 引用别名 |
| Utility/Dropdown | `Editor/Data/Nodes/Utility/DropdownNode.cs:13/44/85/120/169`、`Editor/Data/Graphs/ShaderDropdown.cs:10/26/43`、`Editor/Data/Nodes/Utility/SubGraphNode.cs:287/980` | `shadergraph/include/sg/graph.h`、`shadergraph/src/core/serialize.cpp`、`shadergraph/src/core/subgraph.cpp`、`shadergraph/src/nodes/node_library.cpp`、`shadergraph/src/compile/emit.cpp`、`shadergraph/tests/compiler/test_dropdown_node.cpp` | 独立 dropdowns；entry 动态槽；活动 entry 选择；越界 clamp；子图定义提升/重映射；`dropdown.<refName>` 子图实例选择；展开期特化而非运行时 if 链 |
| Utility/Switch | `Editor/Data/Nodes/Utility/Logic/SwitchNode.cs:12/22/91/130/160/203` | `shadergraph/src/nodes/node_library.cpp`、`shadergraph/src/nodes/switch_cases.h`、`shadergraph/src/compile/emit.cpp`、`shadergraph/tests/compiler/test_utility_switch_preview_nodes.cpp` | Predicate/Fallback/Out + 最多 26 case；`cases` 字符串编码；手工模式槽位 A-Z；floorPredicate；生成比较函数；upstream Float Enum Property 自动模式 |
| Utility/Preview | `Editor/Data/Nodes/Utility/PreviewNode.cs:6/66/75` | `shadergraph/src/nodes/node_library.cpp`、`shadergraph/tests/compiler/test_utility_switch_preview_nodes.cpp` | 动态 In→Out 直通；hasPreview；尺寸编辑元数据待 Web 端接入 |
| Utility/Custom Function | `Editor/Data/Nodes/Utility/CustomFunctionNode.cs:16/75/160/196/232` | `shadergraph/src/nodes/custom_function.h`、`shadergraph/src/nodes/node_library.cpp`、`shadergraph/src/compile/emit.cpp`、`shadergraph/tests/compiler/test_utility_custom_function_node.cpp` | 字符串函数体/file include；inputSlots/outputSlots 动态槽；槽位名/函数名校验；纹理槽位与 include 白名单待补 |
| Input/Geometry | `Editor/Data/Nodes/Input/Geometry/{Position,NormalVector,TangentVector,BitangentVector,ViewDirection,ViewVector,ScreenPosition,VertexColor,VertexID,InstanceID,UV}Node.cs`、`Editor/Data/Graphs/ShaderGraphRequirements.cs:276/297`、`Editor/Generation/Processors/GenerationUtils.cs:1015/1020/1070/1075`；bgfx `tools/shaderc/shaderc.cpp:2134-2259` | `shadergraph/src/nodes/node_library.cpp`、`shaders/shadergraph/{forward_graph.vs.sc,forward_graph.fs.sc,varying_graph.def.sc}`、`tests/compiler/test_input_geometry_nodes.cpp` | 10 节点空间/屏幕模式；UV channel；Vertex ID/Instance ID 从 `gl_VertexID`/`gl_InstanceID` 经 flat varying 进片段 |
| Input/Texture | `Editor/Data/Nodes/Input/Texture/{SampleTexture2D,SampleTexture2DLOD,SampleTexture2DArray,SampleTexture3D,SampleRawCubemap,SampleCubemap,SamplerState,TexelSize,GatherTexture2D,CalculateLevelOfDetailTexture2D,Texture2DAsset,Texture2DArrayAsset,Texture3DAsset,CubemapAsset,ProceduralVirtualTexture,SampleVirtualTexture}Node.cs`、`Editor/Data/Util/MipSamplingModes.cs:47/66/103`、`Utility/SplitTextureTransformNode.cs:20/34`、`Editor/Data/Graphs/Texture2DShaderProperty.cs:88/95/121`、`Editor/Data/Graphs/TextureSamplerState.cs:10/45/50`；`com.unity.render-pipelines.core/ShaderLibrary/Texture.hlsl:25/35/67`、`VirtualTexturing.hlsl:20/37/59/107/194`、`TextureStack.hlsl:327/377/403/460`、`ShaderGraphLibrary/Functions.hlsl:87` | `shadergraph/src/nodes/node_library.cpp`、`tests/compiler/test_input_texture_nodes.cpp`、`src/render/shadergraph/graph_binding.cpp`、`src/render/shadergraph/graph_material_system.cpp`、`src/render/passes/forward_renderer.cpp` | 17 节点采样/尺寸/LOD/资产/Sampler/Split/VT；2D 法线解包；Sample Texture 2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 采用 Unity 槽位并支持 Property/Asset 纹理输入；Sample Texture 2D 支持 Standard/LOD/Bias/Gradient，2D Array 支持 LOD 与导数 Bias/Gradient 近似，3D 支持 Standard/LOD；`enableGlobalMipBias` 通过 `u_sg_globalMipBias=(log2(renderScale), 2^bias)` 对齐 Unity platform macro（2D 默认 true、2D Array 默认 false，LOD 不加 bias）；Texture2D 属性 Tiling/Offset 通过 `_ST` uniform 驱动 Sample Texture 2D/LOD、Normal From Texture 与 Split Texture Transform，运行期支持 sidecar `_ST` 覆盖；Texture2D 属性 `defaultType` 写入 `.sgb`，无纹理覆盖时绑定对应 1x1 fallback；Texture Size/Gather/Calculate LOD 采用 Unity 槽位并支持 Property/Asset 纹理输入；高级采样使用对应 SAMPLER 声明，运行期由 sidecar 图像数组组装 2D Array/3D/Cubemap 并绑定同维度 fallback；Sampler State 覆盖 filter/wrap/anisotropic；isHDR 纹理属性声明 `u_sg_<refName>_HDR`，2D/LOD/2D Array/3D/Cubemap/Reflected Cubemap 采样后按 Unity `AppendHDRDecodeOperation`/`DecodeHDRSample` 条件解码，sidecar `<refName>_HDR` 覆盖；VirtualTexture 属性/PVT 按 Unity TextureStack fallback 展开，支持自定义层引用、`default`/`normalTangentSpace`/`normalObjectSpace` 类型及 LOD/Bias/Derivatives 采样；`sampleQuality` low/high 按 Granite 采样路径近似，`noFeedback+LOD` 允许 VT 输出进入顶点位移，其他组合报 `SG_TARGET_UNSUPPORTED`；真实 Granite streaming/feedback 未接 |
| Input/Matrix | `Editor/Data/Nodes/Input/Matrix/{Matrix2,Matrix3,Matrix4,TransformationMatrix}Node.cs` | `shadergraph/src/nodes/node_library.cpp`、`tests/compiler/test_input_matrix_pbr_nodes.cpp` | 4 节点行设置/变换矩阵；InverseModel 使用 `u_sg_modelInverse` |
| Input/PBR | `Editor/Data/Nodes/Input/PBR/{DielectricSpecular,MetalReflectance}Node.cs` | 同上 | 2 节点介电/金属常量 |
| Input/Scene | `Editor/Data/Nodes/Input/Scene/{Camera,EyeIndex,Fog,Object,SceneColor,SceneDepth,SceneDepthDifference,Screen}Node.cs` | `shadergraph/src/nodes/node_library.cpp`、`tests/compiler/test_input_scene_lighting_nodes.cpp` | 4/8 节点：Camera/EyeIndex/Object/Screen |
| Input/Lighting | `Editor/Data/Nodes/Input/Lighting/{Ambient,BakedGI,MainLightDirection,ReflectionProbe}Node.cs` | 同上 | 4 节点：Ambient/Baked GI/Main Light/Reflection Probe |
| 函数注册 | `shadergraph/src/compile/function_registry.cpp` | 同名去重、冲突错误、首次注册顺序；Random Range helper |
| 属性 | `shadergraph/src/compile/properties.cpp` | refName 排序、纹理 stage、每类预算、`u_sg_<ref>` |
| 关键字 | `shadergraph/src/compile/keywords.cpp` | 引擎/图 bit、boolean/enum 排列、64 cap |
| Surface/模板 | `shadergraph/src/compile/emit.cpp`、`shaders/shadergraph/*.sc` | lit forward/gbuffer/depth/shadow、unlit、preview |
| Pass/stage | `shadergraph/src/compile/targets.cpp`、`pass_plan.cpp` | pass id/模板/stage 唯一性与 SG-0 契约 |
| shaderc | `shadergraph/src/compile/shaderc.cpp` | dxbc/spirv、缓存 key、pass/variant 错误诊断 |
| SGB | `shadergraph/src/compile/pack.cpp` | section 1-9、group/blob offset 回读校验、确定性 |
| CLI | `shadergraph/tools/sgbake.cpp` | `--input/--output/--check/--backend/--variants/--cache` |
| 当前限制 | `checklists/sg2-compiler.md` §10.4 | vertex displacement/varying 裁剪、subgraph library、manifest 等 |

## 2.3 SG-3 运行时回填

| 新模块 | 实现/测试 | 覆盖 |
|---|---|---|
| `.sgb` 解析 | `src/render/shadergraph/sgb_asset.{h,cpp}`、`tests/render/test_graph_material.cpp` | section 1-7、属性/关键字/pass-stage/variant/blob、stage 契约、损坏回退 |
| Program cache | `src/render/shadergraph/graph_program_cache.{h,cpp}` | `(contentHash, pass, variant, backend)`、dxbc/spirv、variant fallback、热重载释放 |
| 属性/贴图绑定 | `src/render/shadergraph/graph_binding.{h,cpp}`、`graph_material_system.cpp` | `u_sg_<ref>`/`s_sg_<ref>`/`_TexelSize`、`u_sg_modelInverse`、matrix/vector、keyword recompute、stage 0-4 |
| 图材质系统 | `src/render/shadergraph/graph_material_system.{h,cpp}` | 资产缓存、fallback 纹理、unlit target、mtime 热重载、损坏保留旧版 |
| 材质 sidecar | `src/asset/material.h`、`src/asset/scene_loader.cpp` | `shader/propertyOverrides/textureOverrides/keywordOverrides`、`alphaMode/doubleSided`、相对路径、`textureOverrides` string/string[] layered 组装 |
| Pass 接入 | `src/render/passes/forward_renderer.cpp`、`src/render/material/shader_pass.cpp` | forward/gbuffer/prepass/shadow 分支；图材质跳过 instancing/GPU-driven/BakedGI/motion |
| 验证 | `tests/shadergraph/lit_graph.*`、`hbg_tests`/ctest | forward/deferred/unlit/alpha-clip/double-sided 双后端；0/近 0 diff |
| 当前限制 | `docs/shadergraph/checklists/sg3-runtime.md` §8.4 | motion、透明高级 pass、GI/lightmap、advanced GBuffer、纹理默认色彩空间等 |

## 2.4 SG-4 编辑宿主回填

| 新模块 | 实现/测试 | 覆盖 |
|---|---|---|
| Document | `shadergraph/include/sg/editor.h`、`shadergraph/src/editor/document.cpp` | 加载/保存/脏标记、layout sidecar、open/save-as、revision |
| 命令/undo | `shadergraph/src/editor/commands.cpp`、`editor_commands.inl` | v1 命令、原子回滚、结构历史、undo/redo |
| Layout | `shadergraph/src/editor/layout.cpp` | `.sgraph.editor.json`/`.ssubgraph.editor.json`、图位置同步、损坏回退 |
| HTTP/Security | `src/app/sg_editor_host.cpp` | 127.0.0.1、token、Origin、8 MiB、路径沙箱、扩展名白名单 |
| 协议 API | `src/app/sg_editor_host.cpp` | session/document/commands/undo/redo/open/save/diagnostics/catalog/assets/SSE |
| Compile queue | `src/app/sg_editor_host.cpp` | 主线程快照、worker、preview/bake、stale、cancel 标志、artifacts |
| Preview | `src/app/sg_preview_renderer.{h,cpp}`、`src/app/sg_editor_host.cpp` | 离屏 FBO/readback/JPEG/PNG、preview target 单 pass、MJPEG、节点预览临时图/异步缓存；参考 `Editor/Drawing/PreviewManager.cs:24`、`Editor/Generation/Targets/PreviewTarget.cs:9` |
| CLI | `src/app/backend.cpp`、`src/app/application.cpp` | `--sg-editor/-root/-web-root/-port/-token`、主线程 poll、关闭 |
| 测试 | `shadergraph/tests/editor/test_document.cpp`、`tests/host/test_sg_editor_host.cpp`、`tools/sg_protocol_smoke.py` | 文档/命令/undo/layout、HTTP 安全/保存、协议冒烟 |
| 当前限制 | `checklists/sg4-host.md` §9 | 纹理默认值/完整 SRP 灯光、子图命令；节点预览前端已接 SG-5 v1 |

## 2.5 SG-5 Web 编辑器回填

| 新模块 | Unity 参考（包根） | 实现/测试 |
|---|---|---|
| 画布/节点/端口 | `Editor/Drawing/Views/MaterialGraphView.cs:26`、`MaterialNodeView.cs:18` | `web/src/graph/GraphCanvas.tsx`、`GraphNode.tsx`；React Flow Handle/Edge |
| Blackboard | `Editor/Drawing/Blackboard/SGBlackboard.cs:34` | `web/src/panels/Blackboard.tsx`；属性/关键字/拖拽/分类展示 |
| Inspector | `Editor/Drawing/Inspector/InspectorView.cs:13` | `web/src/panels/Inspector.tsx`；节点/属性/keyword/Target |
| 搜索 | `Editor/Drawing/SearchWindowProvider.cs:26` | `web/src/panels/SearchPalette.tsx`；Space/键盘/Target 过滤 |
| 主预览/节点预览 | `Editor/Drawing/Inspector/MasterPreviewView.cs:19`、`PreviewManager.cs:24` | `web/src/panels/Preview.tsx`、`graph/NodePreview.tsx`；MJPEG + 202 PNG 轮询 |
| 编辑窗口/命令 | `Editor/Drawing/MaterialGraphEditWindow.cs:26` | `web/src/app/Toolbar.tsx`、`state/editor.ts`；服务端命令/undo/save |
| 协议客户端 | `docs/shadergraph/editor-protocol.md` §2/§12 | `web/src/api/client.ts`；token/超时/重试/错误码/SSE |
| 测试 | `Editor/Drawing/SearchWindowProvider.cs:26` 等交互 | `web/tests/model.test.ts`、`browser-smoke.mjs`；Vitest + Playwright |
| 当前限制 | SG5-12/15 | 组/便签/复制粘贴/i18n/性能/CMake 嵌入未完成 |

## 2.6 SG-6 Math/Matrix/Transform/UV/Procedural/Artistic 节点回填

| 新模块 | Unity 参考（包根） | 实现/测试 | 覆盖 |
|---|---|---|---|
| Trigonometry | `Editor/Data/Nodes/Math/Trigonometry/*Node.cs` | `shadergraph/src/nodes/node_library.cpp`、`tests/compiler/test_math_nodes.cpp` | `DynamicDimensionVector` 宽度传播；`atan2`、角度/弧度、双曲函数生成 |
| Wave | `Editor/Data/Nodes/Math/Wave/*Node.cs` | 同上 | Sawtooth/Triangle/Square/Noise Sine Wave v1；默认 `MinMax` |
| Derivative | `Editor/Data/Nodes/Math/Derivative/*Node.cs` | 同上 | DDX/DDXY/DDY 片段导数表达式 |
| Vector | `Editor/Data/Nodes/Math/Vector/*Node.cs` | 同上 | Dot/Cross/Distance/Fresnel/Projection/Reflection/Rejection/Rotate/Sphere Mask/Refract；标量 `dot/reflect` 特化 |
| Matrix | `Editor/Data/Nodes/Math/Matrix/*Node.cs` | 同上 | Construction/Determinant/Split/Transpose；Matrix Split 输出宽度随维度切换 |
| Transform | `Editor/Data/Nodes/Math/Vector/TransformNode.cs`、`Editor/Data/Util/SpaceTransformUtil.cs`、`Packages/com.unity.render-pipelines.core/ShaderLibrary/{SpaceTransforms,Common}.hlsl` | `shadergraph/src/compile/emit.cpp`、`src/render/shadergraph/graph_material_system.cpp` | Object/World/View/Tangent/AbsoluteWorld/Screen 的 P/D/N 转换；World/View/Tangent->Object 使用 `u_sg_modelInverse`；Screen 通过 `u_global_invViewProj`/`u_global_viewProj` 做 clip-space 往返，UV 沿用 `gi.screenPos` |
| UV | `Editor/Data/Nodes/UV/*Node.cs`、`ShaderLibrary/ParallaxMapping.hlsl`、`PerPixelDisplacement.hlsl` | `shadergraph/src/nodes/node_library.cpp`、`shadergraph/src/compile/emit.cpp` | Tiling/Rotate/Polar/Radial Shear/Spherize/Twirl/Flipbook/Parallax Mapping/POM/Triplanar；Texture 端口支持 Property/Asset；Parallax/POM 应用纹理 Tiling/Offset，Triplanar 使用 node Tile；MeshUV0 fallback；POM 线性搜索+Secant |
| Procedural | `Editor/Data/Nodes/Procedural/CheckerboardNode.cs`、`Procedural/Shape/*Node.cs` | 同上 | Checkerboard/Ellipse/Polygon/Rectangle/Rounded Polygon/Rounded Rectangle；fwidth 抗锯齿 |
| Procedural Noise | `Editor/Data/Nodes/Procedural/Noise/*Node.cs`、`ShaderLibrary/Hashes.hlsl` | 同上 | Simple/Gradient/Voronoi；Deterministic 与 Legacy hash；Out/Cells |
| Artistic Adjustment | `Editor/Data/Nodes/Artistic/Adjustment/*Node.cs` | 同上 | Contrast/Saturation/Hue/White Balance/Replace Color/Invert Colors/Channel Mixer |
| Artistic Normal | `Editor/Data/Nodes/Artistic/Normal/*Node.cs` | 同上 | Normal Blend/Strength/Reconstruct Z/From Height/From Texture/Unpack；Normal From Texture 的 Texture 端口支持 Property/Asset |
| Artistic Blend | `Editor/Data/Nodes/Artistic/Blend/BlendNode.cs` | 同上 | Blend 22 种模式、Opacity |
| Artistic Filter | `Editor/Data/Nodes/Artistic/Filter/*Node.cs` | 同上 | Dither/Fade Transition |
| Artistic Mask | `Editor/Data/Nodes/Artistic/Mask/*Node.cs` | 同上 | Channel Mask/Color Mask |
| Artistic Utility | `Editor/Data/Nodes/Artistic/Utility/ColorspaceConversion.cs` | 同上 | RGB/Linear/HSV 9 种转换 |
| Input/Gradient | `Editor/Data/Nodes/Input/Gradient/{GradientNode,SampleGradientNode,BlackbodyNode}.cs`、`ShaderGraphLibrary/Functions.hlsl` | `shadergraph/src/nodes/node_library.cpp`、`shadergraph/src/compile/emit.cpp`、`tests/compiler/test_input_gradient_nodes.cpp` | Gradient 8 key 常量结构、Sample Gradient v1（blend/fixed/perceptual OkLab）、Blackbody 公式；Web JSON 编辑/渐变预览 |
| UI / Input UI | `Editor/Data/Nodes/UI/*Node.cs`、`Editor/Data/Nodes/Input/UI/*Node.cs` | `shadergraph/src/nodes/node_library.cpp`、`tests/compiler/test_ui_target_nodes.cpp` | 15 节点 target-gated；`preview` 回退；`ui` Target 未实装 |
| Terrain / Mesh Deformation / Custom Interpolator | `Editor/Data/Nodes/Terrain/*.cs`、`Editor/Data/Nodes/MeshDeformation/*.cs`、`Editor/Data/Nodes/Input/CustomInterpolatorNode.cs` | 同上 | 5 节点 target-gated；Terrain/Mesh preview 回退；Instance 固定 vec4 |
| Fresnel 绑定 | `FresnelEffectNode.cs` 的 WorldSpaceNormal/ViewDirection | `shadergraph/src/compile/emit.cpp` | 未连接输入使用 `gi.normalWS`/`gi.viewDirWS` |
| Refract | `RefractNode.cs` | 同上 | Safe/Critical 模式、IOR、近似 Intensity；完整 BSDF Fresnel 未对齐 |
| 状态/测试 | `Tests/Editor/DynamicSlotTests.cs`、`ShaderGeneratorTests.cs` | `docs/shadergraph/node-status.json`、inventory | 208 TEST / 0 TODO；`hbg_sg_tests` 174/8124；`hbg_tests` 325/33719；sgbake dxbc/spirv 主图 8 shaders、Core Math/Logic 8 shaders、Input/Basic 8 shaders、Input/Basic Vector Components 16 shaders、Input/Property 8 shaders、Property Matrix 16 shaders、Property Gradient 16 shaders、Property Texture 16 shaders、Texture Transform 16 shaders、Utility/Keyword 8 shaders、Utility/Dropdown 8 shaders、Utility/Switch 8 shaders、Input/Geometry 8 shaders、Geometry IDs 16 shaders、Input/Texture 5 节点 8 shaders、Input/Texture 高级 4 节点 16 shaders、Sampler State 1 节点 16 shaders、Virtual Texture 2 节点 16 shaders、Virtual Texture Stack 3 层 16 shaders、Texture Mip Sampling 16 shaders、Texture Mip Sampling Array/3D 16 shaders、Input/Matrix+PBR 8 shaders、Input/Scene+Lighting 8 shaders、Input/Scene 4 节点 8 shaders、Input/Gradient 8 shaders、Transform Spaces 8 shaders、Transform Screen 16 shaders、Channels Flip 16 shaders、Channels Swizzle 16 shaders、Switch Property 16 shaders、SG-6C preview 2 shaders；`--check` 48 assets；Dropdown、Sub-graph 与 Custom Function 由专用测试/fixture 覆盖 |

## 3. 明确不对齐

| Unity 机制 | 原因 |
|---|---|
| `MultiJson`/`JsonObject`/`JsonRef` | 自有 JSON + 显式 id；不复刻 Unity 序列化容器 |
| `ScriptedImporter`/`AssetDatabase` | 使用 `sgbake` + `.sgb` + sidecar |
| `ShaderSpliceUtil`/ShaderLab 模板 | bgfx shaderc 只接受 `.sc`，不做包壳/解析 |
| GraphView/UIElements/Searcher | Web 前端 + React Flow |
| `PreviewRenderUtility`/Unity 场景 | bgfx 离屏场景 + MJPEG |
| URP/HDRP/Built-in 多 RP 目标 | 只覆盖 hdrp-bgfx 能支撑的目标 |
| Unity 资产导入 | 自有格式；后续单独里程碑 |

## 4. 维护规则

1. 开始任务前：在对应 checklist 填写本条目的实际参考 `file:line`（不是本文件的概括）。
2. 实现后：回填实现文件/行号；若行为与 Unity 有偏差，写 `Deviation` 与原因。
3. Unity/hdrp-bgfx 升级后：先跑 `sg_node_inventory.py` 与引用检查，再评估受影响条目。
4. 发现文档与源码冲突：先修文档，再改实现；以源码行为为准。
