# Shader Graph C++ / Web 复刻规划索引

> 目标：用 C++20 重写 `com.unity.shadergraph` 17.7.0 的核心能力，编辑器改为 Web（React + React Flow），渲染复用 `hdrp-bgfx`。
> 本文档是全部规划文档的入口；实现按 `checklists/` 逐个里程碑推进。

**参考源码根**

- Unity Shader Graph：`E:\Learn\Graphics\Packages\com.unity.shadergraph`（版本 17.7.0；805 个 `.cs`；`Editor/Data/Nodes` 226 个文件、208 个 `[Title]` 节点）
- 渲染工程：`E:\Learn\hdrp-bgfx`（C++20、bgfx、shaderc、forward/deferred、`.sc` 着色器）
- 本文所有 Unity 路径以包根为基准；hdrp-bgfx 路径以仓库根为基准。

---

## 0. 已确定决策

| 项 | 决策 | 影响 |
|---|---|---|
| 资产格式 | 只用自己的格式，不读 Unity `.shadergraph` | 不复刻 `MultiJson`；后续可另立导入器里程碑 |
| Target 范围 | 尽量覆盖全部 Target | 先建 Target 框架；Terrain/UITK/VFX 等无渲染支撑的目标记为 Known limits |
| Web 前端 | React + TS + React Flow | 节点/控件开发效率优先，接受 DOM 渲染上限 |
| 运行形态 | 原生 `hbg` 宿主 + 浏览器 UI | C++ 权威文档、内置 HTTP/SSE/MJPEG 服务，仅 localhost |
| 预览 | 原生 bgfx 渲染，流式推送到网页 | 编辑防抖编译 + 单 pass 预览；完整 bake 在保存/编译时执行 |
| 节点库 | 全量节点（208 个 `[Title]` + 内部节点） | 分 A/B/C 三批；框架先落地，长尾节点按清单补齐 |
| 代码来源 | 参考行为自行重写 | 不直接搬运 Unity C#/HLSL；引用仅用于语义对齐与 `file:line` 记录 |

> 许可提示：`com.unity.shadergraph` 使用 Unity Companion License。本计划只做行为对齐与独立重写；如后续需要直接复用 Unity HLSL 模板/片段，必须先做法务确认。

## 1. 非目标（v1 明确不做）

- 不读取、不写出 Unity `.shadergraph` / `.shadersubgraph` 资产。
- 不复刻 Unity 的 ShaderLab 模板拼接（`ShaderSpliceUtil`）与 `ScriptedImporter`。
- 不实现 Unity 的 GraphView/UIElements 编辑器；Web 只复刻交互与信息架构。
- 不改写 `hdrp-bgfx` 的渲染管线；只增加图材质的绑定与 pass。
- 不实现 Terrain / UI Toolkit / VFX Target（缺渲染支撑，见 `targets.md`）。
- 不做多用户协同、远程编辑、Undo 历史持久化。

## 2. 文档索引

| 文档 | 内容 |
|---|---|
| [architecture.md](architecture.md) | 模块边界、端到端流水线、Unity↔新实现映射、线程模型、绑定契约、风险 |
| [asset-format.md](asset-format.md) | `.sgraph.json` / `.ssubgraph.json` / `.sgraph.editor.json` / `.sgb` / 材质 sidecar |
| [codegen.md](codegen.md) | 类型系统、具体化、属性/关键字收集、HLSL 生成、pass/variant、shaderc、诊断 |
| [targets.md](targets.md) | Target 框架、Lit/Unlit/Fullscreen/Canvas/CRT/Preview 矩阵、stage 预算、缺口 |
| [nodes.md](nodes.md) | 节点 spec、动态节点、A/B/C 批次、移植流程、测试要求 |
| [nodes-inventory.md](nodes-inventory.md) | 全量节点清单（自动生成，含批次与 Unity 源文件） |
| [node-status.json](node-status.json) | 节点清单状态回填映射（`category/title` → `TODO/IMPL/TEST/GOLDEN`） |
| [editor-protocol.md](editor-protocol.md) | HTTP/SSE/MJPEG API、命令集、revision/诊断、预览参数、安全 |
| [web-editor.md](web-editor.md) | 前端架构、组件划分、状态管理、Unity UX 对照、构建与嵌入 |
| [source-alignment.md](source-alignment.md) | Unity/hdrp-bgfx 逐模块 `file:line` 对齐清单与不对称项 |
| [milestones.md](milestones.md) | SG-0..SG-8 总表、依赖、门禁、执行顺序 |
| [checklists/sg0-spike.md](checklists/sg0-spike.md) | 技术 spike：surface hook、CB/stage、运行时 blob、预览链路 |
| [checklists/sg1-core.md](checklists/sg1-core.md) | 图模型、类型系统、校验、序列化、子图展开 |
| [checklists/sg2-compiler.md](checklists/sg2-compiler.md) | 代码生成、属性/关键字、pass/variant、`sgbake` |
| [checklists/sg3-runtime.md](checklists/sg3-runtime.md) | `GraphMaterialSystem`、材质 sidecar、pass 接入、热重载 |
| [checklists/sg4-host.md](checklists/sg4-host.md) | Document/命令/undo/layout、HTTP/SSE/MJPEG、编译队列 |
| [checklists/sg5-web.md](checklists/sg5-web.md) | React 画布、Blackboard、Inspector、预览、诊断、端到端验收 |
| [checklists/sg6-nodes.md](checklists/sg6-nodes.md) | 全量节点分批移植、特殊节点、节点预览 |
| [checklists/sg7-targets.md](checklists/sg7-targets.md) | Fullscreen / Canvas / CustomRT / 缺口目标 |
| [checklists/sg8-release.md](checklists/sg8-release.md) | 性能、文档、打包、全量回归 |

## 3. 里程碑总览

| 里程碑 | 目标 | 关键出口 | 依赖 |
|---|---|---|---|
| SG-0 | 技术 spike | 一个 6 节点 Lit 图在 D3D11/Vulkan 出图；网页可见 MJPEG 预览 | 无 |
| SG-1 | 核心模型 | 类型推断/具体化/校验/序列化可用，Unity 测试用例对齐 | SG-0 |
| SG-2 | 编译器 | 60 节点可生成 `.sgb`；`sgbake --check` 确定性 | SG-1 |
| SG-3 | 运行时 | 图材质在 forward + gbuffer 可用；sidecar/热重载/双后端 golden | SG-2 |
| SG-4 | 编辑宿主 | Document 命令/undo/layout + HTTP/SSE/catalog + 编译队列 + 主预览/MJPEG + 节点预览 PNG | SG-2 |
| SG-5 | Web 编辑器 | 能完整制作并保存一个 Lit 材质图；错误可定位 | SG-3、SG-4 |
| SG-6 | 全量节点 | 208 个 `[Title]` 节点 + 内部节点全部有测试 | SG-5 |
| SG-7 | 全 Target | Fullscreen / Canvas / CustomRT 可用；缺口写入 Known limits | SG-6 |
| SG-8 | 收尾 | 性能/文档/打包/全量回归达标 | SG-7 |

详见 [milestones.md](milestones.md)。

## 4. 执行方式

1. 每个任务开工前先读对应 Unity/hdrp-bgfx 源码，把实际 `file:line` 写进 checklist；实现后回填。
2. 一个里程碑内的任务按 checklist 顺序执行；跨里程碑功能不得提前混入。
3. 任何"简化"必须写进对应 checklist 的 `Known limits`，不能只留在代码注释。
4. 文档与源码冲突时，先修文档，再改实现；以源码行为为准。
5. 新增 C++ 模块遵守 `hdrp-bgfx/AGENTS.md`：`hbg` 命名空间、C++20、资源句柄化、IDL 常量、RenderGraph 约束。
6. 图材质的 uniform 采用"编译期生成名字 + 运行期句柄"方式，这是对 IDL 规则的显式例外，须在 `architecture.md` 与测试中固化。

### 4.1 工程与依赖（拟定）

| 项 | 约定 |
|---|---|
| 语言/命名空间 | C++20；`sg::`；核心库禁止包含 bgfx/hbg 头文件 |
| 构建 | `hbg_sg`（core/compile/nodes）、`hbg_sg_tests`、`sgbake`、`src/render/shadergraph` 运行时已落地；`hbg_sg_adapter`、`hbg_sg_host` 后续 |
| 第三方 | nlohmann/json、doctest、bimg、stb_image_write（JPEG）、shaderc（均已有）；新增 `cpp-httplib`（header-only）；Web 端 React/Vite/React Flow/Zustand/CodeMirror |
| 资产根 | 默认 `assets/`（用户）或 `tests/shadergraph/`（测试）；`.sgraph.json` / `.sgraph.editor.json` / `.sgb` / `manifest.json` 同目录 |
| 测试根 | `shadergraph/tests`（核心/编译）、`tests/shadergraph`（渲染/golden）、`web/tests`（Vitest/Playwright） |
| 诊断码 | `SG_*` 稳定；新增只增不改，前后端共享 |

## 5. 验证命令（目标形态）

```powershell
# 规划阶段先建立以下命令骨架；SG-0 起逐步落地
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "shadergraph"

# 离线烘焙与确定性
.\build\vs2022\Debug\sgbake.exe --input assets\smoke_lit.sgraph.json --output out\smoke_lit.sgb --backend dxbc,spirv
.\build\vs2022\Debug\sgbake.exe --check assets\

# 图材质渲染验证
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=forward --camera-orbit --frames=3 --screenshot=out\sg_lit_d3d.png
.\build\vs2022\Debug\hbg.exe --backend=vulkan --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=forward --camera-orbit --frames=3 --screenshot=out\sg_lit_vk.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=deferred --camera-orbit --frames=3 --screenshot=out\sg_def_d3d.png

# 编辑器宿主（仅本机）
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-editor=assets\smoke_lit.sgraph.json --sg-root=tests\shadergraph --sg-port=8777

# SG-1 核心模型（已落地）
cmake --build --preset vs2022-debug --target hbg_sg_tests
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "sg_core"

# SG-4 编辑宿主（core v1 + 主预览 + 节点预览）
cmake --build --preset vs2022-debug --target hbg hbg_tests
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-editor=assets\smoke_lit.sgraph.json --sg-root=tests\shadergraph --sg-port=8777
py -3 tools\sg_protocol_smoke.py --port 8777 --token <token> --commands --preview --node-preview

# SG-5 Web 编辑器（dev / build / tests）
cd web
npm install
npm run dev
npm run build
npm test
npx playwright install chromium
$env:SG_EDITOR_URL="http://127.0.0.1:8777/?token=<token>"
npm run test:e2e
cd ..

# SG-4 单帧/节点预览（宿主已启动）
curl -H "X-SG-Token: <token>" "http://127.0.0.1:8777/api/preview.jpg?size=512" -o preview.jpg
curl -H "X-SG-Token: <token>" "http://127.0.0.1:8777/api/preview/node/4.png?size=128" -o node.png

# SG-2 编译器（已落地）
cmake --build --preset vs2022-debug --target sgbake
.\build\vs2022\Debug\sgbake.exe --input tests\shadergraph\assets\smoke_lit.sgraph.json --output out\smoke_lit.sgb
.\build\vs2022\Debug\sgbake.exe --check tests\shadergraph\assets
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "sg_compiler"

# SG-0 spike（已落地；编译并打包 .sgb）
py -3 shadergraph\spike\build_spike.py --root . `
  --shaderc build\vs2022\_deps\bgfx.cmake-build\cmake\bgfx\Debug\shaderc.exe `
  --bgfx-src build\vs2022\_deps\bgfx-src --force
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-spike --screenshot=out\sg_spike_d3d.png --no-vsync --frames=3
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-preview --sg-token=dev --sg-port=8777 --no-vsync --frames=600
# 浏览器打开 http://127.0.0.1:8777/?token=dev

# 节点清单再生成（Unity 源变更后）
py -3 tools\sg_node_inventory.py
```

## 6. 当前状态

| 项 | 状态 |
|---|---|
| 规划文档 | 完成（本目录） |
| SG-0 spike | **完成**（D3D11/Vulkan 0 diff；MJPEG 13 fps；见 `checklists/sg0-spike.md`） |
| 代码 | SG-0 spike、SG-1 核心、SG-2 编译器/`sgbake`、SG-3 运行时、SG-4 编辑宿主 core+主预览+节点预览、SG-5 Web 编辑器 v1、SG-6 Input/Scene/Gradient/Math/Matrix/Transform/UV/Procedural/Artistic 节点 v1 已落地 |
| 当前里程碑 | SG-5 Web 编辑器 v1 主流程可用；SG-6 已开始 Input/Basic/Property/Geometry/Texture/Matrix/PBR/Scene/Lighting/Gradient/Math/Trig/Vector/Wave/Derivative/Transform/UV/Procedural/Artistic/UI/Terrain/MeshDeformation/Logic/Keyword/Preview/Switch/Dropdown/Sub-graph/Custom Function 208 节点，Texture2D Tiling/Offset `_ST` 运行期覆盖、Texture2D defaultType 1x1 fallback、2D Array/3D/Cubemap sidecar 图像数组绑定、Sample Texture 2D/Array/3D mip sampling、renderScale 驱动的 global mip bias、VirtualTexture fallback stack（自定义层引用/法线类型、LOD/Bias/Derivatives 采样、sampleQuality low/high、noFeedback 顶点约束）已接，下一步真实 Granite VT streaming/feedback 与 fixture/golden |
| 风险最高项 | sampler stage 预算、动态类型对齐、预览回读/编译延迟、Web 大图性能 |

## 7. 对齐参考（关键入口）

- Unity 图数据：`Editor/Data/Graphs/GraphData.cs:28`，节点基类 `Editor/Data/Nodes/AbstractMaterialNode.cs:16`，槽位 `Editor/Data/Graphs/MaterialSlot.cs:14`，动态值槽 `Editor/Data/Graphs/DynamicValueMaterialSlot.cs:14`。
- Unity 生成器：`Editor/Generation/Processors/Generator.cs:35`、`Editor/Generation/Processors/GenerationUtils.cs:907`、`Editor/Generation/Processors/PropertyCollector.cs:10`。
- Unity Target：`Editor/Generation/Target.cs:10`、`Editor/Generation/SubTarget.cs:10`、`Editor/Generation/Targets/BuiltIn/Editor/ShaderGraph/Targets/BuiltInLitSubTarget.cs:14`。
- Unity 导入：`Editor/Importers/ShaderGraphImporter.cs:20`（`ShaderUtil.CreateShaderAsset` 在 `:173`）。
- Unity 编辑器：`Editor/Drawing/Views/MaterialGraphView.cs:26`、`Editor/Drawing/MaterialGraphEditWindow.cs:26`、`Editor/Drawing/PreviewManager.cs:24`。
- hdrp-bgfx 表面契约：`shaders/material/surface_data_types.sc:6`、`shaders/material/surface_data.sc:15`、`shaders/passes/forward.fs.sc:17`、`shaders/passes/gbuffer.fs.sc:17`。
- hdrp-bgfx 绑定：`src/render/passes/forward_lighting.h:10-18/66`、`src/shader/shader_key.h:8/119/136`、`src/shader/shader_library.cpp:52/1045`。
- hdrp-bgfx 运行时 blob 编程：`vfx/include/vfx/device.h:37/43`、`src/render/vfx/hbg_vfx_backend.cpp:366`。
- hdrp-bgfx 编辑器/资产先例：`vfx/include/vfx/editor.h:60`、`vfx/tools/vfxbake.cpp:240`、`docs/vfx/architecture.md`、`docs/vfx/asset-format.md`。
