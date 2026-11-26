# SG-0 技术 Spike

> 目标：用最小闭环验证最高风险项，避免在错误前提下投入完整实现。
> 状态：**完成**（2026-11-23；D3D11 + Vulkan 实测通过）。
> 前置：无。实验代码位于 `shadergraph/spike/`、`shaders/shadergraph/spike/`、`src/app/sg_*`；未改动主渲染路径。
> 参考根：Unity `E:\Learn\Graphics\Packages\com.unity.shadergraph`；渲染 `E:\Learn\hdrp-bgfx`。

---

## 0. 决策确认（实现前）

| 决策 | 结论 | 依据 |
|---|---|---|
| 默认 5 张贴图 | **确认**：图贴图固定 stage 0-4；引擎从 5 开始 | `shaders/material/surface_data.sc:9-13` 被图模板省略；`shadows.sc:664` 的 shadow context 占 t8；`forward_lighting.h:10-18` |
| v1 不做 stage remap | **确认**：光照 include 字面量 stage 不变；图模板只替换 surface 函数 | `forward.fs.sc:5-7`、`light_loop.sc`、`stage_map.md` |
| 5 贴图预算是否可行 | **可行**：SG-0 实际绑定 0-4 五张图 + 引擎 5/6/7/8/9/10/11/14/15，无冲突 | 本文件 §5；D3D11/Vulkan 均通过 |

## 1. 验证问题

1. 生成的 HLSL 能否被 `shaderc` 以 `.sc` 方式编译为 dxbc（s_5_0）与 spirv？
2. 图材质能否复用现有 `SurfaceData` 与 forward 光照，只替换表面函数？
3. 图属性 uniform 能否按名字创建/上传？5 张贴图（0-4）+ 引擎 stage 的预算是否成立？
4. `.sgb` 能否在运行时创建 program 并绑定？
5. 离屏预览 + readback + JPEG/MJPEG 的延迟是否可接受？

## 2. 实验范围

- 图：6 节点 Lit 图
  - `UV` → `TilingAndOffset`（默认 1,1/0,0）→ `SampleTexture2D` → `Multiply`（与 Property `_Tint`）→ Lit `BaseColor`。
  - 属性：`_MainTex`（texture2d）、`_Tint`（color）、`_Smoothness`（float）。
  - 目标：`lit`，forward pass；暂不编译 gbuffer/shadow。
- 运行时：临时硬编码 `GraphMaterial`（`SgSpike`），不接入 `MaterialSystem`。
- 预览：512×512 离屏 FBO + `bgfx::readTexture` + stb JPEG；localhost HTTP 页面显示 MJPEG。

## 3. 任务与完成情况

| ID | 任务 | 产物 | 状态 |
|---|---|---|---|
| SG0-1 | 图模板省略 `surface_data.sc`，使用光照默认 stage；图贴图 0-4 | `shaders/shadergraph/spike/forward_graph.*`、`stage_map.md` | 完成 |
| SG0-2 | 生成 surface include（手写模板 + 简单表达式） | `shadergraph/spike/generate_surface.cpp`、`shaders/shadergraph/spike/surface.spike.sc` | 完成 |
| SG0-3 | 图 forward 模板（VS/FS），替换 `hbgMakeSurfaceData` | `forward_graph.vs.sc`、`forward_graph.fs.sc` | 完成（dxbc+spirv 编译通过） |
| SG0-4 | shaderc 调用与缓存 | `shadergraph/spike/build_spike.py`；`--check` 确定性 | 完成 |
| SG0-5 | 属性 uniform：3 属性创建/上传 | `SgSpike::tintUniform/smoothnessUniform/mainSampler` | 完成 |
| SG0-6 | 贴图 stage 0-4 绑定；引擎 5-15 | `SgSpike::bindTextures()` | 完成 |
| SG0-7 | 运行时 program 创建（blob） | `SgSpike::createResources()` + `Asset::find()` | 完成 |
| SG0-8 | 离屏预览 + readback + JPEG | `src/app/sg_spike.cpp`、`src/app/sg_jpeg.cpp` | 完成 |
| SG0-9 | 最小 HTTP MJPEG | `src/app/sg_http_spike.cpp` | 完成 |
| SG0-10 | 双后端截图对比与实测记录 | 本文件 §5-§6 | 完成 |

### 3.1 实现文件

- `shadergraph/spike/generate_surface.cpp`：生成 surface include（可重复运行）。
- `shadergraph/spike/build_spike.py`：编译四个 blob、打包 `spike.sgb`、`--check` 确定性。
- `shadergraph/spike/sgb.h/.cpp`：SGB1 子集解析（magic/version/sections/BLOBS）。
- `shaders/shadergraph/spike/forward_graph.vs.sc` / `.fs.sc`：图 forward 模板。
- `shaders/shadergraph/spike/surface.spike.sc`：生成产物。
- `shaders/shadergraph/spike/stage_map.md`：0-15 stage 表。
- `src/app/sg_spike.h/.cpp`：球体、5 纹理、光照、readback、截图、循环。
- `src/app/sg_http_spike.h/.cpp`：localhost HTTP + `/frame.jpg` + `/stream.mjpeg`。
- `src/app/sg_jpeg.h/.cpp`：stb JPEG/PNG 编码（bimg 无 JPEG 编码）。
- `tests/shadergraph/assets/spike.sgb`：dxbc + spirv 两个后端的 VS/FS blob。
- `third_party/cpp-httplib/httplib.h`、`third_party/stb/stb_image_write.h`。

## 4. 不做

- 不实现完整图编辑器/协议。
- 不接入 `.hdrp.json` 材质 sidecar。
- 不实现 gbuffer/shadow/motion/unlit。
- 不做变体/关键字（只固定一个变体）。
- 不修改现有主渲染 shader 的默认行为（v1 不做 remap）。

## 5. 验收

```powershell
# 主 shader 与 SG-0 spike shader 都由 CMake 构建
cmake --preset vs2022
cmake --build --preset vs2022-debug --target hbg

# 生成 + 编译 + 打包
py -3 shadergraph/spike/build_spike.py --root . `
  --shaderc build/vs2022/_deps/bgfx.cmake-build/cmake/bgfx/Debug/shaderc.exe `
  --bgfx-src build/vs2022/_deps/bgfx-src --force
py -3 shadergraph/spike/build_spike.py --root . `
  --shaderc build/vs2022/_deps/bgfx.cmake-build/cmake/bgfx/Debug/shaderc.exe `
  --bgfx-src build/vs2022/_deps/bgfx-src --check

# 截图
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-spike --screenshot=out/sg_spike_d3d.png --no-vsync --frames=3
.\build\vs2022\Debug\hbg.exe --backend=vulkan --sg-spike --screenshot=out/sg_spike_vk.png --no-vsync --frames=3

# 预览
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-preview --sg-token=dev --sg-port=8777 --no-vsync --frames=600
# 浏览器打开 http://127.0.0.1:8777/?token=dev
```

**通过标准（实测）**

- D3D11/Vulkan 截图：**0 diff，PSNR=inf**（`out/sg_spike_d3d.png` / `out/sg_spike_vk.png`，512²，mean=0.2892，nonBackground=76815）。
- 5 贴图 + 引擎光照同时绑定：**通过**；无 sampler/stage validation error。
- 512² 预览：**≈13 fps**（D3D11，3 秒收到 39 帧，39 个 JPEG boundary），满足 ≥10 fps。
- 首帧 readback 有 0.5-3 s 预热；稳态 readback 4-9 ms。

## 6. 实测记录

| 项 | 预期 | 实测 | 结论 |
|---|---|---|---|
| dxbc 编译（forward VS） | <300ms | ~0.15s | 通过 |
| dxbc 编译（forward FS） | <300ms | 25-34s（Debug shaderc，无 `-O`） | **超预期**；FXC 对完整 light loop 很慢，现有 forward 同样 45s+ |
| spirv 编译（forward FS） | <600ms | 29-36s（Debug shaderc，`-O 3`） | **超预期**；记录为已知成本 |
| 512² readback（稳态） | <10ms | D3D11 **4-9ms** / Vulkan **4.56ms**（preview） | 通过 |
| 512² readback（首帧） | - | 0.06-3.1s（Debug，驱动/readback 初始化） | 不计入稳态；后续预热后测量 |
| JPEG 编码（512², q85） | <5ms | D3D11 ~26ms / Vulkan ~23ms（Debug） | **超预期**；RelWithDebInfo 待复测 |
| MJPEG fps | ≥10 | 13 fps（D3D11，3s / 39 帧） | 通过 |
| stage 冲突 | 0 | 0 | 通过 |
| 双后端差异 | 可解释 | **0 diff** | 通过 |

## 7. 风险与决策点

- 若 0-4 预算不可行：退化为更少贴图/特性门控；SG-0 已证明默认 5 可行。
- 若 readback 阻塞明显：改为按需单帧截图 + 轮询；当前稳态 4-5ms，可连续流。
- 若 shaderc 对生成 HLSL 有额外限制：记录最小约束集。当前无编译错误；dxbc 需跳过 FXC 优化以避免长尾。
- 若光照复用导致差异：当前 D3D11/Vulkan 0 diff，说明 surface hook 与光照输入一致。

## 8. Known limits（SG-0）

- `.sgb` 仅实现 SGB1 子集：META/PASSES 为 JSON，运行时只解析 BLOBS；无 STRINGS/PROPERTIES/KEYWORDS/VARIANTS/HLSL。完整布局在 SG-2。
- 图模型未实现；surface 由 `generate_surface.cpp` 硬编码生成，仅 6 节点 Lit。
- 只编译 forward；gbuffer/prepass/shadow/motion/unlit 未验证。
- 固定单变体，无关键字/属性默认值/材质 sidecar。
- 图贴图 0-4 中只有 stage 0 参与最终颜色；1-4 为 1×1 dummy，用于验证预算与绑定；不证明任意 5 张图内容正确。
- JPEG 编码使用 vendored stb_image_write（bimg 无 JPEG 编码），Debug 构建下较慢。
- 预览进程按 `--frames` 退出（`--sg-preview` 默认 600 帧），不是常驻编辑器；HTTP 仅 localhost + token。
- 首次 readback 预热明显；优化留到 SG-3/SG-4。
- build_spike.py 的缓存键是 spike 源 + generated surface + varying def；修改 light_loop.sc 等公共 include 时需加 --force。完整 include 依赖跟踪在 SG-2。
- 重型 deferred variant 单次编译可能耗时数分钟；构建脚本会串行化并自动重试失败项，CI 需预留时间。

## 9. 对齐参考

- 表面契约：`shaders/material/surface_data_types.sc:6`、`shaders/material/surface_data.sc:15`、`shaders/passes/forward.fs.sc:17`。
- 光照 stage：`src/render/passes/forward_lighting.h:10-18/66`、`shaders/common/light_data.sc:20/37`、`shaders/lighting/shadows.sc:12/26/664`、`shaders/lighting/environment.sc:6-7`、`shaders/lighting/contact_shadow.sc:20`。
- 编译：`tools/shader_build.py:49/55/74`、`vfx/src/compiler/compiler.cpp:201/296`。
- 运行时：`src/gfx/resource_manager.h:56`、`src/gfx/encoder.h:36-38`、`src/render/vfx/hbg_vfx_backend.cpp:366`。
- 预览/窗口：`src/app/application.cpp:636/693`、`src/app/window.cpp:24/77`。
