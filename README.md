# hdrp-bgfx

基于 **bgfx** 的 Windows 实时渲染器，用 C++20 复刻 Unity HDRP 的核心渲染概念和帧流程。项目以 glTF 场景为输入，当前重点验证 **D3D11 / Vulkan**、**Forward / Deferred**、GPU light list、阴影、后处理和体积雾。

> 这是一个学习/验证型渲染器，不是 Unity HDRP 的运行时替代品。HDRP 源码用作行为、数据布局和采样语义的参考。

## 功能

- **渲染路径**
  - Forward-only、Deferred-only、Forward + Deferred mixed
  - RenderGraph：transient texture/buffer、pass culling、view id、FBO 复用
  - HDRP 风格 FrameSettings / PipelineSettings JSON
  - Forward depth prepass、GBuffer、stencil 标记、Deferred Lighting
- **光照**
  - 1 个主方向光；总 light slot 当前为 32（有方向光时 punctual 最多 31）
  - CPU / texture / storage-buffer 三种 light-list 路径，buffer path 默认开启
  - FPTL 16×16 tile、64×64 big-tile、64×64×32 cluster light list
  - GPU light-list producer/consumer；失败自动回退 CPU
  - `lightListCapacity=32/64/128` 容量契约，默认 32；64/128 的 CPU reference 已完成，GPU shader 扩容在 M8.2
- **阴影**
  - 方向光级联阴影、point/spot atlas、PCF / PCSS
  - Per-light shadow settings、shadow cache、cascade fade、normal/peter-panning bias
  - Contact shadows（full/half resolution）
  - GPU shadow culling / indirect draw 为 opt-in 实验路径
- **屏幕空间效果**
  - SSAO、SSR、SSGI、DBuffer decals
  - 静态 reflection probes、sky/IBL、HDR/EXR equirectangular sky
- **体积雾**
  - Screen-space volumetric fog，低分辨率 raymarch + depth-aware upsample
  - Temporal reprojection、punctual light scattering/shadows、local fog volumes
- **后处理**
  - Fixed / Auto / Histogram exposure
  - ACES / Neutral / None tonemapping
  - Uber color grading、bloom、TAA、DOF、motion blur、FXAA
- **工具**
  - Debug UI、light cluster / shadow atlas / GBuffer / depth / motion vector 调试视图
  - `--screenshot`、`--render-test`、`--gfx-test`、`--asset-test`
  - IDL 生成 uniform block，shader variant 构建脚本

## 当前状态

- M0–M7 已完成并验证。
- M8.0 / M8.1 已完成：容量设置、CLI/JSON、IDL、CPU 32/64/128 reference、动态 stride、cluster 32/cell。
- M8.2 未开始：GPU shader 仍固定 33/32，`--light-capacity=64/128` 目前会警告并 clamp 到 32。
- `ctest`：**207 cases / 20,170 assertions**，IDL check 通过。
- D3D11 / Vulkan 的 32-light buffer、texture、CPU 三条路径对 M7 基线截图为 **0 diff**。

详细里程碑和验证记录见 [`docs/implementation-plan.md`](docs/implementation-plan.md) 和各 `docs/mN-checklist.md`。

## 环境要求

- Windows 10/11 x64
- Visual Studio 2022，安装 **Desktop development with C++**
- CMake **3.24+**
- Python 3（IDL 生成和 shader 构建推荐；缺少 Python 时会使用已提交的生成文件，但无法构建 shader）
- 支持 D3D11 或 Vulkan 的 GPU

首次配置时 CMake 会通过 `FetchContent` 拉取 bgfx、GLFW、cgltf、MikkTSpace、nlohmann/json、doctest。离线环境可设置 `HBG_BGFX_CMAKE_DIR`、`HBG_GLFW_DIR`、`HBG_CGLTF_DIR`、`HBG_MIKKTSPACE_DIR`、`HBG_NLOHMANN_JSON_DIR` 指向本地依赖。

## 构建

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
py -3 tools/idl_gen.py --check
```

可执行文件：

```text
build\vs2022\Debug\hbg.exe
```

Release 风格构建：

```powershell
cmake --build --preset vs2022-relwithdebinfo
```

## 运行

交互式加载 glTF 场景：

```powershell
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=C:\path\to\scene.gltf
```

Vulkan：

```powershell
.\build\vs2022\Debug\hbg.exe --backend=vulkan --scene=C:\path\to\scene.gltf
```

加载项目配置：

```powershell
.\build\vs2022\Debug\hbg.exe `
  --backend=d3d11 `
  --scene=C:\path\to\scene.gltf `
  --pipeline-settings=config\pipeline.json `
  --frame-settings=config\frame_settings.json
```

离屏渲染并截图：

```powershell
.\build\vs2022\Debug\hbg.exe `
  --backend=d3d11 `
  --render-test=C:\path\to\scene.gltf `
  --frames=3 `
  --screenshot=out.png
```

Deferred + 后处理示例：

```powershell
.\build\vs2022\Debug\hbg.exe `
  --backend=d3d11 `
  --render-test=C:\path\to\scene.gltf `
  --frames=3 `
  --deferred `
  --pipeline-settings=config\pipeline.json `
  --frame-settings=config\frame_settings.json `
  --volumetric-fog `
  --fog-profile=config\volumetric_fog.json `
  --screenshot=deferred.png
```

资源/渲染基础自测：

```powershell
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --gfx-test
.\build\vs2022\Debug\hbg.exe --backend=vulkan --gfx-test
```

完整命令行参数：

```powershell
.\build\vs2022\Debug\hbg.exe --help
```

常用参数：

| 参数 | 说明 |
|---|---|
| `--scene=<gltf>` | 交互式场景 |
| `--render-test=<gltf>` | 渲染指定帧后退出 |
| `--frames=<n>` | render-test 帧数，默认 3 |
| `--screenshot=<png>` | 最后一帧写 PNG |
| `--backend=d3d11\|vulkan` | 后端选择 |
| `--deferred` | 切换到 deferred |
| `--pipeline-settings=<json>` | PipelineSettings |
| `--frame-settings=<json>` | FrameSettings |
| `--render-scale=<0.1..1.0>` | 动态分辨率/RT scale |
| `--gpu-light-list` / `--no-gpu-light-list` | GPU light list 开关 |
| `--gpu-light-list-path=buffer\|texture` | light-list 存储路径 |
| `--light-capacity=32\|64\|128` | 容量请求；64/128 的 GPU 路径在 M8.2 前会回退 32 |
| `--no-light-cluster` | 强制走 FPTL，不使用 cluster |
| `--debug-view=<mode>` | depth / normal / motion / light-cluster / shadow atlas 等调试视图 |
| `--contact-shadows` | 开启 contact shadows |
| `--volumetric-fog` | 开具体积雾 |
| `--shadow-profile=<json>` | 加载阴影配置 |
| `--postprocess-profile=<json>` | 加载后处理配置 |
| `--sky=<hdr\|exr>` | 加载 HDR sky |

### Debug UI

- `F1`：显示/隐藏面板
- `F12`：截图到工作目录
- `F11`：RenderDoc capture（需要 `renderdoc.dll` 已加载；未安装时忽略）

## 项目结构

```text
src/
  app/           窗口、CLI、Application、DebugUI、gfx/render test
  asset/         glTF、材质、贴图、场景 sidecar 加载
  core/          日志、Profiler、数学库
  gfx/           bgfx 封装、Texture/Buffer/Uniform/Encoder/RenderGraph 基础
  render/
    camera/      ViewState、ResolutionGroup
    culling/     DrawList、frustum、sort key
    graph/       RenderGraph
    lighting/    LightBuilder、LightListBuilder、GPU light data
    passes/      ForwardRenderer 及 pass 记录
    pipeline/    FrameSettings、PipelineSettings
    postprocess/ SSAO/SSR/SSGI、Bloom、TAA、DOF、Motion Blur、Color Grading
    shadows/     ShadowManager、ContactShadow、ShadowSettings
    sky/         Sky/IBL、EnvironmentTexture、ReflectionProbe
    atmosphere/  VolumetricFog、LocalFogVolume
shaders/         .sc shader 源码、manifest、生成产物
config/          Pipeline/Frame/Shadow/PostProcess JSON、uniform IDL
tools/           idl_gen.py、shader_build.py
tests/           doctest 单元测试和 gfx test
docs/            里程碑清单、实现计划、验证记录
```

## HDRP 参考

实现时对照了 Unity HDRP Runtime 和 Core 的以下部分：

- `FrameSettings` 字段/位布局、Pipeline/Frame 配置
- `LightLoopDef.hlsl`、FPTL/big-tile/cluster list 读取语义
- `ShaderConfig.cs` 的 FPTL 容量与 cluster cell 上限
- `HDRenderPipeline.LightLoop.cs` 的 GPU light-list buffer 分配
- HDRP 的 ACES/Neutral tonemapping、PCSS、contact shadow、volumetric fog、color grading
- bgfx 源码中的 D3D11/Vulkan 绑定、compute、indirect 和 shader 编译路径

项目没有直接引入 HDRP 运行时代码；概念对齐和差异记录在 `docs/m*.md` 中。

## 已知限制

- 当前只重点验证 Windows D3D11 / Vulkan；不做 D3D12 / DXR / ray tracing。
- GPU 路径 effective light capacity 仍为 32；64/128 的 CPU reference 和容量契约已完成，GPU 扩容在 M8.2。
- Cluster 每 cell 上限固定 32，不实现 HDRP layered offset / global atomic append。
- 不实现 16-bit packed light index、light category、light layers、cookies/IES、area lights。
- GPU shadow request/atlas packing、async compute 仍是后续工作。
- 方向光目前只支持 1 个主方向光。
- 项目尚未添加 `LICENSE` 文件；使用或分发前请先确认授权。
