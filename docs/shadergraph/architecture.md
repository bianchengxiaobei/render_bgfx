# Shader Graph C++ 复刻架构

> 关联：[README.md](README.md)、[asset-format.md](asset-format.md)、[codegen.md](codegen.md)、[targets.md](targets.md)。
> 目标：在 `hdrp-bgfx` 上提供可独立测试的 C++20 图核心、可离线烘焙的 `.sgb`、以及由原生宿主驱动的 Web 编辑器。
> 原则：核心库不依赖 bgfx/HDRP；渲染适配层不持有图语义；C++ 是唯一权威文档。

---

## 1. 边界

```
shadergraph/                    独立 C++20 库（无 bgfx / hbg:: 头文件）
  include/sg/                   公开 API
  src/core/                     图、类型、校验、具体化、子图
  src/nodes/                    节点定义（spec + 手写动态节点）
  src/compile/                  HLSL 生成、pass/variant 计划、shaderc、.sgb
  src/editor/                   Document、命令、undo、layout sidecar
  tools/sgbake.cpp              CLI

web/                            前端 SPA（React + TS + React Flow）
src/render/shadergraph/         hdrp-bgfx 适配层：材质、program cache、绑定、pass
src/app/sg_editor_host.*        本地 HTTP/SSE/MJPEG 服务与 --sg-editor 模式
shaders/shadergraph/            图材质 pass 模板、varying def、stage 映射
docs/shadergraph/               本规划文档
```

边界规则（对齐 `vfx/` 先例，见 `docs/vfx/architecture.md`）：

- `shadergraph/` 内禁止 `#include <bgfx/...>`、`hbg::*`、`RenderGraph`、`ForwardRenderer`。
- `src/render/shadergraph/` 负责：`.sgb` 加载、program 创建与缓存、属性/贴图绑定、RenderGraph pass 接入、材质 sidecar、热重载。
- `src/app/` 负责：窗口、HTTP 服务、把图文档/编译任务/预览帧串起来；不实现图语义。
- `web/` 只做视图与交互；节点类型、端口合法性、类型推断、诊断全部来自 C++。

## 2. 端到端流水线

```
[编辑器/CLI]
  .sgraph.json  ──parse──▶ sg::Graph
        │                     │
        │                     ├─ validate     节点/槽位/边/关键字/属性/Target 兼容性
        │                     ├─ concretize   动态类型、精度、Literal、活跃节点集
        │                     ├─ plan         属性 CB、关键字变体、pass 列表、stage 预算
        │                     ├─ emit         HLSL（函数注册表 + Surface/Vertex 体）
        │                     └─ compile      shaderc → dxbc(s_5_0) + spirv
        │                                     │
        └──────────────▶ .sgb（确定性二进制 + 内容哈希 + 诊断映射）

[运行时]
  .sgb ──load──▶ GraphMaterialSystem
                    ├─ 属性表 → 运行期 uniform 句柄
                    ├─ pass/variant 表 → ResourceManager::createProgram(blob)
                    └─ GraphMaterialInstance → ForwardRenderer（forward/gbuffer/…）
```

对应 Unity 的流水线：

| Unity 阶段 | Unity 入口 | 新实现 |
|---|---|---|
| 资产序列化 | `Editor/Serialization/MultiJson.cs`、`JsonObject.cs` | 自有 JSON（`asset-format.md`） |
| 图数据与版本 | `Editor/Data/Graphs/GraphData.cs:28/2998` | `sg::Graph` + `GraphDoc` + schema 版本 |
| 节点/槽位/类型 | `Editor/Data/Nodes/AbstractMaterialNode.cs:16`、`Editor/Data/Graphs/MaterialSlot.cs:14`、`Editor/Data/Nodes/SlotValue.cs:8/32` | `NodeDef/NodeInstance/SlotDesc/ValueType` |
| 具体化 | `Editor/Data/Graphs/GraphConcretization.cs:8` | `sg::compile::concretize()` |
| 属性/关键字收集 | `Editor/Generation/Processors/PropertyCollector.cs:10`、`Editor/Data/Util/KeywordCollector.cs:8` | `PropertyLayout` / `KeywordLayout` |
| Target/SubTarget | `Editor/Generation/Target.cs:10`、`Editor/Generation/SubTarget.cs:10` | `TargetDesc` / `SubTargetDesc` |
| 生成器 | `Editor/Generation/Processors/Generator.cs:35/256` | `sg::compile::Generator` |
| 模板/拼接 | `Editor/Generation/Processors/ShaderSpliceUtil.cs:11/55` | `.sc` 模板 + include 注入（不做 ShaderLab 拼接） |
| 导入 | `Editor/Importers/ShaderGraphImporter.cs:20/173` | `sgbake` + 运行时 `.sgb` |
| 编辑器 | `Editor/Drawing/Views/MaterialGraphView.cs:26`、`MaterialGraphEditWindow.cs:26` | Web SPA + HTTP 协议 |
| 预览 | `Editor/Drawing/PreviewManager.cs:24`、`Editor/Drawing/Inspector/MasterPreviewView.cs:19` | 原生 bgfx 离屏渲染 + MJPEG |
| 节点预览 | `PreviewManager` 的每节点预览 | 单节点临时图 → PreviewTarget → 小图缓存 |

## 3. hdrp-bgfx 集成点

### 3.1 SurfaceData 契约

图材质不重写光照，只替换表面函数：

- `SurfaceData` 定义：`shaders/material/surface_data_types.sc:6`。
- 现有表面函数：`shaders/material/surface_data.sc:15` 的 `hbgMakeSurfaceData(...)`。
- 现有调用点：`shaders/passes/forward.fs.sc:17`、`shaders/passes/gbuffer.fs.sc:17`、`shaders/passes/prepass.fs.sc:3`、`shaders/passes/shadowcaster.fs.sc:7`。

新实现：为图材质生成 `hbgGraphSurface(...)`（或同签名包装），在 `shaders/shadergraph/*.sc` 模板中替换 `hbgMakeSurfaceData`。注意：图模板只包含 `material/surface_data_types.sc`（结构体），**不包含** `material/surface_data.sc`（它声明 stage 0-4 的材质 sampler，会与图贴图冲突）。Lit 映射：

| Shader Graph Block | SurfaceData 字段 |
|---|---|
| Base Color | `baseColor`（`alpha` 来自 Base Color.a） |
| Metallic | `metallic` |
| Smoothness | `perceptualRoughness = 1 - smoothness`，同时 `roughness` |
| Normal (TS/OS/WS) | `normalWS`（TS 走 TBN，OS 走 `mul(ObjectToWorld)`） |
| Ambient Occlusion | `ambientOcclusion` |
| Emission | `emissive` |
| Alpha | `alpha` |
| Alpha Clip Threshold | 由 `ALPHA_CLIP` 变体消费 `u_material_metallicRoughness.w` 的替代值 |

Unlit 映射：Base Color/Alpha/Clip → 新增 Unlit forward/depth 模板；现有引擎没有真正的 Unlit 着色器（`MaterialType::Unlit` 只影响 pass mask，`src/render/material/shader_pass.cpp:44` 仍复用 forward 程序），因此 Unlit 是新增 pass，不是复用。

### 3.2 Pass 矩阵（Lit）

| 新 Pass | 来源模板 | 用途 | 图数据依赖 |
|---|---|---|---|
| `ForwardGraph` | `shaders/shadergraph/forward_graph.fs.sc` + `forward_graph.vs.sc` | forward opaque/transparent | SurfaceData 全量 |
| `GBufferGraph` | `gbuffer_graph.fs.sc` | deferred GBuffer | BaseColor/Metallic/Roughness/Normal/AO/Alpha |
| `DepthGraph` | `depth_graph.fs.sc` | depth prepass | Alpha（clip） |
| `ShadowGraph` | `shadow_graph.fs.sc` | shadow caster | Alpha（clip） |
| `MotionGraph` | `motion_graph.vs.sc` | motion vectors | Vertex Position 偏移（可选） |
| `BakedGIGraph` | `baked_gi_graph.fs.sc` | 烘焙 GI | BaseColor/Normal |
| `DebugAlbedo/RoughnessGraph` | debug 模板 | Debug view | BaseColor/Metallic/Roughness |

注：这些名字是规划名；实际 `PassId` 扩展在 SG-3 落地，禁止在 `shader_key.h` 之外硬编码。

### 3.3 Sampler stage 契约（关键）

现有 forward 的 texture/SRV stage 占用（逐项核对）：

| stage | 用途 | 参考 |
|---|---|---|
| 0-4 | 材质贴图（图模板可复用） | `shaders/material/surface_data.sc:9-13` |
| 5 | light data | `shaders/common/light_data.sc:20` |
| 6 | 方向阴影 | `shaders/lighting/shadows.sc:26` |
| 7 | light list | `shaders/common/light_data.sc:37` |
| 8 | shadow context（Buffer，`register(t8)`） | `shaders/lighting/shadows.sc:664`、`src/render/passes/forward_lighting.h:13` |
| 9 | punctual shadow | `shaders/lighting/shadows.sc:12` |
| 10 | environment | `shaders/lighting/environment.sc:6` |
| 11 | contact shadow | `shaders/lighting/contact_shadow.sc:20` |
| 12 | point/IMS SAT shadow | `shaders/lighting/shadows.sc:33/35` |
| 13 | point punctual / area depth | `shaders/lighting/shadows.sc:38`、`src/render/passes/forward_renderer.cpp:6472` |
| 14 | probe array | `shaders/lighting/environment.sc:7` |
| 15 | cookie atlas | `shaders/common/light_data.sc:24` |

图材质约定（v1，SG-0 必须实测）：

- 图模板不包含 `material/surface_data.sc`，因此 stage 0-4 可复用。
- **默认图贴图预算 = 5**：stage `0,1,2,3,4`；所有 graph pass 使用同一映射，避免绑定漂移。
- 可选扩展（需项目关闭对应引擎特性；`.sgb` 记录 `requiredFeatureMask`，运行时校验）：
  - 关闭 shadow context / point-spot 高级阴影（需新增裁剪开关）→ stage 8 可给图（6 张）；
  - 关闭 probe array / cookie atlas → stage 14/15 可给图（+2）；
  - 关闭 point/area 高级阴影（PCSS/IMS/area）→ stage 12/13 可给图（+2）；
  - 关闭 environment 或 contact shadow 还可再释放 10/11，但不推荐（会改变画面）。
  - 实际上限 12；常见配置 5–10。
- prepass/shadow/motion/bakedGI/debug 的引擎占用不同，但图贴图映射保持 `0-4` 不变；每个 pass 的引擎 stage 表写入 `.sgb`。
- 超预算或运行时特性不满足 → Error，不静默降级。

约束：

- shadow context 在 D3D11 中是 `Buffer<float4> : register(t8)`，占用 t8（与 sampler 同一 SRV 命名空间）；不能与图贴图共用。
- 每个 pass 的 stage 表写入 `.sgb`，运行时按表绑定。
- Fullscreen/Canvas 等引擎占用少的目标可放宽，由 `TargetDesc.samplerBudget` 声明。

### 3.4 属性 uniform 契约

- 每个图属性生成一个 bgfx uniform：`u_sg_<refName>`。
- 类型映射：`float/vector2/vector3/vector4/color/boolean/integer` → `Vec4`（未用分量为 0/1 填充）；`matrix2/3/4` → `Mat4`（零填充）；纹理 → `Sampler`；关键字 → 编译期 define（枚举关键字用变体，不引入运行时分支）。
- 运行期按名字创建句柄（`createUniform`），按属性表上传；这是对仓库"uniform 必须 IDL 生成"规则的显式例外，必须在 `architecture.md` 与测试中固化。
- 预算：默认最多 5 张贴图（`0-4`），按特性可扩到 12；32 个标量/向量属性、4 个矩阵；bake 超限报错。D3D11 每 stage 的常量缓冲数量/大小受限；本方案默认预算远低于上限，bake 时仍校验。
- 精度：属性默认精度由 graph/target 决定；`half/float` 只影响 HLSL 声明，不影响上传类型。

### 3.5 材质与 sidecar

- 现有材质字段：`src/asset/material.h:39`；材质变体：`:17/119`（`AlphaMode`、`MaterialVariant`）。
- 现有 sidecar：`src/asset/scene_loader.cpp:1506` 的 `applySidecar`，`materials` 在 `:1542`。
- 新扩展（不破坏旧字段）：

```json
{
  "materials": {
    "GraphMat": {
      "shader": "graphs/lit_test.sgb",
      "alphaMode": "mask",
      "doubleSided": false,
      "propertyOverrides": { "_BaseColor": [1, 0.5, 0.2, 1], "_Smoothness": 0.6 },
      "textureOverrides": { "_MainTex": "textures/a.png" },
      "keywordOverrides": { "_NORMALMAP": true }
    }
  }
}
```

- `GraphMaterialSystem` 负责把 override 合并到 `.sgb` 默认值，生成 `GraphMaterialInstance`。
- 旧材质路径保持可用；图材质仅在 `shader` 字段存在时启用。

### 3.6 Program 缓存与热重载

- `.sgb` 内含 dxbc/spirv blob；运行时用 `ResourceManager::createProgram` 创建（`src/gfx/resource_manager.h:56`）。
- 参考 VFX 的运行时 blob 编程：`vfx/include/vfx/device.h:37/43`、`src/render/vfx/hbg_vfx_backend.cpp:366`。
- 缓存键：`(assetContentHash, pass, variantKey)`；变体键包含引擎变体（AlphaClip/DoubleSided/Transparent）与图关键字位。
- 热重载：监听 `.sgb` mtime/hash；销毁旧 program/texture，重建实例；失败保留旧版本并记录诊断。
- 回退：`.sgb` 缺失/损坏 → 使用原材质路径并只告警一次。

## 4. 线程模型

```
主线程（bgfx + Document + 运行时）
  ├─ 每帧：处理命令队列 → 更新文档 → 必要时触发编译快照
  ├─ 渲染：ForwardRenderer（含图材质 bind）
  ├─ 预览：离屏 FBO → readTexture → 拷贝到帧槽
  └─ 热重载：加载新 .sgb → 重建 program

HTTP 线程池（cpp-httplib）
  ├─ 请求解析 → 命令/查询 → 投递到主线程（mutex + condvar）
  ├─ SSE 事件写线程
  └─ MJPEG 写线程（消费帧槽）

编译 worker 线程
  ├─ 序列化快照（主线程提供不可变 JSON）
  ├─ 图校验 + HLSL 生成
  ├─ shaderc 子进程（dxbc/spirv）
  └─ 写 .sgb → 通知主线程
```

规则：

- 禁止在 HTTP/编译线程调用任何 bgfx API。
- Document 的变更只在主线程发生；HTTP 命令必须等待主线程确认后才返回。
- 编译期间允许继续编辑；结果带 `revision`，过期结果丢弃或标记 stale。
- 预览读回必须与渲染同线程；JPEG 编码可异步，但帧数据必须深拷贝。

## 5. 数据与确定性

- 节点/槽位/边/属性都有稳定 ID；JSON 中显式写出，禁止依赖数组下标。
- 属性按 `refName` 排序、节点按 `id` 排序、边按 `(fromNode,fromSlot,toNode,toSlot)` 排序后再生成，保证字节级确定性。
- 内容哈希：canonical JSON + 生成器版本 + 模板哈希 + 节点定义版本；不包含时间戳/绝对路径。
- `.sgb` 版本与 schema 版本独立；运行时拒绝未知版本。
- 所有诊断携带 `nodeId`/`slotId`（可空）与 severity（Error/Warning/Info）。

## 6. 测试架构

| 层 | 手段 | 入口 |
|---|---|---|
| 核心 | doctest 单测（图/类型/校验/序列化/子图） | `shadergraph/tests` |
| 编译 | fixture 图 → `.sgb`；`sgbake --check`；双后端编译成功 | CTest |
| 渲染 | 图语料 × forward/deferred × D3D11/Vulkan golden | `tests/shadergraph` |
| 协议 | HTTP 命令/并发/错误码；SSE/MJPEG 冒烟 | C++/脚本 |
| 前端 | 组件/协议 TS 测试；Playwright 冒烟（可选） | `web/tests` |
| 性能 | 60 节点图 bake 时间、预览延迟、编辑帧率 | 基准脚本 |

## 7. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| sampler stage 预算 | 图材质无法绑定足够贴图 | SG-0 先验证；图贴图 0-4（默认 5）契约；超限报错；目标级预算 |
| 动态类型/精度/Literal 对齐 | 节点行为与 Unity 不一致 | 以 Unity 测试用例为验收（`Tests/Editor/DynamicSlotTests.cs` 等）；单独里程碑 |
| 变体爆炸/编译时间 | 保存卡顿 | 预览只编译单变体；bake 按 `used/all`；64 变体上限；并行 shaderc |
| 预览回读阻塞 | 编辑器卡顿 | 限频 10-15 fps；只读小 FBO；可暂停流；失败降级为按需截图 |
| 目标缺口（Terrain/UITK/VFX） | 预期落空 | 在 `targets.md` 明示能力矩阵与 Known limits |
| shaderc/HLSL 限制 | 生成失败 | 只生成 `.sc` 兼容 HLSL；禁止 ShaderLab；CI 双后端编译 |
| 许可 | 法务风险 | 只参考行为重写；引用 `file:line`；不搬运 Unity 代码/模板 |

## 8. 对齐参考

- Unity 图/节点/槽位：`Editor/Data/Graphs/GraphData.cs:28`、`Editor/Data/Nodes/AbstractMaterialNode.cs:16/425/821/1098`、`Editor/Data/Graphs/MaterialSlot.cs:14/314`、`Editor/Data/Graphs/DynamicValueMaterialSlot.cs:14/80`、`Editor/Data/Nodes/SlotValue.cs:8/32`、`Editor/Data/Enumerations/Precision.cs:47`。
- Unity 生成：`Editor/Generation/Processors/Generator.cs:35/256`、`Editor/Generation/Processors/GenerationUtils.cs:907/1081/1131/1287/1306`、`Editor/Generation/Processors/PropertyCollector.cs:10/126`、`Editor/Generation/Processors/ShaderStringBuilder.cs:19/167`、`Editor/Generation/Processors/ActiveFields.cs:64`。
- Unity Target：`Editor/Generation/Target.cs:10`、`Editor/Generation/SubTarget.cs:10`、`Editor/Generation/TargetResources/BlockFields.cs:9/21`、`Editor/Generation/Targets/BuiltIn/Editor/ShaderGraph/Targets/BuiltInLitSubTarget.cs:14/45`。
- Unity 编辑器/预览：`Editor/Drawing/Views/MaterialGraphView.cs:26`、`Editor/Drawing/Views/GraphEditorView.cs:47`、`Editor/Drawing/MaterialGraphEditWindow.cs:26`、`Editor/Drawing/PreviewManager.cs:24`、`Editor/Drawing/Inspector/MasterPreviewView.cs:19`、`Editor/Drawing/SearchWindowProvider.cs:26`。
- hdrp-bgfx 表面/绑定：`shaders/material/surface_data_types.sc:6`、`shaders/material/surface_data.sc:15`、`shaders/passes/forward.fs.sc:17`、`shaders/passes/gbuffer.fs.sc:17`、`src/render/passes/forward_lighting.h:10-18/66`、`src/render/passes/forward_renderer.cpp:6457`。
- hdrp-bgfx 运行时：`src/shader/shader_key.h:8/119/136`、`src/shader/shader_library.cpp:52/1045`、`src/gfx/resource_manager.h:56`、`src/gfx/encoder.h:36-38/49`、`vfx/include/vfx/device.h:37/43`、`src/render/vfx/hbg_vfx_backend.cpp:366`。
