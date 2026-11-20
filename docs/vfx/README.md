# VFX 运行时 V1 规划索引

> 工程根：`E:\Learn\hdrp-bgfx`
> 独立库：`vfx/`（C++20，无 bgfx/HDRP 依赖）
> 引擎适配层：`src/render/vfx/`（hdrp-bgfx）
> 状态：规划完成（2026-11-20）；V0 技术 spike 已通过；V1.0b.1–V1.0b.8 已完成（JSON/layout/表达式、Backend/Mock/adapter、compiler/`vfxbake`、Runtime/Effect、scene/CLI/RenderGraph、CPU reference、GPU readback、D3D11/Vulkan golden 与文档回填）；V1.1.1（常量/单次/周期 Burst、dt clamp、spawner CPU reference）与 V1.1.2（Gravity/Drag/Force/Attractor/Turbulence、CPU/GPU curl reference、双后端 golden）已完成；下一步 V1.1.3。
> 原则：先读 Unity VFX Graph / HDRP / hdrp-bgfx 源码，再实现；每个任务必须在 checklist 记录参考 `file:line`。

---

## 0. 已确认决策

| 决策 | 结果 |
|---|---|
| 运行时落点 | 独立库 `hdrp-bgfx/vfx` + `src/render/vfx` 适配层 |
| 资产路线 | 自有 JSON 图 → `vfxbake` 离线烘焙 `.vfxb`（dxbc + spirv） |
| 引擎驱动 | 每帧手动 `Runtime::update(dt)` + `encodeSimulation` / `encodeOutput` |
| 输出 | Unlit Quad + Lit Quad；Lit 复用 hdrp-bgfx 现有 HDRP 光照 |
| 仿真范围 | 常量/周期 Burst、重力/阻力/力场/湍流、平面/球碰撞、Curve/Gradient、GPU 事件、GPU 排序、子图、自定义 HLSL |
| 平台 | Windows，D3D11 + Vulkan |
| 性能 | V1 只测量并记录，不设硬门槛 |

## 1. V1 范围

### 做

- 图资产：多 system、每 system 一个 spawner / init / update / 一个或多个 quad output。
- 属性：内置属性 + 自定义属性；raw buffer 属性集与确定性布局。
- 生成：常量速率、单次 Burst、周期 Burst。
- 运动：Gravity、Drag、Force、Attractor、Turbulence（curl noise）。
- 碰撞：解析平面、球；kill / bounce / stick；friction / restitution。
- 外观：Color / Alpha / Size / Rotation over Life；Curve 与 Gradient LUT。
- 事件：CPU `SendEvent`、GPU 事件（死亡生成 / 子发射器）、事件属性传递。
- 排序：GPU bitonic KVP sort；distance / custom key；透明输出排序。
- 图能力：子图展开、属性重映射、自定义 HLSL block（受限子集）。
- 渲染：Unlit quad、Lit quad（复用 `lighting/light_loop.sc`）、Alpha/Additive、软粒子、手动 bounds + 相机剔除。
- 运行时：多 effect 实例、play/stop/pause/reset、playRate、固定种子、诊断计数。
- 工具：`vfxbake`（JSON → `.vfxb`）、`--check` 确定性校验、CLI/sidecar 加载。
- 测试：单测、mock backend 命令流、CPU reference、D3D11/Vulkan golden、资源泄漏检查。

### 不做（V1 明确边界）

- Unity `.vfx` / `VisualEffectAssetDesc` 导入。
- 图编辑器 UI（V2）。
- Mesh / Line / Ribbon / Point / Decal / SDF / VectorField / Mesh Sampling 输出。
- 粒子投影阴影、粒子 motion vector、TAA 专用粒子重投影。
- effect GPU instancing（多 effect 共享一次仿真）；V1 每个 effect 独立资源。
- 自动 bounds GPU readback；V1 手动 bounds + 编译期 spawner bounds。
- async compute、D3D12、Metal、多相机。
- Custom C# spawner / property binder / Timeline 等价物。

## 2. 里程碑

| 里程碑 | 内容 | 出口 |
|---|---|---|
| V0 | 技术 spike：raw buffer、32B indirect、VS 读 storage、unlit/lit 采样 | **已通过**：D3D11/Vulkan `--vfx-spike`；buffer/pixel readback 精确；unlit/lit/alpha/additive 像素数一致；D3D11 validation 通过；16+16 绑定预算；见 `v0-checklist.md` |
| V1.0b | 库骨架、`.vfxb`、attribute layout、deadList、常量生成、init/update、outputUpdate、fillIndirect、unlit quad、property API、bounds、compiler/CLI | **已完成**：V1.0b.1–V1.0b.8（JSON/layout/codegen、表达式、Backend/Mock/adapter、compiler/`vfxbake`、reader、Runtime/Effect、scene/CLI/RenderGraph、CPU reference、D3D11/Vulkan golden）；下一步 V1.1 |
| V1.1 | Burst、重力/阻力/力场/湍流、平面/球碰撞、Curve/Gradient | **进行中**：V1.1.1 Spawner 与 V1.1.2 力场已完成（CPU/GPU 1e-3 reference、Turbulence curl、双后端 golden）；下一步 V1.1.3 碰撞 |
| V1.2 | GPU 事件 + prefix sum、KVP sort、多 system 连接 | 事件链/排序稳定性/确定性 |
| V1.3 | 子图展开、自定义 HLSL、图校验/错误定位、hot-reload | 展开等价性；自定义 HLSL 双后端编译 |
| V1.4 | Lit quad（复用 HDRP 光照）、软粒子、forward/deferred、性能/诊断、发布验收 | 全矩阵 golden；`ctest`/IDL；无泄漏；V1 发布 |
| V2 | 编辑器（后置） | 消费同一 JSON/.vfxb，不改运行时接口 |

## 3. 文档索引

- `architecture.md`：模块边界、编译/运行时流水线、渲染集成、绑定/stage 表、测试与风险。
- `asset-format.md`：`.vfx.json` schema、`.vfxb` 二进制格式、版本/确定性规则。
- `backend-api.md`：独立库公开 API、`Device`/`CommandSink`/`ShadingProvider`/`FrameContext`、适配层映射。
- `source-alignment.md`：Unity VFX Graph / HDRP / hdrp-bgfx 的 `file:line` 对齐清单。
- `v0-checklist.md`：V0 spike 任务、参考、验收。
- `v1-checklist.md`：V1.0b–V1.4 逐任务清单、参考、验收。
- `v2-editor-outline.md`：编辑器后续接口边界（只占位，不实现）。

## 4. 执行方式

1. **先做 V0**：按 `v0-checklist.md` 逐项完成；每项记录实际 `file:line` 与结论。当前可复现命令：
   - `build\vs2022\Debug\hbg.exe --backend=d3d11 --vfx-spike --no-vsync`
   - `build\vs2022\Debug\hbg.exe --backend=vulkan --vfx-spike --no-vsync`
2. V0 通过后按 `v1-checklist.md` 的 V1.0b → V1.1 → V1.2 → V1.3 → V1.4 顺序实现；不跨里程碑并行堆功能。
3. 每个任务完成后：
   - 勾选 checklist；
   - 回填“实现位置”（新文件 `file:line`）；
   - 记录与 Unity/HDRP 的偏差（`Deviation` 行）；
   - 运行对应测试。
4. 发现文档与源码冲突时：**先改文档，再实现**；以源代码行为为准。
5. 任何“简化”必须写进对应 checklist 的 `Known limits`，不能只在代码注释里。

## 5. 验证命令（实现后）

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure
py -3 tools/idl_gen.py --check

# V1.0b.4 后端/适配层 + V1.0b.5 编译器 + V1.0b.6 runtime + V1.0b.7 集成 + V1.0b.8 reference/golden
cmake --build build/vs2022 --config Debug --target hbg_vfx_tests hbg_vfx_adapter_tests hbg_vfx_compiler_tests hbg_vfx_runtime_tests vfxbake hbg
ctest --test-dir build/vs2022 -C Debug --output-on-failure -R "hbg_vfx"

# CPU/GPU reference：1024 粒子、10 帧、逐粒子 1e-3
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --vfx-reference=vfx\tests\assets\reference_1024.vfxb --vfx-reference-graph=vfx\tests\assets\reference_1024.vfx.json

# 可选：运行 compiler 的 shaderc 集成测试
$env:VFX_TEST_SHADERC=(Resolve-Path 'build\vs2022\_deps\bgfx.cmake-build\cmake\bgfx\Debug\shaderc.exe').Path
$env:VFX_TEST_BGFX_SOURCE=(Resolve-Path 'build\vs2022\_deps\bgfx-src').Path
build\vs2022\vfx\Debug\hbg_vfx_compiler_tests.exe

# 烘焙/校验测试资产
.\build\vs2022\vfx\Debug\vfxbake.exe --check vfx\tests\assets --shaderc build\vs2022\_deps\bgfx.cmake-build\cmake\bgfx\Debug\shaderc.exe --bgfx-src build\vs2022\_deps\bgfx-src

# GPU 验收（实现后）
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=vfx\tests\assets\vfx_scene.gltf --vfx-test --frames=3 --screenshot=vfx_d3d.png
.\build\vs2022\Debug\hbg.exe --backend=vulkan --render-test=vfx\tests\assets\vfx_scene.gltf --vfx-test --frames=3 --screenshot=vfx_vk.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=vfx\tests\assets\vfx_scene.gltf --no-vfx --frames=3 --screenshot=vfx_off.png

# deferred mesh + VFX
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=vfx\tests\assets\vfx_scene_mesh.gltf --pipeline-settings=config\pipeline.json --deferred --vfx-test --frames=3 --screenshot=vfx_deferred.png
```

示例资产：`vfx/tests/assets/vfx_scene.gltf`（camera-only）与 `vfx_scene_mesh.gltf`（deferred mesh）及对应 `.hdrp.json`，asset 指向同目录 `minimal_unlit.vfxb`。

## 6. 源码对齐与许可证

- 对齐目录：`source-alignment.md`。Unity 包路径：`E:\Learn\Graphics\Packages\com.unity.visualeffectgraph`；HDRP 路径：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition`。
- Unity 代码/HLSL 只作行为与数据布局参考，**不直接复制**（Unity Companion License 限制）；所有 shader/代码按 bgfx `.sc` 与 hdrp-bgfx 约定重写。
- 每个 checklist 任务的“参考”列必须至少包含一个原项目 `file:line`；无法对齐时必须写明原因。

## 7. 关键前置结论（来自源码阅读）

- **`com.unity.visualeffectgraph` 不包含引擎 VFX 运行时**：`VisualEffect`/`VFXManager`/GPU 任务执行器/Indirect 渲染在 Unity 引擎 VFX 模块（Unity 源码 `Modules/VFX`），不在 Graphics 仓库。本计划是按模板/数据布局/引擎行为**重实现**，不是移植 runtime 源码。
- 64 threads/group：`Editor/NewCompiler/VfxDispatch.cs:11-21`。
- quad 输出按 `vertexID >> 2 + instanceID * 2048` 映射粒子：`Editor/NewCompiler/Passes/VfxTemplateCodeGenerationPass.cs:12-15,554`；Unity 用 4 顶点 + 6 index；本项目 V1 非索引 **6 顶点**（V0 确认 4 顶点无法组成两个三角形）。UV 语义参考 `Editor/ShaderGraph/Templates/VFXConfigPlanarPrimitive.template.hlsl:13-58`。
- 属性布局按 bucket 分组、offset/stride、capacity 对齐：`Editor/GraphCommon/Compiler/AttributeSetLayout.cs:18-103`、`Editor/NewCompiler/Passes/AttributeLayoutPass.cs:30-115`。
- deadList、spawn/init/update、事件、排序、indirect 的 GPU 语义：`com.unity.visualeffectgraph/Shaders/Temp/*` 与 `com.unity.visualeffectgraph/Shaders/*.compute`。
- hdrp-bgfx 的 indirect 固定 32B/条：`src/gfx/buffer.cpp:132-138`；compute/storage 绑定上限 16：`build/vs2022/_deps/bgfx-src/src/config.h:498-500`、`src/bgfx_p.h:211`。
- hdrp-bgfx 的前向透明 pass 与光照 stage：`src/render/passes/forward_renderer.cpp:4828-4880,6210-6310`。

