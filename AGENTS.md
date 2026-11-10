# hdrp-bgfx 工程约定

## 铁律：实现前必须先读源码

任何渲染功能（Pass、材质、光照、阴影、后处理、资产、RHI 封装）在动手实现之前，必须先阅读对应的源码，再写代码：

- HDRP 源码：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.high-definition\Runtime`
- HDRP Core 源码：`E:\Learn\Graphics\Packages\com.unity.render-pipelines.core\Runtime`
- bgfx / bx / bimg 源码：`E:\Learn\hdrp-bgfx\build\vs2022\_deps\bgfx-src`（或 vendored `third_party/bgfx`）

要求：

- 计划文档（`docs/implementation-plan.md`、`docs/m0-checklist.md`）只作索引，不能替代源码。
- 以源码的实际行为、数据布局、Pass 顺序、API 签名为准；发现文档与源码冲突，先修正文档，再实现。
- 每个任务在 `docs/m0-checklist.md` 中记录实际参考的 `file:line`；实现完成后回填。
- 不要凭记忆猜 HDRP/bgfx 的签名或常量；先 grep 源码确认。

## 构建与测试

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure
py -3 tools/idl_gen.py --check
.\build\vs2022\Debug\hbg.exe --backend=d3d11
.\build\vs2022\Debug\hbg.exe --backend=vulkan
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --gfx-test
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --asset-test=<path\to\scene.gltf>
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=<path\to\scene.gltf>
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<path\to\scene.gltf> --frames=3 --screenshot=out.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<path\to\scene.gltf> --frames=3 --render-scale=0.5 --screenshot=scale.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<path\to\scene.gltf> --frames=3 --screenshot=out.png --pipeline-settings=config\pipeline.json --frame-settings=config\frame_settings.json
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --render-test=<path\to\scene.gltf> --frames=3 --debug-light-data --screenshot=out.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --debug-view=normal --render-test=<path\to\scene.gltf> --frames=3 --screenshot=normal.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --debug-view=depth-pyramid --render-test=<path\to\scene.gltf> --frames=3 --screenshot=depth.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --camera-orbit --debug-view=motion-vectors --render-test=<path\to\scene.gltf> --frames=3 --screenshot=motion.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=<path\to\scene.gltf> --frames=3 --screenshot=ui.png
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=<path\to\scene.gltf> --frames=3 --debug-wireframe --no-shadows --screenshot=wire.png
```

交互模式：F1 显示/隐藏 Debug 面板，F12 截图到 `debug_ui_XXXX.png`，F11 由 RenderDoc 接管（未安装时只记录告警）。`--no-debug-ui` 可关闭面板，`--frames=N` 搭配 `--scene` 时在 N 帧后退出并支持最终截图。

修改 `config/uniforms/*.idl` 后运行 `py -3 tools/idl_gen.py`，同时更新 `src/gfx/generated/uniforms.h` 与 `shaders/include/generated/uniforms.sc`；构建时也会自动执行。

Shader 变体由 `tools/shader_build.py` 编译；默认 `HBG_BUILD_SHADER_TOOLS=ON` 构建项目内 shaderc。若使用外部编译器，配置 `-DHBG_SHADERC=<path-to-shaderc>`。修改 `.sc` 后构建目标 `hbg_shaders` 会按时间戳增量重编译并更新 `shaders/manifest.json`。UI pass 使用独立 `shaders/ui/varying.def.sc`；instanced forward/shadow 使用 `shaders/include/varying_instanced.def.sc`；wireframe pass 使用通用 varying def。

## 代码约定

- C++20；命名空间 `hbg`；类型 `PascalCase`，函数 `camelCase`，成员 `m_`，常量 `k_`。
- 坐标系：右手系 Y-up，CCW 为正面；投影按 `bgfx::getCaps()->homogeneousDepth` 处理深度范围。
- 资源句柄化；`gfx` 层之外禁止裸 `bgfx::*Handle`。例外：`src/app/debug_ui.cpp` 是 Dear ImGui 的 bgfx backend 适配层，允许直接使用 transient buffer/scissor/native submit；业务渲染路径仍必须走 `gfx`。
- GPU 常量结构由 IDL 生成；禁止手写 uniform 名字。ImGui 的 UI shader 不属于材质 uniform 管线，单独放在 `shaders/ui/`。
- 业务渲染路径必须通过 `render::RenderGraph` 声明 texture/buffer 读写、attachment 和 pass func；不要在 `ForwardRenderer` 里直接创建每帧 HDR/FBO。
- RenderGraph ViewId 约定：graph pass 从 8 递增，backbuffer 4094，DebugUI 4095；不要占用这些保留值。
- 静态能力放 `PipelineSettings`，每帧开关放 `FrameSettings`；`FrameSettingsField` 的 bit 索引必须与 HDRP `FrameSettings.cs` 一致。Renderer 使用前必须 `sanitize`，不能直接相信 JSON/调用方声称支持的功能。
- 修改 `FrameSettingsField` 或新增 capability 时，同步 `frame_settings.cpp` 的名称表、`pipeline.json`、测试与 `docs/m0-checklist.md` 的 HDRP `file:line` 引用。
- 相机矩阵/历史状态统一由 `render::ViewState` 提供；不要在 Renderer 里手算 view/proj/prevVP。jitter 默认关闭，启用后 `viewProj` 为 jittered，`nonJitteredViewProj`/`prevViewProj` 保持 non-jittered。每个 view 每次渲染只调用一次 `ViewState::update`，否则会覆盖上一帧历史。
- render/target 尺寸分离：Forward/FBO 用 `ViewState::renderWidth/Height`，backbuffer presentation 用 target 尺寸；`screenSize`/`rtScale` 由生成全局 CB 上传。
- `HistoryBuffer<T>` 的首帧 `previous == current` 且 `hasHistory=false`；第二帧起才对 temporal pass 有效。GPU history texture 由具体 pass 分配，不要假设 CPU 双槽自动分配 RT。
- 场景物体必须通过 `render::DrawList` 过滤/排序后提交；不要在 pass 里直接遍历 `RenderScene::objects`。Pass tag 用 `ShaderPassMask`，queue 用 HDRP `RenderQueueRange`，阴影队列使用 `kRenderQueueRangeAllOpaque`。
- opaque 使用 `kOpaqueSortingCriteria`，transparent 使用 `kTransparentSortingCriteria`；shadow list 不做相机 frustum cull；forward/shadow 的 index buffer 必须使用 submesh `indexOffset/indexCount`。
- **Prepass 顺序**：`Scene Clear → Forward Depth Prepass → Objects Motion Vectors → Camera Motion Vectors → Depth Pyramid → Sky → Forward Opaque → Transparent → Wireframe → Presentation`。Prepass 使用 `DepthOnly/DepthForwardOnly` + AllOpaque；forward opaque 在 prepass 可用时必须 depth Equal + 不写深度。
- **深度依赖**：prepass/pyramid/forward/camera/object-motion 都必须显式 `readTexture(depth)`，不能只 `setDepthAttachment`；否则 RenderGraph 会裁掉 Scene Clear，导致 prepass 读到未清理深度。
- **NormalBuffer**：`RGBA8`，RGB=HDRP `PackFloat2To888` 八面体法线，A=perceptual roughness；prepass FS 只写 normal attachment，深度由 attachment 自动写。法线 clear 用 `BGFX_CLEAR_COLOR`。
- **Motion Vectors**：格式 `RG16F`；物体 MV 读 `ObjectUniforms.prevModel` + `nonJitteredViewProj`/`prevViewProj`，depth Equal；相机 MV 用 `s_depthSource` 重建，有 object MV 时只填 depth==1 背景，无 object MV 时全屏覆盖。不要用 jittered VP 计算 MV。
- **Depth Pyramid**：`RG32F` min/max mip 链；bgfx raster 不要同时读写同一纹理的不同 mip，必须 scratch 中转；mip attachment 通过 `FrameBufferKey.mips`/`bgfx::Attachment`，shader 输入用 `Encoder::setTextureMip`。
- **LightList**：CPU 16×16 FPTL tile + 64×64 big-tile 两阶段；`R32_UINT` 纹理布局 `width=33*tileCountX`、`height=tileCountY`，每个 tile 首 dword 为 count，后接最多 32 个绝对 `GpuLightData` 索引。方向光不进入 tile list，shader 单独循环；punctual 由当前像素世界坐标投影到 tile 后 `texelFetch`；list 不可用时回退全 punctual。ForwardRenderer 必须在 resize/RT scale 变化时重建并上传，forward pass 显式 `readTexture(lightList)`。
- **Shadow v1**：方向光 atlas 2048²（4×1024 级联），punctual atlas 2048²（point 6×256 faces、spot 512，shelf packer）；`ShadowContext` 是 `RGBA32F` 8×32，8 texels/request，最多 32 requests。C++ 按世界到阴影矩阵列写入 texel 0..3；bgfx `mat4` 构造布局要求 shader 用 `transpose` 后 `mul(M, v)`，不要改回直接 `mat4(texels)`。方向光 stage 6、context stage 8、punctual stage 9；`shadowParams.x` 是 PCF filter mode，`shadowCascadeSplits` 是 view-depth 分割。每个 request 一个 graph shadow pass，按 request 设置 viewport/view/projection；point 的 shadowIndex 是 6-face base，spot 是单请求 base。glTF 点光/聚光默认 castShadows=true；无请求时 `LightBuilder` 保持 shadowIndex=-1。
- **Sky/IBL v1**：`SkyManager` 加载 equirect HDR/EXR 或 6:1 cubemap strip；独立 `RGBA32F` sky raw（≤2048×1024）与 IBL 1024×512×7mip 纹理，均为 2D equirect，U wrap/V clamp。`EnvironmentProcessor` 用 CPU GGX 32 samples/mip 生成 IBL mip，SH9 从 IBL base 积分；SH 为 Unity 7×vec4 packing，已预乘 SH basis + clamped-cosine + 1/π。forward/sky shader 使用 stage 10 `s_environmentMap`；无 sky 时绑 1×1 黑 fallback。Sky pass 必须在 opaque/transparent 前、depth pyramid 后；`FrameSettingsField::SkyReflection` 只关闭 IBL specular，不隐藏 skybox。不要改成采样未绑定纹理或把 SH 系数再次乘 basis。
- **最终输出 v1**：`PostProcessUniforms` 前缀 `u_postprocess_`；固定曝光用 EV100，默认 -1.263（等价旧 2× exposure）；自动曝光是最终 `tonemap` shader 内 4×4 center-weighted 瞬时测光，不再加 exposure graph pass。ACES 使用 HDRP `Color.hlsl` approximate，Neutral 使用 HDRP NeutralCurve，最终线性→sRGB；`FrameSettingsField::Postprocess`/`ExposureControl`/`Tonemapping` 门控。`--debug-exposure` 是 EV 覆盖，不是 M1.10 debug view。
- **Debug v1**：`--debug-view=off|light-data|normal|motion-vectors|depth|depth-pyramid|light-cluster|directional-shadow-atlas|punctual-shadow-atlas|albedo|roughness`；`DebugUniforms.depthParams` 为 near/far；DepthPyramid 固定 mip0。LightCluster 是 CPU 16×16 LightList tile 占用图，不是 HDRP ray cluster。方向/punctual shadow atlas debug 先清整张 atlas，再以 raw depth 对比度显示。Albedo/Roughness 通过 separate opaque material pass 重绘，依赖 depth prepass、跳过 sky/forward、透明物体不参与；不要改回从最终 color 采样。`--camera-orbit` 仅用于 render-test 验收，不要把它当运行时相机控制。
- **Camera motion**：camera motion pass 必须采样 `depthPyramid` 而不是同时作为 depth attachment 的 `depth`；D3D 下同一深度纹理同时作为 DSV/SRV 会读到错误值；远平面深度写零运动。
- **Shader 常量**：不要把 ACES/矩阵常量写成全局 `const vec3/mat3`；D3D shader 会读到未初始化值导致全黑。矩阵用函数内行向量 + `dot`（`hbgMulRows`），并保持 D3D11/Vulkan 双后端截图验证。
- DrawBatch 只在 material/mesh/submesh 连续时生成；opaque/shadow 可用 64B instance transform 合批，transparent 必须保持逐物体 back-to-front，不得实例化。实例程序/容量不可用时回退普通逐物体路径。
- 不写无关注释；但源码参考的 `file:line` 记录在文档/任务里。
