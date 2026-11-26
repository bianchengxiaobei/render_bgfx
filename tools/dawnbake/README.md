# dawnbake tools

Dawn GI 离线接入的辅助工具。主计划见 [`../../docs/dawn-gi-plan.md`](../../docs/dawn-gi-plan.md)。

## golden

独立 C# 工具，直接编译 com.tencent.dawn 的协议源码（不依赖 Unity），用于：

- 打印 `Marshal.SizeOf` / `OffsetOf`，作为 C++ 结构体布局的基准；
- 生成 `.scene` / `.jobs` 黄金样本，供 C++ 序列化器做 byte-diff。

### 运行

```powershell
# 结构体尺寸/偏移表
dotnet run --project tools/dawnbake/golden -c Release -- sizes

# 空场景
dotnet run --project tools/dawnbake/golden -c Release -- write-empty <dir> <guid32hex>

# 最小场景：1 个 quad + 1 盏平行光 + 1 个 lightmap job
dotnet run --project tools/dawnbake/golden -c Release -- write-min <dir> <guid32hex>
```

默认引用 `..\..\..\..\dawnlight\com.tencent.dawn\Editor\DawnProtocol`，可用
`-p:DawnProtocolDir=<path>` 覆盖。

> `guid32hex` 是 4 个 uint32 按 `A/B/C/D` 顺序拼成的 32 位十六进制字符串。

## C++ 协议工具（D1）

```powershell
cmake --build --preset vs2022-debug --target hbg_dawnbake

# 结构体尺寸/偏移自检
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --proto-test

# mesh/instance 导出语义自检
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --mesh-test

# material 导出语义自检
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --material-test

# light 导出语义自检
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --light-test

# sky 导出语义自检
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --sky-test

# xatlas UV2 生成 / 烘焙布局自检（D3.1–D3.2）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --uv-test

# 实例尺寸 + AtlasNode pack + 100 块 stress 自检（D3.3–D3.8）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --atlas-test

# rect + UV2 chart 调试图（D3.7）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --dump-uv=out\uv_debug.png

# 完整 job 目录自检（临时目录，自动清理）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --package-test

# .lmgz 解析 / 查看
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --lmgz-test
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --inspect-lmgz out\v2.00000000000000000000000000000005.lmgz

# .sdfgz（ShadowMask）解析 / 绑定 / 批量校验
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --sdfgz-test
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --inspect-sdfgz out\v2.<jobguid>.sdfgz
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --verify-sdfgz out\dawn_shadowmask 11111111222222223333333344444444

# 超尺寸 lightmap split / merge 测试
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --split-test

# job 目录 -> sidecar 绑定 -> split merge -> atlas DDS（可选 edge padding，默认 2）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --job-atlas-test
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --write-job-atlas out\job <guid32hex> out\job\<guid>.lightmap.json out\atlas 2
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --verify-job-atlas out\job <guid32hex> out\job\<guid>.lightmap.json out\atlas

# D5.2 结果绑定校验（raw 或 gzip scene/jobs）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --lmgz-bind-test
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --verify-lmgz out\dawn_min 11111111222222223333333344444444

# D5.3/D5.4 incident+direction atlas 写入与块内膨胀（RGBA16F CPU 图）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --sh-atlas-test

# D5.5 DDS 写出（lm<N>_sh0/1/2.dds，bimg 解析校验）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --sh-atlas-dds-test

# D5.6 .lmgz -> DDS 逐 texel 校验（测试；真实文件用下面的 CLI）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --sh-atlas-dds-verify-test
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --verify-sh-atlas-dds <file.lmgz> <dds-dir> <atlas-index> <target-x> <target-y> <atlas-size>

# D5.7 合并 lightmap fragment 到场景 sidecar（省略输出路径时原地更新）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --sidecar-merge-test
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --merge-sidecar <scene.hdrp.json> <lightmap.json> [<out.hdrp.json>]

# 协议版本拒绝（scene/jobs 版本不在 1000..1002）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --protocol-version-test

# golden 字段级结构比对
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --golden-test tests\dawn\golden\min_scene

# 导出最小完整 job 目录
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-min out\dawn_min 11111111222222223333333344444444

# 生成与 C# golden 逐字节一致的最小场景
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --write-min <dir> 11111111222222223333333344444444

# D7.1 T2 UV V 轴校准场景（+X 红、+Z 绿、白地板）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-t2 out\t2 22222222333333334444444455555555

# D7.2 T1 单平行光校准场景（+Y 白、黑天空、albedo=1）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-t1 out\t1 33333333444444445555555566666666

# D7.3 点光 1/2/4m 衰减校准场景（法线正上方 R/G/B 三灯）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-falloff out\falloff 66666666777777778888888899999999

# D7.4 T1 线性色差校准场景（sRGB 0.8/0.6/0.4，强度 0.5）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-t1-color out\t1_color 55555555666666667777777788888888

# D7.5 T3 四块 2m 地板逐块导出（tile-index 0..3）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-t3-tile out\t3_tile0 0 44444444000000000000000000000001
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-t3-tile out\t3_tile1 1 44444444000000000000000000000002
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-t3-tile out\t3_tile2 2 44444444000000000000000000000003
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-t3-tile out\t3_tile3 3 44444444000000000000000000000004

# 通用 glTF 场景导出（自动读取同名 .hdrp.json、生成 UV2、atlas 和 sidecar）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-scene `
  assets\MyScene.gltf out\my_scene 11111111222222223333333344444444
# 也可用 --scene=<gltf> --out=<dir> 形式；--sidecar 显式指定，--all-objects 忽略 contributeGI
# --samples/--bounces 控制烘焙参数，默认 1024/4

# D7.6 T6 100 物件性能场景（10×10 个 2m 地板，64² lightmap，2048 atlas）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --export-t6 out\t6 44444444000000000000000000000006
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --t6-test

# D7.7 校准配置校验（config/dawn.json）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --dawn-config-test

# 实际 Agent 烘焙（100 tasks；按需启动 coordinator/DawnDeamon）；--atlas-out 烘完自动合并 split 并写 atlas
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --bake out\t6 44444444000000000000000000000006 `
  --agent http://127.0.0.1:5009 --dawn <Dawn.exe> --timeout 1200 --atlas-out out\t6_atlas

# 四个 .lmgz 合并为 256 atlas；edge padding=2，块内膨胀 4 次
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --write-tile-atlas out\t3_atlas 256 2 `
  out\t3_tile0\v2.00000000000000000000000000000005.lmgz 2 2 `
  out\t3_tile1\v2.00000000000000000000000000000005.lmgz 70 2 `
  out\t3_tile2\v2.00000000000000000000000000000005.lmgz 2 70 `
  out\t3_tile3\v2.00000000000000000000000000000005.lmgz 70 70

# .lmgz -> atlas DDS（<atlas-size> <x> <y>）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --write-lightmap-dds out\t2\v2.00000000000000000000000000000005.lmgz out\t2 2048 2 2

# 读 golden → 重写 → byte-diff
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --roundtrip `
  tests\dawn\golden\min_scene\11111111222222223333333344444444.scene `
  tests\dawn\golden\min_scene\11111111222222223333333344444444.jobs

# 查看文件头与对象数量
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --inspect <file.scene|scenegz|jobs|jobgz|mesh|meshgz|mat|matgz>
```

`.scenegz/.jobgz` 使用 gzip deflate（zlib；10B header/trailer 对齐 .NET `GZipStream`，未链接 zlib 时回退 stored block）；本地直读的 `.scene/.jobs` 同时保留。原版写入见 `FDawnHiveInterface.cs:1332-1339`。`--roundtrip`/`--inspect` 支持 stored/deflate gzip 及 mesh/material。官方 Agent 实跑已验证 deflated `.scenegz`/`.jobgz` 可被 DawnAgent/Dawn 读取并产出 `.lmgz`（`Dawn results: validated`）。

T2 校准结论：`uv2.y` 不取 `1-v`；Dawn `IncidentLighting` 与运行时 DDS/UV 行序一致。

T1/D7.2 结论：Dawn `SHVectorR` 是 Unity 方向贴图编码，不是 SH 系数；+Y 单平行光输出 `SHVectorR=(0.5,1.0,0.5,1.0)`、`IncidentLighting=1`。运行时层 0 存 `IncidentLighting`，层 1 存 `SHVectorR`，按 `dot(n, direction.xyz-0.5)/direction.w` 解码，缩放系数 1.0。

D7.3 结论：法线正上方 1/2/4m 三盏 intensity=24 点光，中心 `IncidentLighting=(23.6138,5.9734,1.4554)`，比值 0.253/0.0616（理论 0.25/0.0625）；有效辐射为 `Color * intensity / d²`。`Attenuation=1/range` 供 LightGridCalc，`FalloffExponent=1` 在 inverse-square 路径忽略，`LightPower` 为元数据，不额外缩放亮度；`exportLight` 字段与原版一致，无需 1.5× 等额外系数。

D7.4 结论：T1 彩色平行光（sRGB `(0.8,0.6,0.4)`、intensity 0.5）实烘中心 `(0.301884,0.159252,0.066417)`，解析线性预期 `(0.301914,0.159273,0.066434)`；最大通道误差 0.0259%，CIE76 ΔE=0.0032，<5%。保持线性 `IncidentLighting` → RGBAHalf → shader 采样，不做额外 gamma。

D7.5 结论：四块 2m 地板分别在 `(-1,-1)/(1,-1)/(-1,1)/(1,1)` 用全局点光 `(0,3,0)` 逐块实烘，再合并为 256 atlas；块间 payload 64、间隙 4，`edgePadding=2` + 4 次 3×3 膨胀填满 ring。相邻世界边界 `IncidentLighting` 最大相对差 **0.0129%**，18,496 padded texel 全部 mapped；D3D11 1280×720 debug lightmap 为连续渐变，无黑缝/串色。运行时截图仅将 lightmap 0.25× 以避免 debug view 白色裁剪，原始 DDS 未缩放。原版边界依据：`DawnDilateTexture.cs:24-99`、`DawnPackLightmapTask.cs:30-56`、`DawnLightmap2DImporter.cs:40-44`、`DawnEncodeTextures.cs:416-421`。

D7.6 结论：`--export-t6` 生成 10×10 共 100 个 2m 白地板、64×64 lightmap、2048 单 atlas；`packLightmapAtlases` 多次约 **1–4 ms**（完整写包 137ms）。官方 Agent 实烘 **100/100 tasks、wall 55,491ms**，100 个 `.lmgz` 的 `ExecutionTime` 合计 707.325s（多 Agent 并行）。运行时 texture array 为 1×2048²×3 层 RGBA16F，共 **96 MiB（100,663,296 B）**；100 blocks 合并后的 3 个 DDS 各 32 MiB payload。原版参考：`DawnLightmap2DExporter.cs:98` 的唯一 TaskGuid、`DawnPackLightmapAtlas.cs:87-224`、`DawnLightmapSizeCalculate.cs:11-17,32-44`、`DawnEncodeTextures.cs:148-149`、`DawnLightmap2DImporter.cs:53,74`；运行时见 `src/render/lightmap/lightmap.cpp:124-130,158-166`。

D7.7 结论：`config/dawn.json` 为 v1 校准配置，固化 UV2 不翻转、texelsPerUnit=6.0、单块上限 1024、atlas 2048/taskPackSize 512/splitLightmaps true/padding 2/max 2、方向层 3/RGBA16F、线性色空间、方向缩放 1.0、灯光单位系数 1.0、inverse-square 点光和 `LightPower` 不参与亮度。`--dawn-config-test` 严格校验 schema/未知字段/范围，并交叉检查 `LightmapUvOptions`、`LightmapSizeSettings`、`LightmapPackingSettings` 及 `LightmapAtlasOptions::dilationIterations` 关键默认值；T2/T6 导出读取该配置。字段依据原版 `DawnSettings.cs:122-131,827-834`、`DawnLightmapSizeCalculate.cs:11-17,32-44`、`DawnPackLightmapAtlas.cs:320-324`、`DawnLightmap2DExporter.cs:193`、`DawnLightmap2DImporter.cs:53,74`、`DawnEncodeTextures.cs:148-149`、`DawnLightExporter.cs:275-280,328,351-375`。`edgePadding` 仍由 `--write-tile-atlas` 显式传入。CTest `dawnbake_config`，D7.7 当时 dawnbake **24/24**、非 VFX 用例 **25/25**、全量 CTest **67/67**。

D7.8 参考路径对比：同一份官方 Dawn T2 烘焙结果分别按 Unity 原版导入语义（`DawnEncodeTextures.cs:148-149`：Incident RGBAHalf、Direction RGBA32）与 HBG 运行时（三层 RGBA16F、`baked_gi.sc:8-21`）解码；Unity 公式见 `EntityLighting.hlsl:328-358`。按 `Color32` 截断 + 线性采样模型，T2 128×128 方向 8-bit 量化最大编码差 `0.0039168`、解码最大绝对差 `0.0197983`、PSNR **58.39 dB**；HBG runtime 640×360 仅 3.76% 像素不同、最大 2 LSB（仅 1 像素 >1 LSB），PSNR **66.97 dB**。原版方向纹理导出 `.png` 后按 Unity `DirectionalLightmap` 导入，该类型资源 meta 为 `sRGBTexture: 0`，线性采样分支有强证据但依赖 Unity type setter 默认；PNG 字节路径与 Unity Editor 实机截图仍受 D0.12 阻塞。报告与图见 `docs/dawn-gi-unity-comparison.md`、`docs/images/dawn-d78/`。工具：

```powershell
py -3 tools\dawnbake\unity_reference_compare.py --dds-dir <t2-dir> --rect 2 2 128 128 --normal 0 1 0 --write-unity-png out\t2_unity_png --out docs\images\dawn-d78
```

脚本依赖 Python 3 的 `numpy` 与 `Pillow`；读取 `lm<N>_sh0/1/2.dds`，不重新烘焙，可选 `--write-unity-dds` 输出量化后的 DDS，或 `--write-unity-png` 输出原版导出路径的 8-bit 方向 PNG。


D3 UV2 使用固定 xatlas commit（`jpcy/xatlas@f700c779`），可用 `-DHBG_XATLAS_DIR=<local checkout>` 切到本地副本。`--uv-test` 验证 `resolution=0` 的 texel 单位语义、自动 atlas、统一归一化，以及空/无索引/零面积 mesh 的 `tryUnwrapLightmapUv()` 跳过告警；D2 exporter 喂入生成 UV2 时需 `normalizeUv2=false`。`--atlas-test` 验证原版 `DawnLightmapSizeCalculate` 尺寸公式、2048 AtlasNode pack（每块 padding 2）、单/双 atlas 缩放回退、超限报错和 100 块 stress；同时校验 task mapping 按原版升序 + AtlasNode 的确定性 offset。`--dump-uv` 输出 rect/UV2 chart PNG；`--export-min` / `--package-test` 还会写出 `<guid>.lightmap.json`，供 D5.7 `--merge-sidecar` 合并及 D6 运行时解析。

D5.1 `.lmgz` 读取按原版 `FLightmap2DOutput`：48B header → jobInputIndex → 3 个 PRT flag → Lights → 192B `FLightmap2DSample` 数组，无版本头。`--lmgz-test` 写入/读回合成 output；`--inspect-lmgz <file>` 打印 header/size/samples/lights。`gzipDecompress()` 在链接 zlib 时支持标准 deflate，否则只支持 stored block；原版 `DawnLightmap2DImporter.cs:9-23` 使用 `SerializeObject` 读取该文件。官方 Agent 实跑样本 `v2.00000000000000000000000000000005.lmgz` 已验证（约 43KB，64×64，4096 samples）。

D5.2 结果绑定：`lightmap_result.h/.cpp` 的 `validateLightmap2DResult()` 检查 `JobID/MeshID`、正尺寸、`HeaderInfo.Size` 与 `FLightmap2DInput.Size` 的 padding 关系（无 padding 时相等；有 `LIGHTMAP_FLAGS_PADDING` 时各边 +2，对齐原版 importer），以及 `LightmapData.size == Size.x*Size.y`；`bindLightmap2DResult()` 用 `JobID` 唯一匹配 `BakingJobInputs.lightmap2DJobs`、用 `MeshID` 唯一匹配 `SceneInfo.meshInstances[].header.guid`。`--verify-lmgz <job-dir> <guid32hex>` 支持 raw 或 gzip scene/jobs，逐个校验 `.lmgz`；`--bake` 返回成功前也会执行。原版尺寸断言参考 `DawnLightmap2DImporter.cs:30-44`。

D5.3 atlas：`lightmap_atlas.h/.cpp` 的 `buildLightmapAtlasImages()` 复用 D5.2 binding 和 D3 placement；D7.2 后层 0 写 `IncidentLighting`（RGBA16F），层 1 写 `SHVectorR`（方向+方向性），层 2 为零；目标原点按原版 `UVWidthPixel + 2*Padding` 规则取 `D3 data rect - padding`，`bIsMapped` 另存为 mask。`--sh-atlas-test` 覆盖 padded/unpadded、half 值、边界清零、mask、越界/atlas 索引/rect 尺寸/padding/重叠/空输出；CTest **22/22**（D5.3 时 19/19）。原版参考 `DawnLightmap2DImporter.cs:46-57,74` 和 `DawnEncodeTextures.cs:259-265,399`。

D5.4 块内膨胀：`LightmapAtlasOptions::dilationIterations` 默认 4；`buildLightmapAtlasImages()` 先把每个结果复制到块内临时缓冲，再按 `DawnDilateTexture.cs:24-99` / `DilateLightmap.compute:24-94,96-164` 的 3×3 邻域平均做 4 次迭代，使用 `bIsMapped` 作为有效性。方向贴图 alpha 是 directionality，不能沿用原版 LDR 的 alpha/black 阈值，因此膨胀复制 4 个分量、只在块内传播，避免跨 atlas 块串色；`mapped` 更新为膨胀后的 coverage。D7.5 另外增加 `LightmapAtlasOptions::edgePadding`：在 payload 外建立 ring 后再膨胀，供 `--write-tile-atlas` 写入 atlas；默认 0 保持 D5.4 旧行为。原版 PRT 分支本身没有调用膨胀，D5.4 按计划补在 SH blit 前。`--sh-atlas-test` 还覆盖 0/1/4 次迭代、平均值、alpha 保留和相邻块隔离；D5.7 后 CTest **22/22**。

D5.5 DDS：新增 `lightmap_dds.h/.cpp` 的 `writeLightmapAtlasDds()`，以及 `lightmapAtlasName()` / `lightmapAtlasDdsFileName()`；每个 atlas 输出 `lm<N>_sh0.dds`、`lm<N>_sh1.dds`、`lm<N>_sh2.dds`。`bimg::imageWriteDds` 使用 RGBA16F、`numMips=1`、`numLayers=1`、无 cube、`srgb=false`，实际写 DX10 头（148B）；incident/direction half 数据按 atlas 行序原样输出，不做 Y 翻转。原版 PRT 纹理同样是 `TextureFormat.RGBAHalf`、`linear=true`、无 mip（`DawnEncodeTextures.cs:170-175`），并通过 `SetPixels` 写块（`:257-265`）；原版 `DawnBakingResult.cs:481,541-562` 的 RGBHalf 结果保存为 EXR，没有 DDS，因此 D5.5 是运行时容器，保持相同 bits/行序。`filter=linear`、`wrap=clamp` 不是 DDS 文件元数据，由 D6 上传采样器设置。`--sh-atlas-dds-test` 覆盖 2 atlas / 6 文件、文件名、RGBA16F、无 mip、尺寸、raw bytes 比对，以及空 images/错误 channel 大小拒绝；CTest 新增 `dawnbake_sh_atlas_dds`，D5.7 后 **22/22**。

D5.6 逐 texel 校验：`verifyLightmapAtlasDds()` 按 `.lmgz` 的层 0 `IncidentLighting`、层 1 `SHVectorR`、层 2 零值对比 DDS 解码值，只检查 `bIsMapped != 0` 的 texel（避免 D5.4 膨胀改变 unmapped texel）；每个 texel 比较 RGBA 四个 half 分量，target 原点按原版 `D3 data rect - padding` 传入；v1 暂不校验 `PrtHQ/PrtLQ`（真实 D4.5 样本 `prtHQ=0`）。`--sh-atlas-dds-verify-test` 覆盖合成 `.lmgz` → DDS → 解码和错误分量检测；`--verify-sh-atlas-dds <file.lmgz> <dds-dir> <atlas-index> <target-x> <target-y> <atlas-size>` 用于真实文件手工校验。原版行序/块循环参考 `DawnLightmap2DImporter.cs:46-57,74`，`ToColor` 保留 `.w` 参考 `DawnImporter.cs:249-252`，`SetPixels` 参考 `DawnEncodeTextures.cs:257-265`。CTest 新增 `dawnbake_sh_atlas_dds_verify`。

D5.7 sidecar 合并：`mergeLightmapSidecar()` 读取已有场景 `.hdrp.json` 和 D3 `<guid>.lightmap.json`，校验 fragment 的 `lightmap.version/texelPerUnit/atlases/objects` 与 rect 边界，然后用 `ordered_json` 保留原有 `hdrp/materials/lights/objects/custom` 字段，只替换/新增根 `lightmap`。CLI `--merge-sidecar <scene.hdrp.json> <lightmap.json> [<out.hdrp.json>]` 省略输出时原地更新，并自动创建输出目录；`--sidecar-merge-test` 覆盖字段保留、stale lightmap 替换、原地/幂等、坏根对象、缺 lightmap、坏 JSON、越界 rect 和缺失场景。原版结果映射参考 `DawnBakingResult.cs:94-104,777-778` 和 `DawnLightDataAsset.cs:331-379`。CTest 新增 `dawnbake_sidecar_merge`，当前 **23/23**。

## 通用场景导出

```powershell
hbg_dawnbake --export-scene <scene> <out-dir> [guid32hex] `
  --sidecar <scene.hdrp.json> [--sky <hdr|exr>] [--sky-intensity 1.0] `
  [--sky-rotation 0] [--blender <blender.exe>] [--blender-timeout 600] --samples 1024 --bounces 4

# 或
hbg_dawnbake --scene=<scene> --out=<out-dir> --sidecar=<scene.hdrp.json>
```

- `.gltf/.glb` 走现有 cgltf；`.fbx/.obj/.dae/.3ds/.ply/.stl` 走 Parcel Assimp importer + `parcel_hbg::SceneRenderBridge`；`.blend` 用 Blender headless 转临时 GLB 后导出。Parcel 集成需 CMake `HBG_ENABLE_PARCEL=ON`、`HBG_PARCEL_ROOT`、`HBG_PARCEL_ENTT_ROOT`、`HBG_PARCEL_ASSIMP_ROOT`；Blender 路径可用 `HBG_BLENDER_EXECUTABLE`/`--blender`/环境变量；源 `.blend` 路径会转绝对路径后再交给 Blender。FBX 内嵌纹理通过 `aiScene::GetEmbeddedTextureAndIndex()` 导入（需 Parcel `cpp/src/import/fbx_importer.cpp` 的 embedded-texture 补丁）；`DecodeTextureAsset` 无法解码的内嵌格式（如 EXR）经 `ImportRequest.texture_decode_fallback` 交给 hbg 的 `TextureLoader`/bimg 解码。外部 PNG/TGA/JPG/HDR 走 Parcel，外部 EXR 走 `TextureLoader`/bimg 解码为 RGBA32F。`--parcel-test` 自动覆盖 OBJ、OBJ+PNG、FBX 内嵌 PNG/EXR、OBJ+EXR、STL/PLY/DAE/3DS 三角场景、Parcel sidecar flat sky、sidecar contributeGI 过滤、Assimp Directional/Point/Spot/Area Light、Point/Spot IES；配置 `HBG_BLENDER_EXECUTABLE` 时还会覆盖 BLEND→GLB 及同 stem 源 sidecar。
- sidecar 省略时读取 `<scene 同名>.hdrp.json`；Parcel importer 也会读取同名 `.parcel.json`。
- `objects[].contributeGI=true` 决定参与对象；sidecar 没有 GI 标记时默认导出全部不透明对象，`--all-objects` 强制全部。
- 自动为每个 mesh 生成 UV2（xatlas）、计算 lightmap 尺寸、打包 atlas，并写 `.scene/.jobs/.scenegz/.jobgz/.meshgz/.matgz` 和 `<guid>.lightmap.json`。材质含有效 emissive（texture 或非零 factor，`emissiveStrength>0`）时，对应 `MeshInstance` 写 `MESH_INSTANCE_FLAGS_USE_EMISSION_LIGHTING`，对应原版 `DawnMeshExporter` 的 BakedEmissive/light BVH 语义。
- `--sky <hdr|exr>` 可接入环境天空（equirect/cubemap strip），`--sky-intensity`/`--sky-rotation` 覆盖 sidecar；未提供且 sidecar 无 `sky` 时使用黑色占位。导出后按现有 `--bake --agent ...`、`--write-lightmap-dds`、`--merge-sidecar` 流程继续。
- sidecar `lights[]` 可选 `shadowTranslucency`（默认 1.0，0..1）、`castShadows`、`indirectMultiplier`（逐灯，默认 1.0）、`range`、`sourceRadius`、`sourceLength`、`lightSourceAngle`、`barnDoorAngle`/`barnDoorLength`、`instanceId`（写入 `LightGuid.D`，缺省按灯名 FNV-1a）、`bakeMode`/`lightmapBakeType`（`baked`/`mixed`/`realtime`，默认 `baked`）、`mixedMode`（`shadowmask`/`indirectonly`，默认 `shadowmask`）、`lightingChannel`（0..3）、`useColorTemperature`（默认 false）与 `colorTemperature`（默认 6570，clamp 到 1000..20000）、`shadowType`/`shadows`（`none`/`hard`/`soft`，默认 `soft`）以及 `enablePenumbra`/`penumbraTint`/`penumbraLuma`；`shadowType=hard/none` 时源半径/源长度/平行光角度按原版清零，`none` 同时清除 `CAST_SHADOW`；`useColorTemperature=true` 时按 HDRP Filter+Temperature 语义把 `CorrelatedColorTemperatureToRGB` 乘到灯色，再计算 direct/indirect/LightPower；`mixed + shadowmask` 写 `STATIONARY|DISTANCE_FIELD_SHADOW` 和 lighting-channel 位，`mixed + indirectonly` 只写 `STATIONARY`，`realtime` 不写 `DIRECT_LIGHTING*`；`range` 决定点光/聚光 `Attenuation=1/range`，`sourceRadius/sourceLength` 写入 `Dimensions`，`castShadows` 控制 `CAST_SHADOW` 位；area 灯可写 `type:"area"`、`shape`、`width`/`height`；`cookie` 指向相对 sidecar 目录的 2D cookie，`cookieScale`/`cookieOffset`/`cookieMode` 可选，导出为 `SceneInfo.cookieTextureDatas`、`IESTextureSlice` 和 `LightTextureMode=1`；`ies` 指向相对 sidecar 目录的 IESNA 文件，点光生成 64×64 octahedral cookie，聚光生成 8×8 径向 cookie；`falloff` 可写 `inverse-square`/`bakery-physical`/`bakery-fake`（默认 inverse-square），对应原版 `FalloffMode` 与 `NON_INVERSE_SQUARE_FALLOFF`/`BAKERY_FALLOFF`/`BAKERY_FAKE_FALLOFF`；Rect/Disc 按原版 `isInverseSquared` 视为 inverse-square，不置 `NON_INVERSE_SQUARE_FALLOFF`；`enablePenumbra=true` 时导出 tint/luma，关闭时按原版零初始化写 `(0,0,0,0)`/`0`。灯光数组按原版 `DawnLightExporter.ExportLights` 顺序输出：Directional → Sky → Local。
- 场景含 `mixed + shadowmask` 灯时，按原版 `GatherRelevantLightsCPU` 收集 `meshInstances[].relevantLights`（Directional 总有；局部灯按 bbox/bounds 相交；realtime 跳过），并按 `DawnLightmap2DExporter` 只在相关灯含 Baked 时保留 lightmap job 的 `DIRECT`；lightmap job 在 `lightmap.padding>0` 时写 `PADDING`（原版 `DawnLightmap2DExporter.cs:161,191` 的 `Allocation.Padding>0`），与 SDF job 一致。每个 mesh 生成一个 `SDFShadowJobs` 条目：`JobID=MeshGuid` 且 B=1、C=mesh index，`Lights/ShadowmaskLights` 为相关 mixed cast-shadow 灯 GUID，`Flags=BILINEAR_FILTER(+PADDING)`，`Size=lightmap size`，`UpsampleFactor=4`，`LightmapUVIndex=1`。
- 按原版 `DawnPackLightmapTask.cs:216-243` 对 SDF job 做 `SDFShadowGroupJobs` 打包：已入 lightmap group 的 SDF job 直接继承该 group 的 `GroupedSize` 与同序 `MappingOffsets`（保证 shadowmask 与 lightmap 使用同一布局）；未入组 job 按原版 `ShadowMaskTaskGroupSingle` 单 job 成组，`GroupSize=max(w,h)`、offset=(0,0)。单 job group 先按 max(w,h) 升序，再输出 packed group；`JobID=(102,1,index,0xFFFFFFFF)`。独立 `packSdfShadowGroups(jobs, packSize)` 保留为无 mapping group 信息的 AtlasNode 打包路径，供单测使用。同时写 `SdfShadowParameter`/`ShadowSettingParameter`（`bAllowSignedDistanceFieldShadows=1`）。
- lightmap job 同样按 `DawnPackLightmapTask` 生成 `MappingGroupJobs`：只有 padded cell（raw size + 2×PADDING）< `taskPackSize` 的 job 入组，`JobID=(0,0,index,0xFFFFFFFF)`、`GroupedSize=taskPackSize`、`MappingOffsets=(x,y,lightmap job index)`；超尺寸 job 不再单独发 `LIGHTMAP2D`，按原版 `DawnLightmap2DSplitTask` 生成 `MappingSplitJobs`（`config/dawn.json.lightmap.splitLightmaps` 默认 true）：`SplitSize=taskPackSize`、`OverlapSize=3`、`JobID=(splitGroupIndex,103,globalSplitIndex,lightmapJobIndex)`、`MappingOffsets=(ix,iy,lightmapJobIndex)`，步长 `taskPackSize-2*OverlapSize`；Agent 发 `LIGHTMAP2D_SPLIT`，结果仍为每个拆分任务 GUID 的 `.lmgz`。`--verify-lmgz` 按原版 `DawnLightmap2DSplitImporter.GroupSplitLightmapOutput` 规则合并：非首行/列跳过 overlap，末 tile 保留到边界，合并后按原 lightmap job 的 MeshID/Size 校验。`--agent-spec-test` 覆盖入组后跳过单个 lightmap、以及单个/group/split 三种任务；`--split-test` 覆盖 split JobID/offset/padding 和 merge 的 overlap/未覆盖/JobID 错误。
- `.sdfgz` 结果按原版 `FSDFShadowOutput`（48B header + custom `ShadowMaps`，每项 `LightID` + 12B `FSDFShadowInfo` 数组）读取；`--sdfgz-test` 覆盖 gzip 读写与校验，`--inspect-sdfgz <file>` 查看 map/mapped/distance，`--verify-sdfgz <job-dir> <guid32hex>` 按 JobID/MeshID 批量绑定。`ShadowMaps[i].LightID` 必须等于输入 `ShadowmaskLights[i]`，与 Dawn 实际输出一致；`--sdfgz-test` 覆盖双灯顺序及 LightID 不匹配拒绝。
- 真实 Agent 双灯验证（job6，两个 mixed shadowmask 平行光，lightmap 未分组时）：`--bake` 4/4 tasks，2 个 `.sdfgz` 各 2 maps；`v2.494E...0001.sdfgz` 50×50、5000 samples，`v2.494E...0100000002.sdfgz` 31×31、1922 samples；`--verify-sdfgz` 共 6922 samples/5972 mapped，map[0]=`E04E3F61`、map[1]=`B1EA6248` 与输入顺序一致。
- SDF group 打包真实验证（job7，`taskPackSize=256`，shelf 版本）：2 个 SDF job 合为 1 个 256×256 group，offset `(0,0,0)`/`(52,0,1)`，`tasks=3/3`；Dawn 仍输出逐 job 尺寸 50×50/31×31，`--verify-sdfgz` 6922 samples/5972 mapped。非打包分支（job8，512/311 lightmap）生成单 job group，GroupSize 分别 512/311、offset `(0,0,index)`。2026-11-26 后 packed group offset 改为继承 lightmap `MappingGroupJobs`（AtlasNode 升序），因此 job7/job9 的旧 offset 仅作历史记录，需用 Agent 复跑确认新布局。
- lightmap group 真实 Agent（job9，taskPackSize=256，双灯，shelf 版本）：lightmap 与 SDF 各 1 个 group，Agent 只发 2 个 group task，`tasks=2/2`、`Dawn results: validated`；回传 2 个 `.lmgz`（94,427B/17,704B）和 2 个 `.sdfgz`（655B/416B），`--verify-lmgz` 3145 samples/2729 mapped、`--verify-sdfgz` 6922/5972。新的 AtlasNode 升序布局待复跑。
- lightmap split 真实 Agent（job10，dense 512/311、taskPackSize=256、samples=4、splitLightmaps=true）：512 拆 9 个 tile、311 拆 4 个 tile，加 2 个 SDF group 共 `tasks=15/15`；`Dawn results: validated`，`--verify-lmgz` 合并为 512×512/262144 samples（split=9）和 311×311/96721 samples（split=4），mapped 262144/57820；`--verify-sdfgz` 514×514/528392 mapped、313×313/195938 mapped。第一次 Agent 启动仍可能 `HIVE_STARTUP_FAILURE`，同 job 重跑成功。2026-11-26 后两个单 job SDF group 按原版升序变为 311→512（旧版为 512→311），结果内容不变，待复跑记录。
- 新增 `--write-job-atlas <job-dir> <guid32hex> <lightmap.json> <out-dir> [edge-padding]`：读取 `.scene/.jobs` 与 sidecar，按 `scene.meshInstances[].header.uv2Offset/uv2MapSize` + sidecar object rect 匹配每个 lightmap job；结果统一经 `loadLightmap2DOutputs()` 合并 split tile，再调用 `buildLightmapAtlasImages()` 写所有 `lm<N>_sh0/1/2.dds`。匹配使用 raw `uv2MapSize`（padding job 的结果 header 为 raw+2，不能拿它匹配 sidecar）；默认 `edge-padding=2`（与原 atlas padding 一致），可选 0；带 `PADDING` 的 block 已含 1 texel 外扩，因此额外 ring 会按 atlas 可用边距 clamp（原版 packing padding 2 时通常为 1），非 padding block 仍严格报越界。`--verify-job-atlas` 逐 block 回读 DDS 校验。`--bake ... --atlas-out <dir>` 在结果校验通过后自动查找 `<guid>.lightmap.json` 并调用同一路径（对应原版 `DawnEncodeTextures` 的自动后处理）；`--job-atlas-test` 覆盖 sidecar 顺序无关匹配、split merge、padded split→atlas、DDS 回读和 post-bake 输出（2 atlases/3 blocks/8 split tiles）；真实 job10 生成 1 atlas/2 blocks/358,865 texels 并校验通过。
- sidecar object 读取新增 `readLightmapSidecar()`，要求 rect 为正方形并校验 atlas/rect 范围；`writeLightmapSidecar()` 改用 `ordered_json` 保留 object 插入顺序，job atlas 先按同序 index 匹配（多 atlas 可存在相同 rect），失败再按 `uv2Offset` 与结果尺寸唯一匹配，重复匹配会报错。`--job-atlas-test` 覆盖 2 atlas、相同 rect 跨 atlas 和 sidecar 顺序保持。
- sidecar 可写根 `sky`，与 `SceneSky`/原版 `DawnSkyLight` 对应：
  - `mode`: `skybox`（默认有 texture 时）/`trilight`/`flat`/`none`；`enabled` 可关闭。
  - `texture`: skybox 的环境图，相对 sidecar 目录；支持 2:1 equirect 和 6:1 cubemap strip。
  - `color`, `intensity`, `indirectMultiplier`, `indirectIntensity`, `rotation`, `shadowTranslucency`、`instanceId`（写入 sky `LightGuid.D`，缺省 `0x534B594C`）。
  - trilight 额外 `skyColor`/`equatorColor`/`groundColor`/`lerpExp`，按原版在 128² 六面上直接插值；flat 的 `SkyData.Color` 使用 `ambientColor`，`EnvCube` 按原版常量 `1/π`；`adaptRotation` 为原始 cubemap 适配旋转。
  - 导出时同时写 `FSkyLightInfo` 和原版 `DAWN_PT_LIGHT_SKY` 灯光条目（cast shadow + direct lighting，`Dimensions=MipDimensions`）。
  - 运行时 `hbg` / `--render-test` 在未传 CLI `--sky` 时会读取同一 sidecar 的 `skybox` 作为环境图（CLI 优先）；`trilight`/`flat` 仅用于烘焙。
- 测试：`--scene-export-test`，覆盖 sidecar `contributeGI` 过滤、flat/trilight sky、禁用 sky、skybox 缺图报错、`.scene/.jobs` 读回和包文件。

## Dawn 本地模式（重要）

```powershell
Dawn.exe -local -localdirectory <dir>\ <guid32hex> [-lm 0 -1 1]
```

- `<dir>\` **必须以反斜杠结尾**；Dawn 直接拼接 `<dir>\<guid>.scene` / `.jobs`。
- `.scene` / `.jobs` 不是 `.scenegz` / `.jobgz`：它们是**未压缩**的
  `int32 version(1002) + FSceneInfo/FBakingJobInputs`。
- `-lm start count round` 控制 lightmap job 范围；`0 -1 1` 表示全部烘一遍。
- 工作目录建议为 `Dawn.exe` 所在目录。
- 本机 Spike 记录与阻塞点见 `docs/dawn-gi-checklist.md` 的 D0 小节。
- 2026-11-19 验证：D2.8 生成的 raw scene/jobs 可被 Dawn 读取并完成 64×64 烘焙（`Lighting complete`，退出码 0）；本机需临时把 OIDN device type 从 CPU(1) 改为 CUDA(3) 绕过 CPU device 初始化失败。该 patch 只用于验证，不提交。local 模式未落盘 `.lmgz`，结果通道归 D4/D5。

## 直启驱动

```powershell
hbg_dawnbake --run-dawn <job-dir> <guid32hex> --dawn <Dawn.exe> [--timeout 600] [--log <file>] [--keep-job]
hbg_dawnbake --run-dawn <guid32hex> --job-dir <dir> --dawn <Dawn.exe>
hbg_dawnbake --bake <job-dir> <guid32hex> --dawn <Dawn.exe> [--timeout 600] [--result-grace 2000] [--keep-job]
hbg_dawnbake --bake <guid32hex> --job-dir <dir> --dawn <Dawn.exe>
```

- 校验 32 hex GUID、`Dawn.exe` 和 raw `<guid>.scene/.jobs`；工作目录强制为 `Dawn.exe` 所在目录。
- `localdirectory` 自动补尾反斜杠；stdout/stderr 合并写入 `<job-dir>/Dawn.log`，结果含日志尾部。
- `JobObject` 管理子进程树，超时默认 10 分钟并杀进程；`HBG_DAWN_EXE` 可替代 `--dawn`。
- `--job-dir` 可替代位置参数并自动创建缺失目录；默认只删除本次创建的临时目录，已存在目录保持不变；`--keep-job` 保留自建目录。未指定目录时默认根为 `%TEMP%/hbg_dawnbake/jobs/<guid>`。
- `--bake` 从 raw `.jobs` 的 `lightmap2DJobs[].jobId` 计算 `v2.<TaskGuid>.lmgz`，运行中每 100ms 轮询，退出后再等 `--result-grace`；失败码映射原版 `EBakingError`（超时 -5、非零退出 -6、结果缺失 -20）。
- `--bake` 输出 CI 可解析进度：`DAWN_PROGRESS phase=building|done|failed completed=N total=M percent=P`；`completed` 为新出现的 `.lmgz` 数，`total` 为 lightmap job 数。原版进度公式见 `Dawn4UnityWindow.cs:216-234`。
- `--process-test` 用 `cmd.exe` 验证退出码、日志捕获、超时杀进程、缺可执行文件和错误输入；`--result-test` 用 fake Dawn 验证结果成功/缺失/超时；`--job-lifecycle-test` 验证临时目录/保留/复用。
- 错误处理：缺 DLL 的 `ERROR_MOD_NOT_FOUND`、磁盘满的 `ERROR_DISK_FULL/ERROR_HANDLE_DISK_FULL` 会追加可操作提示；scene/jobs 版本不在 `1000..1002` 时在读取阶段报 `version ... is not supported`；`--protocol-version-test` 覆盖越界版本。
- 2026-11-19 local 实测：Dawn 退出 0、`Lighting complete`，但直启 local 模式未写出 `v2....lmgz`；`--bake` 正确报 `RESULT_IMPORT_FAILURE(-20)`。**2026-11-20 官方 Agent 路径已打通**，见下节；直启 local 仍仅用于读取/烘焙验证。
- 原版没有直接 spawn Dawn.exe：`JobManagement.cs:38` 只把 `%08X%08X%08X%08X` GUID 交给 Hive；结果名/目录来自 `ImportExport.cs` 的 `CreateChannelName` 与 `FDawnHiveInterface.OpenChannel`；job 目录参考 `OpenJob` / `Clear Cached Data`。

## Agent gRPC（可选）

```powershell
# 1) 启动本地 Coordinator mock（5009/5011 由 DawnDeamon/DawnAgent 自己监听）
dotnet run --project tools/dawn-coordinator -- --port 5010

# 2) 启动 DawnDeamon（5009），其 appsettings.json 已指向 http://localhost:5010
DawnDeamon.exe

# 3) 先导出 D2 包，再驱动官方 ClientAgent/Agent 作业流
hbg_dawnbake --export-min <job-dir> <guid32hex>
hbg_dawnbake --bake <job-dir> <guid32hex> --agent http://127.0.0.1:5009 --dawn <Dawn.exe>
# 远程 Agent 结果下载：作业仍 active 时经 5011 拉取缺失结果
hbg_dawnbake --bake <job-dir> <guid32hex> --agent http://127.0.0.1:5009 --agent-files http://127.0.0.1:5011 --dawn <Dawn.exe> --agent-download-results
# 强制重下已存在的本地结果（验证远端结果/SHA1）
hbg_dawnbake --bake <job-dir> <guid32hex> --agent http://127.0.0.1:5009 --agent-files http://127.0.0.1:5011 --dawn <Dawn.exe> --agent-download-results --agent-force-download
hbg_dawnbake --agent-upload http://127.0.0.1:5011 <job-id> <local-file> <remote-path>
hbg_dawnbake --agent-upload-test
hbg_dawnbake --agent-cache-upload http://127.0.0.1:5011 <job-id> <hash> <remote-path> [<hash> <remote-path> ...]
hbg_dawnbake --agent-multi-upload http://127.0.0.1:5011 <job-id> <hash> <remote-path> <local-file> [...]
hbg_dawnbake --agent-transfer-job http://127.0.0.1:5011 <job-id> http://other:5011 <remote-path> [...]
hbg_dawnbake --agent-download http://127.0.0.1:5011 <job-id> <remote-path> <local-file>
```

- 本地 Coordinator mock 位于 `tools/dawn-coordinator`，直接引用原版 `DAWNHiveCore.dll` 的 `CoordinatorServiceBase`，实现 `BeginMissionV2/CheckJob/EndMission/MissionPing` 等 RPC；无需改 DawnDeamon 配置。Dawn 插件不在默认 `..\..\..\dawnlight\com.tencent.dawn` 时，用 `dotnet build tools\dawn-coordinator -p:DawnRoot=<path>` 覆盖。

- proto 位于 `tools/dawnbake/proto`；开启选项后由 protoc/grpc_cpp_plugin 生成 C++，并编译 `dawn_agent_client.cpp`；post-build 自动拷贝 vcpkg 运行库到 exe 目录。
- 流程按真实 proto：`BeginMission → RunJob(JobFullDesc+TaskDescList) → GetJobResult(stream) → EndMission`；原清单中的 `BeginJobSpecification/AddTask/EndJobSpecification` 是本地 Hive 构造步骤，不是 ClientAgent RPC。
- `UploadFile` 已按原版 Agent `UploadFile` 实现：首包必须带 `jobID/filePath/fileSize/hash`（hash 为 SHA1 大写）；先等 `NotInCache` 再发 3 MiB 数据，`DoneInCache` 直接成功；空文件不发送数据包，最后等 `Done`。测试为 `--agent-upload-test` / `dawnbake_agent_upload`（含 >3 MiB 多分块），真实 Agent 上传 `hbg_upload_probe.txt` 日志 `Succeed recive file`；另在官方 active T6 job 上完成 6,291,456B 上传（334ms）并用 `DownloadFile` 回读，SHA-256 一致（704ms）。`BatchUploadFileInServerCache` 已按 `Agent.proto` 实现：请求只带 `UploadFileInfo.hash/filePath`，不带 `fileData`；要求 ack 数量与请求一致，接受 `Done/DoneInCache`，`NotInCache/Fail` 带路径和 message 返回。CLI 为 `--agent-cache-upload`，`--agent-cache-test` 用本地进程内 gRPC fake Agent 校验请求字段、成功、cache miss 和 per-file failure；真实 active job 上缓存命中 1 个文件，错误 hash 返回 `NotInCache`。`UploadMultiFiles` 另在真实 active job 上传 1 MiB + 2 MiB 两个文件（303ms），`DownloadFile` 回读 SHA-256 一致。
- `DownloadFile` 已按 `Agent.proto:58,155-173` 实现流式下载：`Data` 追加、`DataDone` 提交，且 `DataDone.fileData` 也必须写入（原版 Agent 在最后一块小于 buffer 时把数据放在 `DataDone` 里）；`Fail/Fail_FileNotFound` 带 message 失败。先写 `.part`，成功后再替换目标，失败不留半文件。CLI 为 `--agent-download`，`--agent-download-test` 用同一 fake Agent 覆盖分块成功、`DataDone` 带 payload、空文件、not found、fail；`DataDone.fileHash` 按原版 `FileCacheSystem.ComputeFileHashAsync` 的 **SHA1 大写十六进制**校验。`--bake ... --agent ... --agent-files ... --agent-download-results` 在 `runJob()` 收到 `D.O.N.E` 后、`EndMission` 前对缺失的 `.lmgz` 调用 `downloadFiles()`：优先 `DownloadMultiFiles` 批量下载，失败时逐个 `DownloadFile` 回退（此时 Agent 仍处于 current job）；已存在的本地结果跳过，加 `--agent-force-download` 则全部重下。原版 Agent 的 `DownloadFile` 只服务 `_curJobContext` 且校验 JobID，作业结束后单独调用会报 `doesn't match current job`。DownloadMultiFiles 也已按 DAWNAgent 2.4.5 实际协议补齐（Editor 旧 Agent.proto 未含）：DownloadFileAck.filename=5、DownloadMultiFilesReq{jobID,filePathes}、DownloadMultiFilesAck{errMsg,items}；支持多文件连续 Data/DataDone、逐文件 SHA1 校验、.part 原子提交。客户端 API 为 `AgentClient::downloadMultiFiles()`，测试 `--agent-multi-download-test` / `dawnbake_agent_multi_download`；`downloadFiles()` 自动选择多文件下载并在 RPC 不支持时回退单文件，测试 `--agent-download-files-test` / `dawnbake_agent_download_files`。原版 ClientAgent 自身也用该 RPC 回传结果（D4.5 日志 Got DownloadMultiFiles 1 files）；`RunJob` 的 GeneralAck 不含支持位，原版从 `BeginJobAck.isSupportDownloadMultiFiles` 判定，本客户端以 UNIMPLEMENTED/失败回退兼容旧 Agent。真实 DAWNAgent 2.4.5 已用 `--agent-download-results --agent-force-download` 强制重下：`v2...05.lmgz` 190,301B，Agent 日志 `Got DownloadMultiFiles 1 files`，客户端 SHA1 校验通过，`Dawn results: validated`。
- `UploadMultiFiles` 已按 `Agent.proto:143-152` 实现：每个 `UploadFileInfo` 内联 `fileData`，ack 数量必须匹配，接受 `Done/DoneInCache`，`Fail` 带 remote path + message。CLI 为 `--agent-multi-upload`，`--agent-multi-upload-test` 校验 hash/path/payload、成功和 per-file fail。
- `UploadJobFilesToAnotherAgent` 已按 `Agent.proto:61,176-180` 实现：发送 `jobID/anotherAgentAddress/filePaths`，解析原版 `GeneralAck` 的 `code/message`；CLI 为 `--agent-transfer-job`，`--agent-transfer-test` 校验目标 Agent、jobID、文件列表和失败 ack。
- 安装坑：vcpkg 自带 Ninja 1.13.2 会在 c-ares 的 try_compile 卡死；本次将 `downloads/tools/ninja-1.13.2-windows/ninja.exe` 换成 Ninja 1.11.1 后 c-ares 5.7 分钟通过，gRPC 全量编译约 58 分钟。
- 实测（lightmap 未分组时代）：本地 mock Coordinator 5010 + DawnDeamon 5009 + DawnAgent 5011，`--bake ... --agent http://127.0.0.1:5009` 输出 `DAWN_PROGRESS phase=done completed=4 total=4 percent=100.0`、`Dawn agent: NONE (0) ack=0 tasks=4/4`、`Dawn results: validated`；结果 2 个 `.lmgz` + 2 个 `.sdfgz` 经 Agent 下载回 client job 目录。
- gz-only 实测：job 目录删除 raw `.scene/.jobs`，仅保留 deflated `.scenegz`/`.jobgz`，Agent/Dawn 仍完成并产出 `.lmgz`；`buildAgentJobSpec()`/`expectedDawnResults()` 会自动回退 `.jobgz`。`spec.jobId` 按原版 `%08X` 大写，避免带 A-F 的 GUID 与 DawnAgent 当前 job 大小写不匹配；`taskID` 也用 `guidHex` 大写。`--agent-spec-test`（CTest `dawnbake_agent_spec`）覆盖 gz-only、lightmap+SDF group 任务、结果文件和大小写。
- 关键：lightmap 单任务 GUID 必须为 `(0,0,LightmapTask,5)`，`TaskDesc.Parameters="LIGHTMAP2D"`；lightmap group 为 `(0,0,index,0xFFFFFFFF)`、`TaskDesc.Parameters="LIGHTMAP2D_GROUP"`，结果文件为组内每个 job 的 `.lmgz`；lightmap split 为 `(splitGroupIndex,103,globalSplitIndex,lightmapJobIndex)`、`TaskDesc.Parameters="LIGHTMAP2D_SPLIT"`，结果文件为拆分任务 GUID 的 `.lmgz`；SDF group 使用 `(102,1,index,0xFFFFFFFF)`、`TaskDesc.Parameters="SHADOWMASK_GROUP"`，结果文件为组内每个 job 的 `.sdfgz`。入组/拆分的 lightmap job 不再发单个 `LIGHTMAP2D`；只发单个 `SHADOWMASK` 任务时 Dawn 不写 SDF 结果。`GetJobResult` 收到 `D.O.N.E` 后 cancel context 再 `Finish()`，避免偶发挂起。
- Agent task cost 按原版 `TaskManagement.cs` / `DawnLightmap2DGroupTask.UpdateCost` / `DawnLightmap2DSplitTask.UpdateCost` / `DawnShadowMaskGroupTask.UpdateCost`：单 lightmap=`w*h*samples`；lightmap group=`groupedSize²*samples*(bounces+1)`；lightmap split=`splitSize²*samples*(bounces+1)`；SDF group=`groupSize²*lightCount*samples*ShadowRayFactor*MaxUpSamplingFactor`，上限 `INT_MAX-1`。`--agent-spec-test` 覆盖单个 lightmap、lightmap group、lightmap split、SDF group 四种 cost。
- 默认 `HBG_BUILD_DAWN_GRPC=OFF`；当前 dawnbake CTest 为 **36/36**（含 `dawnbake_split`、`dawnbake_job_atlas`）；全量 CTest 当前 **148** 项尚未重跑（含 `hbg_vfx_asset_check`，建议 `--timeout 1200`）。`hbg_tests` 上一轮 **315/315**；当前工作区外部 `config/pipeline.json` 新增字段未被 `test_frame_settings.cpp` 识别，导致 `Shipped settings files load` 1 个失败，与本轮 dawnbake 改动无关。
