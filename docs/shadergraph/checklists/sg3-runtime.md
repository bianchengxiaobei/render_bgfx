# SG-3 运行时接入

> 目标：把 `.sgb` 图材质接入 `hdrp-bgfx`，复用现有光照/阴影/GBuffer/后处理，且不回归旧材质。
> 状态：**完成（v1，2026-11-23）**；forward/deferred/unlit/alpha-clip/double-sided 双后端已验证；motion/GI 等已知限制见 §8。
> 前置：SG-2（编译产物可用）、SG-0（stage/回读结论）。
> 参考：`src/render/material`、`src/render/passes`、`src/shader`、`src/asset`、`vfx` 的运行时编程模式。

---

## 1. 范围

- `src/render/shadergraph/`：`graph_material_system.{h,cpp}`、`graph_program_cache.{h,cpp}`、`graph_binding.{h,cpp}`。
- `src/asset/material.h`/`scene_loader.cpp`：材质 sidecar 扩展。
- `shaders/shadergraph/`：Lit/Unlit 正式模板。
- stage 契约验证（v1 不做 remap）：图模板不声明 0-4 材质 sampler。
- 不实现编辑器/HTTP/Web。

## 2. 任务

| ID | 任务 | 产物 | 参考 | 验收 |
|---|---|---|---|---|
| SG3-1 | Stage 契约验证（v1 不做 remap）：图模板不声明 0-4 材质 sampler；5 贴图预算 + `requiredFeatureMask` | 校验代码/测试 | `src/render/passes/forward_lighting.h:10-18`、`shaders/material/surface_data.sc:9-13`、`shaders/common/light_data.sc:20/37` | 现有 shader golden 0 diff；预算/特性不满足时报错 |
| SG3-2 | Lit 图模板：forward/gbuffer/prepass/shadow/motion | `shaders/shadergraph/*.sc`、`varying_graph.def.sc` | `shaders/passes/forward.fs.sc:17`、`gbuffer.fs.sc:17`、`prepass.fs.sc:3`、`shadowcaster.fs.sc:7` | 编译通过；spike 图出图 |
| SG3-3 | Unlit 程序与 pass | `shaders/shadergraph/unlit_*`、`PassId` 扩展 | `src/render/material/shader_pass.cpp:44` | 无光照输出正确 |
| SG3-4 | `GraphMaterialSystem`：加载/清单/program cache | `graph_material_system.*` | `src/shader/shader_library.cpp:52/1045`、`src/gfx/resource_manager.h:56`、`src/render/vfx/hbg_vfx_backend.cpp:366` | `.sgb` 可加载；缺失回退 |
| SG3-5 | 属性/贴图绑定（CB、sampler flags、变体选择） | `graph_binding.*` | `src/gfx/encoder.h:36-38`、`src/render/material/material_system.cpp:168`、`src/render/material/material_system.h:25/42` | 改属性/贴图即时生效 |
| SG3-6 | ForwardRenderer 接入：draw list/queue/state/透明/AlphaClip/DoubleSided | `forward_renderer.*` | `src/render/passes/forward_renderer.cpp:149/164/200/6457`、`src/render/material/shader_pass.cpp` | forward/deferred 图材质正确 |
| SG3-7 | 材质 sidecar：`shader/propertyOverrides/textureOverrides/keywordOverrides` | `src/asset/material.h`、`scene_loader.cpp` | `src/asset/material.h:39`、`src/asset/scene_loader.cpp:1506/1542` | sidecar 覆盖生效；旧字段不变 |
| SG3-8 | 热重载/失败回退/诊断 | `graph_material_system.cpp` | `vfx/src/runtime/runtime.cpp:643` | 改 `.sgb` 自动生效；损坏保留旧版 |
| SG3-9 | 测试/golden/旧材质回归 | `tests/shadergraph/` | `AGENTS.md` 构建与测试 | 双后端 golden；旧 golden 0 diff |

## 3. 渲染接入细节

### 3.1 Pass 选择

- `Material` 增加 `graphShader`（`.sgb` 路径 + 内容哈希）。
- `defaultShaderPassMask`：Lit 使用 `Forward/GBuffer/DepthOnly/ShadowCaster/MotionVectors`；Unlit 使用 `ForwardOnly/DepthForwardOnly/ShadowCaster/MotionVectors`。
- Deferred 下 Lit 图材质走 `GBufferGraph`；Unlit 走 forward-only 队列。
- Transparent 只走 forward；AlphaClip 使用变体 + alpha clip 状态。
- v1 图材质不参与 instancing；`drawForwardBatch` 遇到图材质返回 false，回退逐物体路径。

### 3.2 Stage 与 uniform

- 按 `architecture.md` §3.3：图贴图 0-4（默认 5），引擎 5-15。
- 每个 pass 的 stage 表来自 `.sgb`；绑定代码按语义名查表，不硬编码数字。
- 属性 uniform 按名字创建并缓存；上传前按类型转换（矩阵行→列）。
- 需要逆模型的图声明 `u_sg_modelInverse`；图材质绘制路径按 DrawItem world 矩阵求逆并逐 draw 上传，供 Transform/TransformationMatrix/Geometry object-space 使用。
- 贴图 sampler flags 来自属性/材质覆盖；无纹理时使用默认纹理（white/gray/bump）。

### 3.3 渲染状态

- 复用现有状态函数（`forwardState/bakedGIState/shadowState`，`forward_renderer.cpp:149/164/200`）。
- 图 Target 的 `RenderStateDesc` 与材质设置合并：队列/Blend/Cull/ZTest/ZWrite。
- `DOUBLE_SIDED` 的法线翻转仍在模板/surface 中处理；不要与现有材质逻辑重复。
- 透明物体的排序/队列沿用 `DrawList` 现有规则（`AGENTS.md`）。

## 4. 热重载与回退

```
watch .sgb mtime/hash
  → 内容变化：加载新资产 → 校验 → 创建新 program/纹理 → 切换实例 → 销毁旧资源
  → 失败：保留旧资源；记录一次错误（不逐帧刷屏）
  → 缺失/损坏：回退旧材质路径；UI/日志提示
```

- program 缓存键：`(assetHash, pass, variantKey)`。
- 变体键：引擎变体 + 图关键字位；不存在时回退基础变体并告警一次。
- 资源生命周期：`GraphMaterialSystem::shutdown` 必须销毁所有 program/uniform/texture。

## 5. 验收

```powershell
# 图材质场景
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\lit_graph.gltf --frames=3 --screenshot=sg_lit_d3d.png
.\build\vs2022\Debug\hbg.exe --backend=vulkan --render-test=tests\shadergraph\lit_graph.gltf --frames=3 --screenshot=sg_lit_vk.png

# forward / deferred
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=forward --frames=3 --screenshot=sg_fwd.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=deferred --frames=3 --screenshot=sg_def.png

# 旧回归
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<旧场景> --frames=3 --screenshot=old.png
```

通过标准：

- Lit 图材质 forward + gbuffer 双后端出图；与手写等价 shader 0 diff 或阈值内。
- Unlit 无光照输出正确；透明/AlphaClip/DoubleSided 用例通过。
- stage 契约不改变现有 shader 输出（golden 0 diff）。
- 热重载：修改 `.sgb` 后下一帧生效；损坏文件不崩溃。
- 资源泄漏检查：启动/关闭/热重载多次无泄漏（沿用仓库资源测试方式）。

## 6. Known limits（初始）

- 图材质不参与 instancing/GPU driven。
- 透明图材质不支持 distortion/backface pass。
- 自定义插值器（Custom Interpolators）在 v1 不支持。
- 图材质的 GBuffer 高级特性（ClearCoat/Transmission 等）在 v1 不写入 advanced 通道。
- Lightmap/BakedGI 对图材质的支持在 SG-7 完善。

## 7. 对齐参考

- 材质/采样器：`src/asset/material.h:39`、`src/render/material/material_system.h:25/42`、`src/render/material/material_system.cpp:168`。
- 程序/清单：`src/shader/shader_key.h:8/119/136`、`src/shader/shader_library.cpp:52/1045`、`src/gfx/resource_manager.h:56`。
- 绑定：`src/render/passes/forward_lighting.h:10-18/66`、`src/gfx/encoder.h:36-38/49`、`src/render/passes/forward_renderer.cpp:6457`。
- 状态：`src/render/passes/forward_renderer.cpp:149/164/200`。
- Unlit pass mask：`src/render/material/shader_pass.cpp:44`。
- sidecar：`src/asset/scene_loader.cpp:1506/1542`。
- VFX 运行时编程：`vfx/include/vfx/device.h:37/43`、`src/render/vfx/hbg_vfx_backend.cpp:366`、`vfx/src/runtime/runtime.cpp:643`。


---

## 8. 实现记录与验收（2026-11-23）

### 8.1 任务回填

| ID | 状态 | 实现 | 验证 |
|---|---|---|---|
| SG3-1 | 完成 | `sgb_asset.cpp::validateStageContract` 校验 0-4 graph stage、重复 stage、stage 预算与 `requiredFeatureMask` | `test_graph_material.cpp` 篡改 stage 用例 |
| SG3-2 | 部分完成 | `forward/gbuffer/depth/shadow` 已接入；motion pass 未编入 SG-2 资产 | D3D11/Vulkan forward + deferred 出图 |
| SG3-3 | 完成 | `graph_material_system.cpp` 识别 `unlit` target；unlit forward/depth/shadow program | `smoke_unlit.sgb` 双后端 0 diff |
| SG3-4 | 完成 | `sgb_asset`、`graph_program_cache`、`graph_material_system`；见 §8.2 | parser/hot reload/全 27 资产测试 |
| SG3-5 | 完成 | `graph_binding`：属性 uniform、sampler、`_TexelSize`、matrix/vector、keyword variant；`graph_material_system` 逐 draw 上传 `u_sg_modelInverse` | `graphVariantKey` 用例；渲染 sRGB/override |
| SG3-6 | 完成 | `forward_renderer.cpp` 图材质分支与 batch/GPU-driven 回退 | forward/gbuffer/prepass/shadow 分支 |
| SG3-7 | 完成 | `asset/material.h`、`scene_loader.cpp`：`shader/propertyOverrides/textureOverrides/keywordOverrides`、`alphaMode/doubleSided` | sidecar 用例 |
| SG3-8 | 完成 | `GraphMaterialSystem::poll`：mtime 热重载、损坏保留旧 asset、旧 program 释放 | 手动跑 16000 帧热重载成功 + 损坏回退 |
| SG3-9 | 完成 | `tests/render/test_graph_material.cpp`、`tests/shadergraph/lit_graph.*` | hbg_tests 290/33162；ctest 129/129 |

### 8.2 关键实现

- `src/render/shadergraph/sgb_asset.{h,cpp}`：SGB1 运行时解析（section 1-7、字符串表、属性、关键字、pass/stage、variant/group、blob）；不依赖 `sg`/bgfx 核心。
- `src/render/shadergraph/graph_program_cache.{h,cpp}`：`(contentHash, pass, variant, backend)` → `ResourceManager::createProgram`；缺失 variant 回退并只告警一次。
- `src/render/shadergraph/graph_binding.{h,cpp}`：`u_sg_<ref>`/`s_sg_<ref>`/`u_sg_<ref>_TexelSize`；属性类型与 override；关键字 recompute；纹理 stage 0-4；`graph_material_system` 逐 draw 上传 `u_sg_modelInverse`。
- `src/render/shadergraph/graph_material_system.{h,cpp}`：按材质路径加载/缓存资产；fallback 纹理；unlit target 自动改写 `material.type`；mtime 热重载。
- `src/asset/material.h`/`scene_loader.cpp`：sidecar 扩展；`shader` 路径解析为绝对路径，属性数组 1-16 分量，贴图加入 `scene.images`。
- `src/render/passes/forward_renderer.cpp`：forward/gbuffer/prepass/shadow 的逐物体图材质分支；图材质跳过 instanced/GPU-driven/BakedGI/motion；图场景强制 texture light-list 路径。
- `src/render/material/shader_pass.cpp`：图材质默认不请求 MotionVectors（无 SGB motion pass）。

### 8.3 测试与 golden

```powershell
# 运行测试
ctest --test-dir build/vs2022 -C Debug --output-on-failure

# forward 双后端
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=forward --camera-orbit --frames=3 --screenshot=out\sg_lit_d3d.png
.\build\vs2022\Debug\hbg.exe --backend=vulkan --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=forward --camera-orbit --frames=3 --screenshot=out\sg_lit_vk.png

# deferred 双后端
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=deferred --camera-orbit --frames=3 --screenshot=out\sg_def_d3d.png
.\build\vs2022\Debug\hbg.exe --backend=vulkan --render-test=tests\shadergraph\lit_graph.gltf --lit-shader-mode=deferred --camera-orbit --frames=3 --screenshot=out\sg_def_vk.png
```

实测：

- `hbg_tests`: **290 cases / 33162 assertions** 全绿；SG-3 新增 **9 cases / 231 assertions**。
- `ctest`: **129/129** 通过。
- forward D3D11 vs Vulkan：MSE≈`1.09e-6`，PSNR≈`107.8 dB`（仅 1-2 LSB 差异）。
- deferred D3D11 vs Vulkan：MSE≈`5.43e-7`，PSNR≈`110.8 dB`。
- unlit：双后端 **0 diff**；alpha-clip / double-sided：双后端 **0 diff**。
- 图场景测试资产：`tests/shadergraph/lit_graph.gltf/.bin/.hdrp.json`、`grid.bmp`；sidecar 默认路径为 `<scene>.hdrp.json`。
- 热重载：运行中替换 `.sgb` 后下一帧生效；写入损坏文件打印一次 `hot reload failed, keeping previous asset` 并继续渲染。

### 8.4 实际限制（覆盖原 Known limits）

- 图材质 **MotionVectors pass 尚未编入** SG-2 `.sgb`，默认 pass mask 不请求 MotionVectors；motion 图材质后续与 SG-7 一起补齐。
- 图 shader 未定义 `HBG_LIGHT_LIST_BUFFER`；图场景强制走 **CPU light-list texture 路径**，标准材质同场景也使用该路径，避免 stage 7 的 buffer/texture 类型不匹配。
- 图材质不参与 instancing/GPU-driven shadow/GBuffer；遇到图材质时逐物体回退。
- 透明图材质的 distortion/backface/prepass/postpass 未实现；alpha-clip 与 double-sided 已验证。
- BakedGI/Lightmap/Debug 材质通道、ClearCoat/Transmission 等 advanced GBuffer 字段、自定义插值器、双面法线翻转不完整。
- `.sgb` 不保存纹理属性的默认路径/色彩空间；运行时无 override 时使用 **linear white** fallback，需要色彩纹理时在 sidecar 中显式覆盖。
- sidecar 属性 override 的 key 必须以 `_` 开头；矩阵按 JSON 行主序写入，复杂矩阵约定仍需逐节点验证。
- 同名但类型不同的图属性会触发 `GraphUniformCache` 冲突并禁用该材质；这是当前按名字全局创建 uniform 的约束。
- `GraphMaterialSystem` 不扫描 `manifest.json`；运行时只加载 sidecar 显式引用的 `.sgb`。
- 变体键由材质 `alphaMode/doubleSided` 与图 keyword override 计算；若 `.sgb` 未编译该键则回退首个匹配变体并只告警一次，发布前应使用 `sgbake --variants all` 或匹配 sidecar。
- 热重载保留旧资源直到新资产绑定成功；旧 program 在新 hash 无其他材质使用时释放。
