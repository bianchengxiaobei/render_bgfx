# Shader Graph Target 设计

> 关联：[architecture.md](architecture.md)、[codegen.md](codegen.md)。
> 目标：用一套 `TargetDesc/SubTargetDesc` 描述所有目标；v1 先落地 Lit/Unlit，再按 SG-7 扩展 Fullscreen/Canvas/CRT。
> 对齐原则：保留 Unity 的 Target/SubTarget 分层与 Block/Pass/Variant 语义，但不复刻 ShaderLab 模板。

---

## 1. Target 框架

```cpp
struct PassDesc {
    std::string id;                  // "forward", "gbuffer", ...
    std::string vsTemplate, fsTemplate;
    std::vector<BlockId> surfaceBlocks;   // 该 pass 需要的 surface 字段
    std::vector<BlockId> vertexBlocks;
    std::vector<std::string> defines;     // 基础宏
    StageMap stageMap;                    // 语义 → stage
    RenderStateDesc renderState;
    VariantDims variants;                 // 该 pass 参与哪些变体
    bool useInPreview = false;
};

struct TargetDesc {
    std::string id;                  // "lit"/"unlit"/"fullscreen"/"canvas"/"crt"
    std::string displayName;
    std::vector<PassDesc> passes;
    std::vector<BlockDesc> surfaceBlocks;   // 全部可用 block（含默认值）
    std::vector<BlockDesc> vertexBlocks;
    SamplerBudget samplerBudget;
    UniformBudget uniformBudget;
    VariantDims variants;
    std::vector<std::string> requiredInputs;
    std::vector<std::string> optionalInputs;
    bool supportsInstancing = false;
    bool supportsTransparent = true;
    bool supportsAlphaClip = true;
    bool supportsDoubleSided = true;
};
```

- Target 以 C++ 静态注册（`TargetRegistry`），同时导出 JSON 元数据给 Web（`/api/targets`）。
- `SubTarget` 在 v1 简化为 Target 内的 settings 预设（Lit/Unlit 各一个）；后续需要材质变体（如 Decal）时再引入真正的 SubTarget。
- Block 定义（对齐 Unity `Editor/Generation/TargetResources/BlockFields.cs:9/21`）：`VertexDescription.Position/Normal/Tangent`、`SurfaceDescription.BaseColor/Metallic/Smoothness/NormalOS/NormalTS/NormalWS/Occlusion/Emission/Alpha/AlphaClipThreshold`。
- 每个 block 有：id、显示名、类型、默认值、允许的精度、是否可连接、在哪些 pass 活跃。
- 未连接的 block 使用默认值；默认值必须与 Unity 对应 Target 一致（逐项记录在 checklist）。

能力矩阵：

| Target id | 状态 | 渲染依赖 | 说明 |
|---|---|---|---|
| `lit` | SG-1..SG-3 | 现有 `SurfaceData` + forward/gbuffer | 主目标，v1 必须完整 |
| `unlit` | SG-3 | 新增 unlit forward/depth shader | 现有 `MaterialType::Unlit` 只影响 pass mask，没有真正 unlit 程序 |
| `fullscreen` | SG-7 | PostProcessChain + 全屏 pass | 自定义全屏材质 |
| `canvas` | SG-7 | 新增 2D quad/sprite 绘制 | 不是 Unity UGUI/UITK 兼容运行时 |
| `crt` | SG-7 | 新增离屏 RT 子系统 | Custom Render Texture 等价物 |
| `preview` | SG-2 | 离屏 FBO | 内部使用，不写入用户资产 |
| `terrain` | 不做 | 无地形系统 | 写入 Known limits |
| `uitk` | 不做 | Unity 运行时专属 | 写入 Known limits |
| `vfx` | 延后 | 现有 VFX shading pack | 价值/成本比低，见 §7 |

## 2. Lit Target

### 2.1 Surface Blocks

| Block | 类型 | 默认 | 映射 |
|---|---|---|---|
| Base Color | color | (1,1,1,1) | `SurfaceData.baseColor/alpha` |
| Metallic | float | 0 | `SurfaceData.metallic` |
| Smoothness | float | 0.5 | `SurfaceData.perceptualRoughness = 1 - s` |
| Normal (Tangent) | vector3 | (0,0,1) | TBN → `normalWS` |
| Normal (Object) | vector3 | (0,0,1) | `ObjectToWorld` → `normalWS` |
| Normal (World) | vector3 | (0,0,1) | 直接使用 |
| Occlusion | float | 1 | `SurfaceData.ambientOcclusion` |
| Emission | color | (0,0,0,1) | `SurfaceData.emissive` |
| Alpha | float | 1 | `SurfaceData.alpha` |
| Alpha Clip Threshold | float | 0.5 | `ALPHA_CLIP` 变体消费 |

- Normal 三选一由 target setting `normalSpace` 决定（对齐 `BuiltInLitSubTarget.cs:97-102`）。
- Alpha/Clip 仅在透明或 alphaClip 时默认激活，但允许 `allowMaterialOverride` 全部显示。

### 2.2 Passes

| Pass | 模板 | 说明 |
|---|---|---|
| forward | `forward_graph.vs/fs.sc` | opaque/transparent；复用 light loop |
| gbuffer | `gbuffer_graph.fs.sc` | deferred；写 5 个 MRT |
| depth | `depth_graph.fs.sc` | prepass；只写深度/alpha clip |
| shadow | `shadow_graph.fs.sc` | shadow caster；alpha clip |
| motion | `motion_graph.vs.sc` | 仅当存在顶点位移时需要 |
| bakedGI | `baked_gi_graph.fs.sc` | 复用现有 baked GI 结构 |
| debug | `debug_graph.fs.sc` | Albedo/Roughness debug |

- 图贴图 stage 0-4（默认 5，按特性可扩到 12），引擎占用 5-15（`architecture.md` §3.3）。
- `DOUBLE_SIDED` 法线翻转由模板统一处理，surface include 不重复。
- 透明材质使用 forward pass + blend state；v1 不进入 instancing。

### 2.3 变体

| 维度 | 位 | 说明 |
|---|---|---|
| AlphaClip | bit0 | `ALPHA_CLIP` |
| DoubleSided | bit1 | `DOUBLE_SIDED` |
| Transparent | bit2 | 渲染状态 + 队列（是否编译变体由 Target 决定） |
| NormalMap/MaskMap | bit3/4 | 若 Target 需要内置材质兼容（v1 可不用） |
| 图关键字 | bit8.. | 最多 8 位 |

- 默认只编译 `Used` 组合；`All` 用于发布。
- 变体上限 64；超限报错。

## 3. Unlit Target

### 3.1 Blocks

| Block | 类型 | 默认 |
|---|---|---|
| Base Color | color | (1,1,1,1) |
| Alpha | float | 1 |
| Alpha Clip Threshold | float | 0.5 |

### 3.2 Passes

| Pass | 模板 | 说明 |
|---|---|---|
| forward | `unlit_graph.vs/fs.sc` | 直接输出 BaseColor * 顶点色（可选） |
| depth | `unlit_depth_graph.fs.sc` | alpha clip |
| shadow | `shadow_graph.fs.sc` | alpha clip |

- 现有 `src/render/material/shader_pass.cpp:44` 对 Unlit 使用 `ForwardOnly/DepthForwardOnly` 掩码，但实际仍绑定 lit forward 程序（`shaders/passes/forward.fs.sc`）。因此必须新增真正的 unlit 程序与 `PassId`，否则无法得到无光照结果。
- Unlit 不参与 GBuffer/Deferred；deferred 管线中走 forward-only 队列。

## 4. Fullscreen Target（SG-7）

- 参考：`Editor/Generation/Targets/Fullscreen/FullscreenSubTarget.cs:99/132/294/513`。
- Blocks：Base Color、Alpha；可选 Eye Depth（写深度时）。
- Pass：单个全屏 pass，输入 UV/ScreenPosition；渲染状态（ZTest/ZWrite/Blend/Stencil）由 target settings 控制。
- 渲染接入：在 `PostProcessChain` 中新增"自定义全屏材质"节点，位置可配置（默认在 post 之前/之后二选一）；不修改现有 pass 顺序。
- 需求：全屏三角形/quad、`u_global_screenSize`、scene color/depth 采样。

## 5. Canvas / UI Target（SG-7）

- 参考：`Editor/Generation/Targets/Canvas/CanvasSubTarget.cs:87/96-98/240`、BuiltIn 的 `BuiltInCanvasSubTarget.cs:23`。
- Blocks：Base Color、Emission、Alpha。
- Pass：UI quad/sprite；alpha blend、无深度写入（或可配置）。
- 渲染接入：`hdrp-bgfx` 目前只有 ImGui 的 `PassId::Ui`（`src/app/debug_ui.cpp:320`）和 `shaders/ui/*`，没有运行时 UI 绘制。需要新增一个最小的 2D quad/sprite 绘制路径与材质绑定。
- 不支持 UGUI/UITK 的组件语义、遮罩、Stencil 兼容；文档必须写明。

## 6. Custom Render Texture Target（SG-7）

- 参考：`Editor/Generation/Targets/CustomRenderTexture/CustomTextureSubTarget.cs:24/32/37-38`。
- Blocks：Base Color、Alpha。
- 需要新增：离屏 RT 资产（尺寸、格式、更新模式 realtime/on-demand）、双缓冲/清屏策略、材质渲染 pass、预览。
- 渲染接入：新增 `CrtSystem`（或 `RenderTextureAsset`）与 RenderGraph pass；不复用 swapchain。
- v1 只支持 2D RT + 单 pass；不支持 3D/array/cubemap CRT。

## 7. 暂不实现的目标与原因

| Target | 原因 | 处理 |
|---|---|---|
| Terrain | `hdrp-bgfx` 无地形系统（全仓库无 terrain 实现） | Target 描述保留，UI 禁用；Known limits |
| UI Toolkit | Unity 运行时专属；无 UITK 渲染器 | 不做；文档说明 |
| VFX | 现有 VFX 使用独立 shading pack（`vfx/include/vfx/shading.h`、`shaders/vfx/*`），Shader Graph 的 VFX Target 需要把图编译成 VFX 输出程序，成本高且价值低 | 延后到独立里程碑评估 |
| Decal / HDRP 专属子目标 | 现有渲染器无 Decal 材质 target | 后续按需 |

原则：UI 显示目标列表时，不支持的 Target 明确标记为"未实现/不支持"，不允许选择后静默降级。

## 8. 与 Unity 的对照

| Unity | 新实现 | 备注 |
|---|---|---|
| `Target`/`SubTarget`（`Editor/Generation/Target.cs:10`、`SubTarget.cs:10`） | `TargetDesc`/`SubTargetDesc` | 去掉 ScriptableObject/反射注册 |
| `BuiltInTarget`/`BuiltInLitSubTarget`（`Editor/Generation/Targets/BuiltIn/.../BuiltInLitSubTarget.cs:14/45`） | `lit` Target | 只对齐 surface 语义，不对齐 Built-in RP |
| `BuiltInUnlitSubTarget.cs:14/27` | `unlit` Target | 新增 unlit 程序 |
| `FullscreenSubTarget.cs:99/132` | `fullscreen` Target | 接 PostProcessChain |
| `CanvasSubTarget.cs:26/87` | `canvas` Target | 新增 2D 绘制 |
| `CustomTextureSubTarget.cs:10/24` | `crt` Target | 新增离屏 RT |
| `UISubTarget.cs:20/74` | 不做 | UITK |
| `Terrain/ITerrainSubTarget.cs` | 不做 | 无地形 |
| `VFXTarget.cs:17/46` | 延后 | 现有 VFX |
| `PreviewTarget.cs:9/29` | `preview` Target | 节点/主预览 |
| `BlockFields.cs:9/21` | Block 定义 | 字段名可不同，语义对齐 |
| `PassDescriptor/SubShaderDescriptor/RenderStateDescriptor`（`Editor/Generation/Descriptors/*`） | `PassDesc/RenderStateDesc` | 输出为 `.sc` 而非 ShaderLab |

## 9. 验收标准

| Target | 必测 | 出口 |
|---|---|---|
| lit | forward + gbuffer + prepass + shadow + motion；D3D11/Vulkan golden | 与手写等价 shader 0 diff 或阈值内 |
| unlit | forward + depth + shadow；透明/alpha clip | 无光照结果正确；双后端 |
| fullscreen | 单 pass；blend/depth/stencil 组合 | 与内置 post pass 对比 |
| canvas | quad/sprite；alpha blend/裁剪 | 截图验证 |
| crt | 单 pass + 更新模式；清屏 | RT 内容截图验证 |
| preview | 球/方片/平面；节点小图 | 12-15 fps 流式预览 |

所有 Target 的 stage/属性/变体预算必须在 `sgbake --check` 中验证；超限必须失败而不是降级。
