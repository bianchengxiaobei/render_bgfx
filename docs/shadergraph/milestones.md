# Shader Graph 复刻里程碑

> 关联：[README.md](README.md)、[checklists/](checklists/)。
> 原则：每个里程碑必须有可运行的产物与门禁；未过门禁不进入下一阶段；跨阶段功能不提前混入。
> 所有任务在 checklist 中记录 Unity/hdrp-bgfx 的 `file:line` 参考，完成后回填实现位置。

---

## 1. 总览

| 里程碑 | 主题 | 主要产物 | 前置 | 门禁 |
|---|---|---|---|---|
| SG-0 | 技术 spike | 6 节点 Lit 图、surface hook、CB/stage 契约、MJPEG 预览 | 无 | D3D11/Vulkan 出图；网页可见预览 |
| SG-1 | 核心模型 | `sg::Graph`、类型系统、校验、JSON、子图展开 | SG-0 | 核心单测 + 迁移 fixture 通过 |
| SG-2 | 编译器 | `sg::compile`、60 节点、`.sgb`、`sgbake` | SG-1 | fixture 双后端编译；`--check` 确定性 |
| SG-3 | 运行时 | `GraphMaterialSystem`、材质 sidecar、forward/gbuffer、热重载 | SG-2 | 双后端 golden；旧材质 0 回归 |
| SG-4 | 编辑宿主 | Document/命令/undo/layout、HTTP/SSE/MJPEG、编译队列 | SG-1、SG-2 | 协议 fixture；并发/错误码 |
| SG-5 | Web 编辑器 | 画布/Blackboard/Inspector/预览/诊断/保存 | SG-3、SG-4 | 端到端制作 Lit 材质 |
| SG-6 | 全量节点 | 208 个 `[Title]` + 内部节点、节点预览 | SG-5 | 节点清单 100%；每节点测试 |
| SG-7 | 全 Target | Fullscreen/Canvas/CRT；缺口文档化 | SG-6 | 各 Target 截图/回归 |
| SG-8 | 收尾 | 性能、文档、打包、全量回归 | SG-7 | CTest/IDL/gfx-test 全绿 |

## 2. 关键路径

```
SG-0 ──▶ SG-1 ──▶ SG-2 ──▶ SG-3 ──▶ SG-5 ──▶ SG-6 ──▶ SG-7 ──▶ SG-8
                     │        │
                     └──▶ SG-4 ─┘
```

- SG-4 可与 SG-3 并行（依赖 SG-1/2 的文档与编译接口）。
- SG-5 可在 SG-4 协议稳定后先行做 UI 骨架（mock 数据），但验收必须接真实宿主。
- SG-6 的批次 A 可在 SG-2 后并行；批次 B/C 需要 SG-3/SG-5 的验证能力。
- SG-7 依赖 SG-6 的节点覆盖与 SG-3 的渲染扩展点。

## 3. 逐里程碑定义

### SG-0 技术 spike

**状态**：完成（2026-11-23）；实测与已知限制见 [checklists/sg0-spike.md](checklists/sg0-spike.md)。

**目标**：一次性验证最高风险：生成 HLSL 能被 shaderc 接受、图材质能接入现有 forward/gbuffer、CB 与 5 贴图 stage 契约可行、预览回读可流式。

**范围**：

- 6 节点 Lit 图：UV → TilingAndOffset → SampleTexture2D → Multiply → Lit BaseColor。
- 生成 surface include，接入 `shaders/passes/forward.fs.sc` 的等价图模板。
- 3 个属性（color/float/texture）+ 5 个纹理属性（1 张真实 + 4 张 dummy，验证 0-4 预算）。
- 运行时从 `.sgb`/临时 blob 创建 program；手接材质绑定。
- `hbg --sg-editor` 最小 HTTP 页面 + MJPEG 预览（可先不含 React）。
- 记录实测：编译耗时、stage 占用、回读延迟、双后端差异。

**门禁**：

- D3D11/Vulkan 截图 0 diff；与手写 Lit 的独立参考对比留到 SG-3（SG-0 无独立参考）。
- 网页能持续看到预览帧（≥10 fps，512²）。
- `docs/shadergraph/checklists/sg0-spike.md` 回填实测数据与决策修正。

### SG-1 核心模型

**状态**：完成（2026-11-23）；实现与测试回填见 [checklists/sg1-core.md](checklists/sg1-core.md) §7。

**目标**：可独立测试的图数据与类型系统，不依赖 GPU。

**范围**：

- `sg::Graph`、NodeDef/NodeInstance、SlotDesc、Edge、Property、Keyword、Group/Note。
- 动态类型具体化、精度传播、Literal；校验（环、缺失输入、类型不匹配、重名）。
- JSON 读写、`schemaVersion` 迁移、子图展开、稳定排序。
- 单测：移植 Unity `Tests/Editor` 的类型/校验预期。

**门禁**：

- 核心单测全绿；非法 JSON 不崩溃。
- 同一图两次序列化字节一致（不含布局）。
- 迁移 fixture（旧版本 JSON → 当前）通过。

### SG-2 编译器

**状态**：完成（2026-11-23）；实现与测试回填见 [checklists/sg2-compiler.md](checklists/sg2-compiler.md) §10。

**目标**：60 个 A 批节点 + 完整生成/打包管线。

**范围**：

- `sg::compile`：活跃节点、属性/关键字收集、函数注册表、Surface/Vertex 生成、pass/variant 计划。
- A 批节点（83 个）优先完成核心子集（至少 60）；其余在 SG-6。
- `.sgb` 打包、`sgbake` CLI（`--input/--output/--check/--backend/--variants`）。
- 预览 target（单 pass）用于节点预览。

**门禁**：

- 20 个 fixture 图双后端编译通过。
- `sgbake --check` 字节确定性通过。
- 属性/关键字/变体预算超限有稳定错误码。

### SG-3 运行时接入

**目标**：图材质在 hdrp-bgfx 中可用，旧材质无回归。

**范围**：

- `GraphMaterialSystem`：`.sgb` 加载、program cache、属性/贴图绑定、变体选择。
- `.hdrp.json` 材质扩展；`GraphMaterialInstance`。
- Lit：forward + gbuffer + prepass + shadow + motion；Unlit：新增程序。
- 热重载、失败回退、诊断。
- stage 契约验证（v1 不做 remap）：图模板不声明 0-4 材质 sampler；全量 golden 回归。

**门禁**：

- 图材质 D3D11/Vulkan golden；forward/deferred 均可用（已通过，PSNR > 100 dB）。
- 旧材质/场景 golden 0 diff（stage 契约不改变现有输出；全量 ctest 129/129）。
- 热重载与缺失 `.sgb` 回退测试通过（运行中替换成功；损坏保留旧资产）。

**v1 完成状态（2026-11-23）**：`GraphMaterialSystem`、SGB 解析、属性/贴图绑定、sidecar、mtime 热重载、forward/gbuffer/prepass/shadow/unlit 已落地；9 cases / 231 assertions。MotionVectors、透明高级 pass、BakedGI/Lightmap、advanced GBuffer 留到 SG-7；图场景强制 CPU light-list texture 路径。详见 `checklists/sg3-runtime.md` §8。

### SG-4 编辑宿主

**目标**：C++ 权威文档 + 本地 HTTP 服务，可供 CLI/测试/Web 共用。

**core v1 + 主预览 + 节点预览完成状态（2026-11-24）**：`sg::editor::Document`、v1 命令/undo/layout、`SgEditorHost` HTTP/SSE/catalog/compile queue、CLI 接入与安全边界已落地；`preview` 单 pass 编译、FBO/readback/JPEG/PNG、`/api/preview.jpg`、10fps MJPEG、节点预览 202→PNG 已接入；`hbg_tests` 293 cases/33208 assertions。

**范围**：

- `sg::editor::Document`：命令、undo/redo、layout sidecar、脏标记、保存。
- `sg_editor_host`：HTTP/SSE/MJPEG、token、路径沙箱、编译队列、预览队列。
- 协议错误码/并发/revision 语义。
- 协议 fixture 与 C++ 快照测试。

**门禁**：

- 所有 v1 命令有请求/响应 fixture。
- 并发编辑返回 409 且可恢复；路径逃逸被拒绝。
- 预览流断开后停止渲染；编译可取消。

### SG-5 Web 编辑器

**状态**：进行中（v1 骨架 + 主流程，2026-11-24）。`web/` 已接 React Flow、协议客户端、命令队列/SSE、Blackboard/Inspector/Search、主预览 MJPEG、节点预览 202 PNG、诊断定位与打开/保存/undo/redo；Vitest 6/6、Playwright dev/static smoke 通过。组/便签/复制粘贴、i18n、CMake 嵌入与 300 节点性能仍未完成，详见 [checklists/sg5-web.md](checklists/sg5-web.md) §7。

**目标**：能在浏览器完整制作、编译、预览、保存一个 Lit 材质图。

**范围**：

- React Flow 画布、节点/端口/连线、组/便签、多选/复制/删除。
- Blackboard、Inspector、Search、Diagnostics、Master Preview、Toolbar。
- 命令队列、revision、SSE、预览降级、节点预览（可先关闭）。
- 主题/快捷键/本地化（zh-CN 默认）。

**门禁**：

- 端到端脚本：新图 → 属性/纹理 → 连接 → 编译 → 预览 → 保存 → 重开一致。
- 错误定位（类型/缺输入/shaderc）可从日志跳到节点。
- 60 节点图编辑到预览刷新 <500ms；512² 流 ≥10 fps。

### SG-6 全量节点

**状态**：进行中（Input/Basic/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Trig/Vector/Wave/Derivative/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function v1，2026-11-26）。已实现 208 个 Input/Basic/Property/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 节点并接入动态类型/代码生成测试；Input/Texture 高级 12 节点已落地 v1（虚拟纹理按 Unity TextureStack fallback 展开层，支持自定义层引用/法线类型与 LOD/Bias/Derivatives 采样；Sample Texture 2D 已接 Standard/LOD/Bias/Gradient，2D Array 已接 LOD 与导数 Bias/Gradient 近似，3D 已接 Standard/LOD；ForwardRenderer 的 renderScale 已驱动 `u_sg_globalMipBias`），batch C 已完成 target-gated v1，Transform Screen、Property Matrix/Gradient/Texture2D 输出与 Input/Basic Vector 分量输入已补齐，剩余真实 Granite VT streaming/feedback 与 fixture/golden，详见 [checklists/sg6-nodes.md](checklists/sg6-nodes.md) §8。

**目标**：208 个 `[Title]` 节点 + 内部节点（Block/Redirect/SubGraphOutput/Legacy）全部实现并有测试。

**范围**：

- 批次 A/B 剩余状态回填与 fixture/golden；批次 C（UI/Terrain/MeshDeformation）已提供 target-gated preview v1。
- 子图（展开/属性提升/递归检测）、Dropdown（独立 dropdowns、活动 entry 选择、`dropdown.<refName>` 子图实例选择）、Custom Function（HLSL 文本/文件）；Gradient 节点已走代码生成期 key 采样，Blackboard Gradient 属性的运行期 LUT 仍待做。
- 节点预览（异步、按需、缓存）。
- 节点清单状态回填。

**门禁**：

- inventory 覆盖率 100%（`TODO` 清零）。
- 每节点至少 1 个单测/生成断言；代表性节点有双后端 golden。
- 未知类型/旧版本加载不崩溃。

### SG-7 全 Target

**目标**：Fullscreen/Canvas/CustomRT 可用；Terrain/UITK/VFX 明确不支持。

**范围**：

- Fullscreen：PostProcessChain 自定义全屏 pass、blend/depth/stencil。
- Canvas：2D quad/sprite 绘制、alpha blend。
- CRT：离屏 RT 资产、更新模式、预览。
- 能力矩阵与 UI 禁用逻辑；Known limits 文档。
- 每个 Target 的测试矩阵与截图。

**门禁**：

- 各 Target 的验收截图/回归通过。
- 不支持的 Target 无法选择且给出原因。
- 旧 Target 无回归。

### SG-8 收尾

**范围**：

- 性能：bake/预览/编辑；变体缓存；并行编译。
- 文档：architecture/asset/codegen/targets/nodes/protocol/web 全量更新。
- 打包：web 资源嵌入、单文件运行、端口/token 体验。
- 全量回归：CTest、IDL、gfx-test、双后端 golden。
- 迁移：schema 版本、legacy 节点、已知限制清单。

**门禁**：

- `ctest` 全绿；`idl_gen --check` 通过；D3D11/Vulkan gfx-test 通过。
- 关键路径性能达标（见 `web-editor.md` / `architecture.md`）。
- 文档与实现一致；Known limits 完整。

## 4. 工作流与并行

| 工作流 | 负责范围 | 可并行点 |
|---|---|---|
| Core | 图/类型/校验/序列化 | SG-0 后独立 |
| Codegen | HLSL/pass/variant/sgbake | SG-1 接口稳定后 |
| Runtime | 材质/program/pass/热重载 | SG-0 原型后 |
| Host | HTTP/命令/预览 | SG-2 编译接口后 |
| Web | UI/交互/协议客户端 | SG-4 协议稳定后（可先 mock） |
| Nodes | 节点 spec/实现/测试 | SG-2 后分批 |
| Targets | 目标扩展/渲染接入 | SG-6 后 |

协作规则：

- 接口变更先改文档与头文件，再改实现；跨模块变更需同步 checklist。
- 每个里程碑结束打 tag/记录 commit；checklist 回填实现位置。
- 新发现的简化/缺口立即写入对应 `Known limits`，不得口头传递。

## 5. 验收命令（逐步落地）

```powershell
# 核心
cmake --preset vs2022
cmake --build --preset vs2022-debug --target hbg_sg_tests
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "shadergraph"

# 编译/资产
.\build\vs2022\Debug\sgbake.exe --check tests\shadergraph\assets

# 渲染
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\lit_graph.gltf --frames=3 --screenshot=sg_d3d.png
.\build\vs2022\Debug\hbg.exe --backend=vulkan --render-test=tests\shadergraph\lit_graph.gltf --frames=3 --screenshot=sg_vk.png

# 宿主/编辑器
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-editor=tests\shadergraph\assets\lit_test.sgraph.json --sg-port=8777
```

## 6. 里程碑状态表（实现时回填）

| 里程碑 | 状态 | 完成日期 | 备注 |
|---|---|---|---|
| SG-0 | 完成 | 2026-11-23 | D3D11/Vulkan 0 diff |
| SG-1 | 完成 | 2026-11-23 | 17 cases / 666 assertions |
| SG-2 | 完成 | 2026-11-23 | 29 cases / 1335 assertions；27 fixture 双后端 |
| SG-3 | 完成（v1） | 2026-11-23 | GraphMaterialSystem/sidecar/热重载；forward+deferred 双后端；图材质测试 9 cases/231 assertions |
| SG-4 | 完成（core v1 + 主预览 + 节点预览） | 2026-11-24 | Document/命令/undo/layout；HTTP/SSE/catalog/compile jobs；preview 单 pass + FBO/readback/JPEG/PNG/MJPEG；节点预览异步编译/缓存 |
| SG-5 | 进行中（v1 骨架 + 主流程） | 2026-11-24 | React Flow 画布/Blackboard/Inspector/Search/Preview/Diagnostics/Toolbar；Vitest 6/6、Playwright smoke；组/便签/i18n/嵌入/性能待做 |
| SG-6 | 进行中（Input/Basic/Property/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 节点 v1） | 2026-11-26 | 208 个节点；hbg_sg_tests 174/8124；hbg_tests 325/33719；inventory 208 TEST/0 TODO；Texture2D defaultType fallback 已接；Texture mip sampling Standard/LOD/Bias/Gradient、global mip bias 与 HDR decode 已接；Sampler State 支持 filter/wrap/anisotropic（bgfx 全局 max anisotropy）；VirtualTexture fallback 的 sampleQuality low/high 与 noFeedback+LOD 顶点阶段约束已接；SG-6C target-gated 完成 |
| SG-7 | 未开始 | - | - |
| SG-8 | 未开始 | - | - |
