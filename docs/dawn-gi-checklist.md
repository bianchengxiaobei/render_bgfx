# Dawn GI 接入检查清单

> 主文档：[`dawn-gi-plan.md`](dawn-gi-plan.md)。本文件是唯一进度来源：做完一项就勾一项并写验收数据。
> 状态标记：`[ ]` 未开始 / `[x]` 完成 / `[!]` 阻塞（在行尾写原因）。

---

## 当前状态

- 阶段：**D4 完成（OnlyLocal + 本地 Coordinator mock 打通官方 Agent 作业流，产出真实 `.lmgz`）；D5.1–D5.7 完成；D6.1–D6.7 完成（D6.6/D6.7 T4/T5 已在 D7.2 方向贴图数据上复验）；D7.1 完成（T2 UV V 轴）；D7.2 完成（Dawn 方向贴图解码 + T1 单平行光标定）；D3 清单完成（D3.1–D3.9）；D2.1–D2.10 完成**；D1 已完成（gzip stored 已补）；D0 CPU OIDN 仍失败，CUDA 验证 workaround 已跑通
- 协议版本：`1002`（Dawn 插件 2.4.5，Dawn.exe 1.3.5.20241220）
- 最近更新：2026-11-20

---

## D0 Spike & 基础设施（3–5 天）

- [x] D0.1 无 Unity 环境：改用 `tools/dawnbake/golden`（C# 直接编译协议源码）生成 `.scene/.jobs`；官方 Unity golden 待有环境时补
- [x] D0.2 验证 `Dawn.exe` 直启：`-local -localdirectory <dir>\ <guid>` 可绕过 Agent，读取 `<guid>.scene` / `<guid>.jobs`（未压缩，版本头 1002）
- [x] D0.3 验证二进制：无需 `optix*.dll`（`dumpbin` 仅依赖 `nvcuda/vulkan-1/OpenImageDenoise`；OptiX 走驱动 `nvoptix.dll`）
- [x] D0.4 记录启动依赖：工作目录为 exe 目录；`-localdirectory` 必须以 `\` 结尾；`Dawn.data` 必需；OIDN device 插件必需
- [x] D0.5 工具骨架：`tools/dawnbake/golden`（C#）+ `tools/dawnbake/README.md`；C++ `hbg_dawnbake` 骨架并入 D1
- [x] D0.6 结构体尺寸/偏移表已生成（见附录 A），与 C# `Marshal` 一致
- [x] D0.7 黄金样本入库：`tests/dawn/golden/min_scene/`（`.scene` 1516B、`.jobs` 800B、`expected.json`）
- [!] D0.8 Spike 阻塞：Dawn 1.3.5 在本机 `DeviceResource` 阶段崩溃，详见下方「D0 阻塞记录」
  - 2026-11-19 验证 workaround：临时把 `oidnNewDevice(CPU=1)` 改为 `CUDA=3` 后，Dawn 可完成 D2.8 场景读取、64×64 lightmap 烘焙并正常退出；该修改仅用于验证，不进入交付。

**D0 出口条件**：驱动方式已明确（直启）；golden 可读；尺寸表已落表。**唯一阻塞是 Dawn 运行时环境问题，与协议/数据无关。**

### D0 阻塞记录（2026-11-19）

已确认的事实（用 cdb + 断点/栈取证）：

1. `Dawn.exe` 完整解析我们生成的 scene/jobs：场景统计、Baking Parameters、Jobs Range、材质/网格上传、OptiX 设备初始化全部通过。
2. 崩溃点在 `GPUBaking::DeviceResource::DeviceResource` → `OpenImageDenoiserDevice`：
   - `oidnNewDevice(CPU)` 返回 `0`，`oidnGetDeviceError` 返回 `error=0, msg=null`（设备静默失败）；
   - 构造函数随后**无条件**执行 `spdlog::warn("create OIDN denoiser msg:{},Error:{}", msg, err)`；
   - fmt v6 对空 `const char*` 抛 `format_error("string pointer is null")`，成为致命异常。
3. 补丁验证：NOP 掉该日志调用后，下一处 `oidnCommitDevice(null)` 继续抛异常 → 说明设备确实为 null，不是单纯日志问题。
4. 独立 OIDN 测试（同一 Binaries 目录，P/Invoke）`oidnNewDevice(CPU/CUDA)` 均成功；说明失败是 **Dawn 进程内状态**（DLL/插件加载、TBB 或 CUDA/OptiX 先后顺序）导致，待隔离。
5. Vulkan 校验层为 1.3.216（系统 Vulkan SDK），Dawn 使用 Vulkan 1.3.2xx 的 `VkStructureType 1000455000` / `VkDynamicState 1000455005`；校验层回调同样存在空字符串日志风险。**禁用校验层（重命名 `VkLayer_khronos_validation.json`）后该路日志消失，但 OIDN 阻塞仍在。**
6. 2026-11-19 进一步验证：把 `OpenImageDenoiserDevice` 构造里的 `oidnNewDevice(1)` 改为 `oidnNewDevice(3)`（文件偏移 `0xEE465`，`B9 01 00 00 00` → `B9 03 00 00 00`）可让 OIDN 走 CUDA device，Dawn 随即完成 D2.8 场景的烘焙；说明 CPU device 失败是进程内加载/TBB 相关，而不是协议或场景数据问题。

可选的下一步（按优先级）：

- [ ] D0.9 在**干净机器**（未装 Vulkan SDK、干净驱动）复测同一 golden，排除环境因素
- [ ] D0.10 向 Dawn 提供方索取修复版 `Dawn.exe`（1.3.5 的 OIDN 日志 bug 已定位）
- [x] D0.11 尝试官方 Agent 路径（`DAWNAgent.exe` + `ClientAgent.proto`），观察 Agent 是否设置了不同环境
  - `DawnDeamon.exe` 可在 5009 启动，并成功拉起 `DawnAgent` 监听 5011（日志 `Local agent launched.`）。
  - 已补本地 **Coordinator mock**（`tools/dawn-coordinator`，5010 h2c），官方 OnlyLocal 作业流已打通；详见 D4.5。
  - C++ gRPC 客户端已编译，详见 D4.4–D4.6。
- [ ] D0.12 在干净机器上补一份 Unity 官方 golden，与 C# golden 做 byte-diff


---

## D1 协议层（1.5–2 周）

### D1.1 基础类型

- [x] `FGuidInfo` / `FSHAHashInfo` / `float2/3/4` / `int2/3` / `uint2` / `Matrix4x4` / `TSHVec2`
- [x] 每个类型 `static_assert(sizeof(...))`，数值来自 D0.6 的实测表
- [x] `Writer` / `Reader`：整块写、数组头、版本头
- [ ] gzip 读写（zlib），与 `.scenegz` 等扩展名对应

### D1.2 场景结构

- [x] `FMaterialInfoHeader` / `FMaterialInfo`（五张 `FTexture2DInfo<FMaterialColor>`）
- [x] `FMeshElementInfo` / `FMeshInfoHeader` / `FMeshInfo` / `FMeshTexcoordBuffer`
- [x] `FMeshInstanceInfoHeader` / `FMeshInstanceInfo`
- [x] `FSceneInfoHeader` / `FSceneInfo`（含 partition 字段，v1 填 0 但必须序列化）
- [x] `FLightInfo`（176B 显式偏移）/ `FLightFullInfo` / `FLightProfileInfo`
- [x] `FSkyLightInfoHeader` / `FSkyLightInfo`
- [x] 版本分叉：`FMeshInfo` 的 `[1001] Index32BitBuffer` 条件

### D1.3 作业结构

- [x] `FBakingJobParameters` 及全部子参数（逐字段照抄，含 padding）
- [x] `FLightmap2DInput`
- [x] `FBakingJobInputs`：`Serialize_1_0_0` / `Serialize_1_0_1` 两套顺序
- [x] 未使用数组一律写空数组头（`int32 0`），不得省略

### D1.4 结果结构

- [x] `FLightmap2DOutputHeader`
- [x] `FLightmap2DSample`（192B，Pack=16；`SHVectorR/G/B` @0/16/32，`IncidentLighting` @48）
- [x] `FLightmap2DOutput`

### D1.5 验证

- [x] `--dawn-proto-test` 对 golden 每个文件做 读→写→byte-diff
- [x] 记录字节级差异（应为 0）
- [x] 将 `sizeof`/`offsetof` 表写入 `docs/dawn-gi-checklist.md` 附录 A

**D1 出口条件**：T0 通过（全部 golden round-trip 0 差异）。

### D1 结果（2026-11-19）

- `hbg_dawnbake --proto-test`：21 项 `sizeof` 全部 OK（含 `FLightmap2DInput` 160、`Lightmap2DSample` 192、`FBakingJobParameters` 232）。
- `hbg_dawnbake --write-min` 与 C# golden **逐字节一致**：`.scene` 1516B、`.jobs` 800B。
- `hbg_dawnbake --roundtrip`：两个 golden **0 差异**（读→写→byte-diff）。
- CTest 新增并通过：`dawnbake_proto_sizes`、`dawnbake_golden_roundtrip`。
- 已知未完成：gzip 目前只写/读 stored blocks（合法但无压缩），deflate 压缩待补；`.lmgz` 结果读取尚未用真实输出验证；`FBakingJobInputs` 中未使用的 job 数组只接受空数组。

---

## D2 场景转换（2–3 周）

- [x] D2.1 mesh 导出：顶点/法线/切线（**swap**：Tangent=cross(T,N), BiTangent=T）/索引 16/32
- [x] D2.2 submesh → `FMeshElementInfo`（StartIndex 以索引为单位）
- [x] D2.3 实例导出：双矩阵 + 包围盒 + `LightmapId` + `UV2MapSize/UV2Offset`
- [x] D2.4 材质导出：Diffuse/Transmission/Emissive/Normal/Specular 五张 RGBA8
- [x] D2.5 材质参数映射：BlendMode / bTwoSided / OpacityMaskClipValue / DiffuseBoost / EmissiveBoost
- [x] D2.6 灯光映射：Dir/Point/Spot/Rect + `Flags` 位编码 + `Dimensions` 语义
- [x] D2.7 天空 cubemap：equirect → 6 面 float4（原插件只写 base 面，`MipDimensions` 仅声明 mip 数）
- [x] D2.8 写出完整 job 目录（scenegz/jobgz/meshgz/matgz）
- [x] D2.9 用 golden 场景逐文件比对结构（字段级，不要求字节级）
- [x] D2.10 Dawn 能读取并开始烘焙（先不关心结果）

**D2 出口条件**：手搭 1 个三角面 + 1 盏平行光场景，Dawn 不报错并产出 `.lmgz`。当前 D2.1–D2.10 完成；Dawn 已读取并完成烘焙，但 local 模式未落盘 `.lmgz`，结果通道留到 D4/D5。

### D2 进度（2026-11-19）

- 新增 `tools/dawnbake/include/hbg/dawn/dawn_export.h` + `src/dawn_export.cpp`：
  - `exportMesh(const asset::Mesh&, ...)` → `FMeshInfo`；
  - `exportMeshInstance(const asset::Mesh&, const render::RenderObject&, ...)` → `FMeshInstanceInfo`。
- 照抄原代码：
  - 切线：`TangentBuffer = cross(T, N)`，`BiTangentBuffer = T`（`DawnMeshExporter.cs:773-775`）。
  - 索引：每三角形写 `V0, V2, V1`（`:802-817`）；16/32 位条件含总索引数 < 65535。
  - UV2：`CalculateUVBounds` 后归一化（`:759-787`，`MeshUtils.CalculateUVBounds`）。
  - Element：`PrimitiveCount = 三角形数`，`StartIndex += PrimitiveCount`（`:819-826`），`MaterialIndex = 0xFFFF`。
  - 实例：`LightmapId = MeshIndex`、`LODLevelInHLODScene = -1`、矩阵直拷（`DawnExporter.cs:178-186`）。
- `--mesh-test` 校验并通过：索引交换、切线/副切线、`MaterialIndex=0xFFFF`、`LightmapId`、矩阵与包围盒。
- `exportMaterial` 已按 `DawnMaterialExporter.cs` 实现：
  - `Format = ETextureFormat.UBYTE4 (19)`、`WrapMode=0`；Diffuse/Emissive 做 sRGB→linear，Normal/Specular 保持线性。
  - Emissive 先除 π，再按最大通道归一化，余量写入 `HeaderInfo.EmissiveBoost`（`DawnMaterialExporter.cs:391-401/1115-1151`）。
  - Specular 打包 `(0.5, roughness, metallic, ior=1)`；有 metallicRoughness 贴图时取 glTF 的 G=roughness、B=metallic（`:889-897`）。
  - BlendMode：Opaque=0 / Mask=1 / Blend=2；Mask 写 `OpacityMaskClipValue`，非 Opaque 写 Transmission（Mask 用 alpha，Blend 用 `rgb*(1-a), a=1-a`；`:948-1006`）。
  - `MATERIAL_FLAGS_TWOSIDE` / `MATERIAL_FLAGS_SURFACE_DOMAIN` 已写入。
- `--material-test` 校验并通过：颜色量化、Specular 打包、EmissiveBoost、Normal Y flip、Mask/Blend Transmission、sRGB→linear。
- 材质偏差：Normal 按 [0,1] 标准编码写出（原插件 `*2-1` 后会被 `ColorConverter` 截断，疑为 Unity 侧 bug）；常量贴图用 1×1（原插件用 `NormalTextureResolution` 正方形）。两者待 D6 画面校准确认。
- CTest 新增 `dawnbake_material_export`，材质阶段 4/4 通过。
- **D2.6 灯光导出**已按 `DawnLightExporter.cs` 实现（`exportLight`）：
  - Directional：`Normal=-forward`、`Dimensions.x=sin(0.5°*LightSourceAngle)`、`Attenuation=1`、bounds 全 0；额外 `NON_INVERSE_SQUARE_FALLOFF`（原逻辑对 Directional 会置位）。
  - Point：`Dimensions=(sourceRadius,sourceRadius,sourceLength)`、`Attenuation=1/range`、`FalloffExponent=1`、bounds=`center±range`。
  - Spot：`Shaping=(cosOuter,1/(cosInner-cosOuter))`、`FalloffExponent=8`、bounds 按锥体 tip+disc 扩展。
  - Area：Rect `Dimensions=(w,h,w*0.5)`；Disc `Dimensions=(w,w,range*2)`；`Shaping=(cos(barnDoorAngle),barnDoorLength)`；Tube 暂按 Rect 近似。
  - `Flags`：类型位 + `CAST_SHADOW` + `HAS_STATIC_LIGHTING`/`INDIRECT_LIGHTING`/`INDIRECT_LIGHTING_PROBE` + 可选 `DIRECT_LIGHTING`/`DIRECT_LIGHTING_PROBE`。
  - `LightPower` 按 `luminance(baseColor) * intensity * indirectMultiplier * indirectIntensity`，与原 `HDRColor.GetPower()` 一致。
- 坐标/角度约定：引擎灯光 forward 是 `-Z`；Dir 的 Dawn Normal 取 `+Z`，Point/Spot/Area 取 `-Z`。Engine `outerConeAngle` 已是半角（rad），不再乘 0.5；原 Unity `OuterConeAngle` 是整角（度）。
- 已记录偏差：`LightGuid.D` 用灯光名 FNV-1a（原插件用 `GetInstanceID()`）；`sourceRadius/sourceLength/directionalSourceAngle` 目前由 `LightExportOptions` 提供，默认 0（引擎 `Light` 暂无对应字段）；颜色默认按线性处理，可开 `srgbToLinear`。
- `--light-test` 校验并通过：Dir flags/normal/power、Point attenuation/bounds、Spot cone/bounds、Rect/Disc dimensions、Tube 回退、sRGB→linear、GUID 稳定性。
- CTest 新增 `dawnbake_light_export`，当前 5/5 通过。
- **D2.7 天空导出**已实现（`exportSkyLight`）：
  - 支持 equirect（2:1）和 6:1 cubemap strip；输出顺序 `+X,-X,+Y,-Y,+Z,-Z`，面内行优先。
  - 面方向严格照 `CubemapPixelCoordToDirection`（`:1058-1073`）；每个像素 RGB 除 π 后写入 `EnvCube`（`:719-792`）。
  - `SkyData.MipDimensions=(ceil(log2(cubeSize))+1, cubeSize, 0)`；原插件实际只写 base 6 面，不写 mip 数据，这里保持一致。
  - `SkyData.Color=color*intensity`，`IndirectLightingIntensity=indirectMultiplier*indirectIntensity`，`EnvColor` 可配。
  - 旋转按 Unity `Quaternion.Euler` 的 ZXY 顺序构造；`SkyData.transform` 按原代码双转置语义写 `transpose(rotation)`。
  - 无天空源时输出 1×1×6 全 0 占位；`cubeSize` 默认 512、上限 512。
- `--sky-test` 校验并通过：面数/尺寸、常量色采样、sRGB→linear、旋转矩阵、空天空占位、mip 数。
- CTest 新增 `dawnbake_sky_export`，天空阶段 6/6 通过。
- **D2.8 完整 job 目录**已实现（`writeDawnPackage`）：
  - 文件名严格照 `ImportExportUtil.CreateChannelName`（`ImportExport.cs:31-43`）：`v2.<GUID>.scenegz/jobgz`、`v2.<Index>.<MeshGuid><LOD>.meshgz`、`v2.<Index>.<MatHash>.matgz`。
  - 同时写本地直读用的 `<guid>.scene/.jobs`，便于 `Dawn.exe -localdirectory`。
  - `.scene` 保留完整 `Meshes/Materials/MeshInstances`，与 `FSceneInfo.Serialize` 一致；mesh/material 另写 cached 文件。
  - `Lightmap2DInput.Flags` 按 `DawnLightmap2DExporter.cs:189-194` 设 `DIRECT|INDIRECT|DIRECTION|BILINEAR_FILTER|BM_STATIC`；`PartialJobsTotalNum.NumLightmap2DJobs=1`。
  - `meshGuids` 用实例 GUID；`Lightmap2DInput.MeshID` 与实例 GUID 一致；`Lightmap2DInput.JobID` 按原版 `TaskGuid=(0,0,LightmapTask,EGUID_B_FOR_TEXTUREMAPPING=5)`，首个任务为 `(0,0,0,5)`。
  - gzip 目前用 **stored blocks**（无压缩但格式合法），已验证 .NET `GZipStream` 可解；后续可换 deflate。
- `--package-test` 校验并通过：scenegz/jobgz 解压后与本地文件一致、scene/jobs/mesh/material 重新解析无尾字节、meshId/flags/texture format 正确。
- CTest 新增 `dawnbake_package_export`，dawnbake 全量 8/8 通过；`--roundtrip`/`--inspect` 现支持 stored gzip 及 mesh/material。
- **D2.9 golden 字段级比对**已实现（`--golden-test <dir>`）：
  - 读取 `expected.json`，校验文件名/大小、协议版本、scene/jobs 全部关键字段、mesh/material/instance/light/sky/jobs 交叉引用，以及两个文件均无尾字节；当前 353 个字段检查。
  - 结构断言照 C# `tools/dawnbake/golden/Program.cs` 的 `BuildMinimalScene/BuildMinimalJobs` 与 `BakingSceneDefine.cs`/`BakingJobDefine.cs`，不是从 C++ writer 反推。
  - 发现 golden 是手搭协议样本，与 D2 导出语义存在预期差异：索引未交换（`0,1,2,0,2,3`）、`LODLevelInHLODScene=0`、材质 `Format=0`、灯光 `Flags=12352`（无 cast shadow/non-inverse）、天空 `MipDimensions=(1,1,1)`、`PartialJobsTotalNum=0`；D2.8 包按原导出器语义输出（交换索引、LOD=-1、Format=19、Flags 含额外位、sky z=0、partial=1）。
  - 新增 CTest `dawnbake_golden_structure`，8/8 通过。
- **D2.10 Dawn 读取 + 烘焙验证**（2026-11-19）：
  - 用 `--export-min` 生成 raw `<guid>.scene/.jobs` + scenegz/jobgz/meshgz/matgz，送入 `Dawn.exe -local -localdirectory <dir>\ <guid> -lm 0 -1 1`。
  - 日志确认完整读取：`Num Lightmap2D Jobs:1`、`Jobs Range Info`、材质/网格上传、`BakeLightmap2D() start`、`FLightmapStaticLaunch::Execute(Normal) ... launchSize(64,64)`，最后 `Lighting complete [Startup = 138 ms, Lighting = 6.92 sec]`，进程退出码 0。
  - 本机 CPU OIDN 仍失败；验证时使用**临时副本**把 `OpenImageDenoiserDevice` 的 `oidnNewDevice(CPU=1)` 改成 `CUDA=3`（文件偏移 `0xEE465` 的 `B9 01 00 00 00` → `B9 03 00 00 00`）。这是验证-only，不提交、不交付。
  - CUDA/CPU 差异说明 OIDN device 初始化与 Dawn 进程内 TBB/插件状态相关；干净机器/修复版 exe 仍需 D0.9/D0.10。
  - local 模式未在 job 目录生成 `.lmgz`；结果通道/落盘路径留到 D4/D5（Agent 或 direct launcher）确认。

- 待办：`UV2MapSize/UV2Offset` 仍由 `MeshExportOptions` 注入，D3 atlas 计算后填入；尚未接 `--export <gltf>`；D4/D5 结果通道待做。

---

## D3 UV2 + Atlas（1–2 周）

- [x] D3.1 引入 xatlas（CMake FetchContent），验证 `resolution=0` 语义
  - 依赖：FetchContent pin `jpcy/xatlas@f700c779`，`hbg_xatlas` 静态库；可用 `HBG_XATLAS_DIR` 指向本地 checkout。
  - 实测（2026-11-19，`--uv-test`，2m×1m quad，`texelsPerUnit=6`、`padding=2`）：`resolution=0` 产生 **一个自动尺寸 atlas**，`atlasCount=1`，13×7；输出 UV 是 **texel 单位**（raw max 12.5×6.5），不是 [0,1]。
  - 注意：当前 xatlas 没有 `ChartOptions::normalDeviation/normalSeam`；实际字段是 `normalDeviationWeight=2`、`normalSeamWeight=4`，C++ 包装按当前 API 暴露。
- [x] D3.2 每唯一 mesh 生成 UV2 + naturalSize
  - `unwrapLightmapUv()` 用位置/法线/索引跑 xatlas，输出 remap 后的 `asset::Mesh`（复制顶点属性、`texcoord1=UV2`、保留索引数/submesh 范围）及 `sourceVertex/uvTexels/uvMinTexels/uvMaxTexels`。
  - UV2 采用 **统一 scale**：`(rawUV - rawUVMin) / max(rawUVSpanX, rawUVSpanY)`，保留 chart aspect 与 texel density；不能用 per-axis 归一化，否则方 atlas 会拉伸。
  - `naturalSize = ceil(max(rawUVSpanX, rawUVSpanY))`（xatlas texel 单位，不含 atlas 外围 padding）；测试 quad 为 12。
  - `MeshExportOptions::normalizeUv2` 新增；D2 exporter 默认仍 per-axis 归一化，喂入 xatlas UV2 时必须设 `false`（`--uv-test` 已验证）。
  - `--uv-test` 已加入 CTest；dawnbake 当前 **10/10**。
- [x] D3.3 实例尺寸：`texelPerUnit * scaleInLightmap` + clamp [4, 1024]
  - `calculateLightmapSize()` 照 `DawnLightmapSizeCalculate.cs:11-17`：`Resolution=max(0, texelPerUnit*scale*LODScale)`，`TexelSize=Resolution*max(uvW,uvH)*sqrt(area/(uvW*uvH))`，再 floor/clamp。
  - `uvW/uvH` 用 xatlas raw texel bounds（不是 per-axis 归一化 UV）；`LODScale=0.5^lod`；`padding` 时上限先减 2（与原代码一致）。
  - 实测：2m×1m quad / 6 texels/m → 12；scale×2 → 24；LOD1 → 6；最小 clamp 4；padding 时最大 1022，unpadded 1024。
- [x] D3.4 shelf packer：降序、2048、padding 2
  - `packLightmapSquares()` 按 `S_i` 降序（同尺寸按输入索引稳定排序）做 shelf pack；默认 atlas 2048。
  - `padding=2` 为**每块四周**留白，块间净距 4 texel；rect 为实际光图块 `(x,y,S,S)`，不含 padding。
  - 溢出时返回 `false`（D3.5 负责二分缩放/第二 atlas）。
  - `--atlas-test` 覆盖尺寸公式、降序打包、无重叠/越界、确定性、空输入与溢出；CTest **10/10**。
- [x] D3.5 溢出处理：二分缩放（下限 0.5）→ 第二 atlas → 报错
  - `packLightmapAtlases()` 策略：先用 1 张 atlas 满分辨率；失败则在 `[0.5,1]` 二分缩放；仍失败再尝试 2 张；2 张也失败则报错。
  - 缩放使用 `ceil(scale*S_i)` 且单块最小 4；所有块同一次打包共用 scale，保证 sidecar 的 `st` 简单。
  - 原版 `DawnPackLightmapAtlas.cs:59-85` 的 `LowerScale` 是 **0.1**；v1 按计划收紧到 0.5，可通过 `LightmapPackingSettings::minScale` 覆盖。
  - 实测：1400/700/700 只能一张时自动缩到约 0.97；5×1500 需要 2 张并缩到约 0.68；9×1500 超出 2 张上限报错。
  - `--atlas-test` 已覆盖满分辨率、单 atlas 缩放、双 atlas 缩放、超限失败、空输入、零尺寸；CTest **10/10**。
- [x] D3.6 sidecar 输出：`lightmap.atlases` + `objects[].rect/st`
  - 新增 `LightmapSidecar` / `writeDawnPackage(..., &lightmap)`，输出 `<guid>.lightmap.json`（根对象 `lightmap`，可直接合并进 `.hdrp.json`）。
  - `st = (S/A, S/A, x/A, y/A)`；rect 用不含 padding 的实际块；同时把 `FMeshInstanceInfoHeader.UV2MapSize/UV2Offset` 对齐到同一 rect。
  - 当前最小场景：`lm0` 2048、`Quad` rect `[2,2,64,64]`、`st=(0.03125,0.03125,0.0009765625,0.0009765625)`、`scale=1`、`contributeGI=true`。
  - `--package-test` 解析 JSON 并校验 atlas/object/rect/st 与 scene instance 的 UV2 字段，CTest **10/10**。
  - 注意：该文件是独立 fragment；D5.7 已提供 `--merge-sidecar` 合并进已有 `.hdrp.json`，D6 才由运行时解析。
- [x] D3.7 `--dump-uv` 调试图：rect/chart 线框
  - 新增 `writeLightmapUvDump()`，用 bimg 输出 PNG；参考原预览 `DawnLightmapPackingPreviewWindow.cs:958-1063/1071-1187` 的矩形填充、边线、UV2 黄色线框与 Y 翻转。
  - `--dump-uv=<out.png>` 用内置 1m cube 跑 xatlas（32 TPU）、尺寸计算和 2048 atlas，输出 1024px 调试图；实测 `rect=[2,2,95,95]`、`charts=6`。
  - `--dump-uv` 加入 CTest，dawnbake 当前 **11/11**。
- [x] D3.8 100 块方块压力测试：无重叠、无越界、padding 正确
  - `--atlas-test` 增加 100 块（16–128px）stress，复用 `verifyPacking()` 检查每块尺寸、atlas 边界、padding 与两两不重叠；当前全部落入 1 张 atlas。
  - CTest `dawnbake_atlas_pack` 通过，dawnbake **11/11**。
- [x] D3.9 退化网格处理：零面积跳过 + 警告
  - 新增 `tryUnwrapLightmapUv()`：空顶点、无三角形、索引越界、零表面积、xatlas 失败均返回 `false`，通过 `HBG_LOGW` 告警，调用方可直接跳过该对象。
  - 参考原版空 UV2 跳过告警 `DawnMeshExporter.cs:531-536`，以及零 UV 面积处理 `DawnLightmapSizeCalculate.cs:27-30`；v1 对零面积明确跳过。
  - `--uv-test` 覆盖空 mesh、无索引、零面积三种 skip 和有效 mesh 不 skip，CTest **11/11**。

**D3 出口条件**：T3 的打包部分通过；sidecar 已输出，运行时读取待 D6。

---

## D4 作业驱动（3 天–3 周）

- [x] D4.1 直启路径：工作目录、GUID 参数、超时、杀进程、日志收集
  - 新增 `dawn_launch.h/.cpp`：`buildDawnArguments()` 生成 `-local -localdirectory <dir>\ <guid32hex>`，GUID 固定 32 hex（对应原版 `FGuid.A/B/C/D` 的 `%08X` 拼接，`JobManagement.cs:38`）；工作目录为 Dawn.exe 目录。
  - 启动前校验 Dawn.exe、raw `<guid>.scene/.jobs`；`CreateProcessW` 不经过 shell，stdout/stderr 合并写入 `<job-dir>/Dawn.log`（`--log` 可覆盖），日志尾部进入结果。
  - `JobObject + JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 管理子进程树；超时 `TerminateJobObject`，默认 10 分钟，`--timeout` 可配。
  - CLI：`--run-dawn <job-dir> <guid32hex> --dawn <Dawn.exe> [--timeout N] [--log file]`，也支持 `HBG_DAWN_EXE`；`--process-test` 覆盖退出码、日志、超时杀进程和非法 GUID/缺输入校验。
  - 实测（2026-11-19，D0 CUDA workaround exe）：D2.8 场景 `exit=0`、`elapsed=6791ms`，日志含 `Lighting complete`；直启 local 不落 `.lmgz`（官方 Agent 路径见 D4.5）。
- [x] D4.2 结果轮询：`.lmgz` 出现 + 进程退出 + 失败码映射（驱动侧）
  - 结果名按原版 `ImportExportUtil.CreateChannelName`：`v2.<TaskGuid A..D %08X>.lmgz`，路径为 job 目录；`--bake` 从 raw `.jobs` 的 `lightmap2DJobs[].jobId` 生成期望列表。
  - `runDawnJob()` 在进程运行期间每 100ms 轮询结果文件，进程退出后再等 `--result-grace`（默认 2s）；用启动前 mtime/size 基线排除旧文件。
  - 失败码照 `LightingSystem.cs` 的 `EBakingError` 映射：超时 `JOB_KILLED(-5)`、非零退出 `JOB_FAILURE(-6)`、结果缺失 `RESULT_IMPORT_FAILURE(-20)`、启动/输入错误 `HIVE_STARTUP_FAILURE/CHECK_SCENE_FAILURE`。
  - `--result-test` 用 fake Dawn 覆盖成功、结果缺失、超时三条路径；CTest **17/17**。
  - 真实 local 烘焙：Dawn 退出 0 且完成 `Lighting complete`，但直启 local 不生成 `.lmgz`；官方 Agent 路径（D4.5）已生成 `v2.00000000000000000000000000000005.lmgz` 并回传。
- [x] D4.3 `--keep-job` / `--job-dir` 生命周期
  - 新增 `JobWorkspace`：`--job-dir <dir>` 可替代位置参数；目录不存在则创建，默认 job 根为 `%TEMP%/hbg_dawnbake/jobs/<guid>`。
  - 默认只清理**本次创建**的临时 job 目录；已存在的目录永不自动删除；`--keep-job` 保留本次创建的目录。`--job-dir` 同时支持 `--job-dir=<dir>`。
  - 参考原版 `OpenJob` 在 `TLBSCache/Jobs/<GUID>` 建目录（`FDawnHiveInterface.cs:537`）、`CloseJob` 不删目录（`:713`），以及显式 `Clear Cached Data` 删除缓存（`Dawn4Unity.cs:768-801`）；v1 采用更安全的“只删自建临时目录”策略。
  - `--job-lifecycle-test` 覆盖自动删除、`--keep-job` 保留、已存在目录保留、默认临时根；CLI 实测 `--run-dawn ... --job-dir` 缺输入时默认清理、`--keep-job` 保留，CTest **17/17**。
- [x] D4.4（备选）gRPC 客户端：`ClientAgent.proto` 生成 C++
  - 已复制 `DAWNCommon.proto` / `ClientAgent.proto` / `Agent.proto` 到 `tools/dawnbake/proto`，新增 `HBG_BUILD_DAWN_GRPC` 可选 CMake 生成/编译路径（protoc + grpc_cpp_plugin）。
  - 已实现 `dawn_agent_client.h/.cpp`（pimpl，反射填 proto，避免生成字段命名差异）：`ClientAgentService` 的 `BeginMission/RunJob/GetJobResult/EndMission`，以及 `AgentService.UploadFile`。
  - vcpkg 安装：abseil/c-ares/protobuf/re2/grpc 1.81.1/protobuf 6.33 已装。关键坑：vcpkg 自带 **Ninja 1.13.2 在 c-ares try_compile 卡死**，换成 Ninja 1.11.1 后 c-ares 5.7 分钟通过；gRPC 全量 DEBUG+REL 约 58 分钟。
  - `HBG_BUILD_DAWN_GRPC=ON` + `CMAKE_PREFIX_PATH=E:\Dev\vcpkg\installed\x64-windows` 编译通过；post-build 自动拷贝 gRPC/OpenSSL/protobuf/abseil/re2/c-ares/zlib DLL。**CTest 14/14**。
- [x] D4.5（备选）gRPC 作业流：OnlyLocal 官方 Daemon/Agent 实跑成功
  - 代码按真实 proto 实现 `BeginMission → RunJob(JobFullDesc+TaskDescList) → GetJobResult(stream) → EndMission`；原清单中的 `BeginJobSpecification/AddTask/EndJobSpecification` 是本地 Hive 构造步骤，不是 ClientAgent RPC。
  - 已确认 **DawnDeamon 能正常拉起 DAWNAgent**（5011 监听，日志 `Local agent launched.`）；此前直接运行 `DAWNAgent.exe` 的 `NETSDK1099` 不是官方启动路径，不能再作为 Agent 启动失败依据。
  - 已补 `tools/dawn-coordinator`：直接引用原版 `DAWNHiveCore.dll` 的 `CoordinatorServiceBase`，实现 `RegisterAgent / AgentPing / CheckJob / BeginMissionV2 / BeginJob / JobPing / EndMission / MissionPing` 等本地 mock，监听 `http://0.0.0.0:5010`（h2c）。
  - 运行方式：`dotnet run --project tools/dawn-coordinator -- --port 5010`；DawnDeamon 读 `appsettings.json` 的 `DAWN:CoordinatorAddress=http://localhost:5010`，无需改客户端配置。
  - 实跑（2026-11-20）：`DawnDeamon` 5009 → 本地 `DAWNAgent` 5011 → 官方 `Dawn.exe` 烘焙；`DawnDeamon` 日志 `Got task result from Agent`、`Task ... done`、`job ... succeed`，结果经 `DownloadMultiFiles` 回传到 client job 目录。
  - 关键协议修正：lightmap 任务 GUID 必须按原版 `TaskGuid = (0,0,LightmapTask,EGUID_B_FOR_TEXTUREMAPPING=5)`，任务 `Parameters = "LIGHTMAP2D"`；此前用 `(jobIndex,0,0,0)` 导致 Dawn `Job Not Processed`。C++ `buildMinimalJobs` / golden 已改为 `(0,0,0,5)`。
  - C++ `--bake ... --agent http://127.0.0.1:5009` 实测输出 `DAWN_PROGRESS ... done completed=1 total=1 percent=100.0`、`Dawn agent: NONE (0) ack=0 tasks=1/1`、`Dawn results: validated`；真实结果 `v2.00000000000000000000000000000005.lmgz`（约 43KB），`--inspect-lmgz` 解析 `samples=4096`、`size=64x64`。
  - 本轮仍使用 D0 的 CUDA workaround `Dawn.exe`（OIDN CPU 初始化失败未修复），仅作 D4/D5 验证，不进入交付。
  - `GetJobResult` 收到 `D.O.N.E` 后调用 `TryCancel()` 再 `Finish()`，避免 server stream 已成功但客户端 `Finish` 偶发挂起。
- [x] D4.6（备选）文件上传
  - 已实现 `AgentClient::uploadFile()` + `--agent-upload`，走 `AgentService.UploadFile` 流式分块（64 KiB，最后一包 `fileDataDone`），校验 ack `Done/DoneInCache`；编译通过。
  - `BatchUploadFileInServerCache` 未实现；本地 Agent 共享 job 目录时不需要上传。本轮官方 Agent 作业中 `UploadMultiFiles` 已实际收到 6 个 job 文件（含 scenegz/jobgz/meshgz/matgz），上传路径验证通过。
- [x] D4.7 进度输出（百分比/任务数），可被 CI 解析
  - `--bake` 输出稳定前缀 `DAWN_PROGRESS phase=building|done|failed completed=N total=M percent=P`；`total` 为 `lightmap2DJobs` 数，`completed` 为已出现的新 `.lmgz` 数。
  - `runDawnJob()` 新增 `onProgress(completed,total)` 回调，只在完成数变化时触发；CLI 每行 flush，CI 可按 `DAWN_PROGRESS` 前缀抓取。
  - 原版 BUILDING 进度为 `m_RecieveTaskCount / NumOfTaskExported`（`Dawn4UnityWindow.cs:216-224`），IMPORTING 为 `NumOfTaskImported / NumOfTask`（`:232-234`）；计数器定义见 `TaskManagement.cs:186-216`，接收计数递增见 `FDawnHiveInterface.cs:770-794`。直启模式无 gRPC 回调，v1 用结果文件数近似完成任务数。
  - 真实 local：`completed=0 total=1 percent=0.0` → `phase=failed`，与 `RESULT_IMPORT_FAILURE(-20)` 一致；官方 Agent 路径已输出 `phase=done`。CTest **17/17**（完整）。

**D4 出口条件**：一条命令完成 场景 → 烘焙 → 结果文件，失败有清晰错误码。

---

## D5 结果导入（3–5 天）

- [x] D5.1 `.lmgz` 解析（gzip + `FLightmap2DOutput`）
  - 原版入口：`DawnLightmap2DImporter.ImportLightmap2D()` 通过 `ImportExportUtil.SerializeObject` 读 `v2.<jobGuid>.lmgz`（`DawnLightmap2DImporter.cs:9-23`），不是带版本头的 `SerializeObjectWithVersion`。
  - 原版布局（`BakingJobDefine.cs:556-607`）：`FLightmap2DOutputHeader`(48B) → `JobInputIndex`(i32) → `bHasPrtHQTextures/bHasPrtLQTextures/bHasStaticTextures`(3×u8) → `Lights`(Guid 数组) → `LightmapData`(`FLightmap2DSample[192B]` 数组)；**没有版本头，也没有尾部 padding**。
  - C++ 新增 `readLightmap2DOutputFile()`，复用已有 `readLightmap2DOutput()`，校验 inflate 后无 trailing bytes；`gzipDecompress()` 优先用 zlib 处理标准 deflate，未链接 zlib 时回退 stored-block 解析。
  - CLI：`--lmgz-test` 合成 4×2 output 做写→gzip→读回；`--inspect-lmgz <file>` 打印 header/size/samples/lights。合成 deflate（Python gzip）实测解析成功。
  - CTest 新增 `dawnbake_lmgz_reader`，当时 **17/17**；D5.2 后完整为 **18/18**。真实 Dawn `.lmgz` 已由 D4.5 官方 Agent 路径产出并解析成功；`JobInputIndex` 在 native 输出中为未初始化值（本次 `-857276416`），D5.2 不使用该字段。
- [x] D5.2 `Size` 与 `FLightmap2DInput.Size` 校验；`JobID/MeshID` 定位实例
  - 原版参考：`DawnLightmap2DImporter.ImportLightmap2D()` 用 `LightmapTask.Allocation.UVWidthPixel/UVHeightPixel + 2*Padding` 与 `HeaderInfo.Size` 做 `Debug.Assert`（`DawnLightmap2DImporter.cs:30-44`）；导出端 `DawnLightmap2DExporter.cs:184-187` 以 `JobID=TaskGuid`、`MeshID=Lightmap2D.Guid`、`Size=UVWidthPixel/UVHeightPixel` 写入。
  - 新增 `lightmap_result.h/.cpp`：`validateLightmap2DResult()` 校验 `JobID`、`MeshID`、正尺寸、`HeaderInfo.Size` 与 `FLightmap2DInput.Size`（无 padding 时相等；有 `LIGHTMAP_FLAGS_PADDING` 时各边 +2，对齐原版 importer）、`LightmapData.size == Size.x*Size.y`；`bindLightmap2DResult()` 用 `JobID` 唯一匹配 `BakingJobInputs.lightmap2DJobs`、用 `MeshID` 唯一匹配 `SceneInfo.meshInstances[].header.guid`，重复/缺失都报错；不读取 `JobInputIndex`。
  - CLI `--verify-lmgz <job-dir> <guid>`：自动读取 raw `.scene/.jobs` 或 `.scenegz/.jobgz`，逐个读取 `v2.<JobID>.lmgz` 并输出 `job/instance/meshIndex/size/samples/mapped`；`--bake` 成功返回前也会走同一校验。
  - 实测 D4.5 真实样本（约 43KB）：`job=0 instance=0 meshIndex=0 size=64x64 samples=4096 mapped=4096`；缺失结果返回非零。`--bake --agent` 实测在 `Dawn agent: NONE ...` 后追加 `Dawn results: validated`。CTest 新增 `dawnbake_lmgz_binding`，D5.2 完成时 **18/18**；D5.3 后 **19/19**。
- [x] D5.3 三通道写入 atlas rect（RGBA16F；D7.2 语义改为 incident/direction/zero）
  - 新增 `lightmap_atlas.h/.cpp`：`buildLightmapAtlasImages()` 接收 D5.2 binding、D3 placement 和 atlas 尺寸；每个结果输出 `LightmapAtlasImage`（3 个 RGBA16F 通道 + `bIsMapped` mask）。
  - D5.3 当时 `SHVectorR/G/B` 分别写 R/G/B 三张图；D7.2 实测后改为层 0 `IncidentLighting`、层 1 `SHVectorR`、层 2 零。原版参考：`DawnLightmap2DImporter.cs:53,74` 分别读取 incident/direction，`DawnEncodeTextures.cs:399,408` 用 `SetPixels(EncodeOffset, Width, Height, ...)` blit。
  - padding 处理：D3 sidecar `rect` 是不含 padding 的数据块；原版 `DawnPackLightmapTask.cs:33-51` 的 `LightmapSizeX/Y` 含 `2*Padding`，所以目标原点为 `rect - padding`，输出尺寸为 `input.Size + 2*Padding`。非 padded 时原点就是 rect。
  - `--sh-atlas-test` 覆盖 padded/unpadded、RGBA16F half 值、边界清零、mask、越界/atlas 索引/rect 尺寸/padding/重叠/空输出；CTest 新增 `dawnbake_sh_atlas`，D5.3 时 **19/19**，D5.7 后 **22/22**。D5.3 原始拷贝测试显式传 `LightmapAtlasOptions{0}`。
- [x] D5.4 块内膨胀 3~4 texel（blit 前）
  - `LightmapAtlasOptions::dilationIterations` 默认 4；`buildLightmapAtlasImages()` 先按结果块写临时 3×RGBA16F，再膨胀，最后拷入 atlas target rect。
  - 参考 `DawnDilateTexture.cs:24-99` 与 `DilateLightmap.compute:24-94,96-164`：4 次迭代，每次对未覆盖 texel 取 3×3 邻域内有效 texel 的平均值；边界限制在结果块内，不跨 atlas 块。原版 `DawnEncodeTextures.cs:416-448` 只对 LightmapColor/LightmapDir 调用，PRT 分支未调用；D5.4 按计划补在 SH blit 前。
  - D7.2 后层 0 是 incident 颜色、层 1 是 direction+directionality；alpha 不承担 coverage，因此用 D5.1 的 `bIsMapped` 作为有效性，膨胀平均并保留 4 个分量，不使用原版 alpha/black 阈值，也不把 alpha 置 1。`mapped` 更新为膨胀后 coverage。
  - `--sh-atlas-test` 额外覆盖 0/1/4 次迭代、两个有效邻居的平均、alpha 保持、mask 扩张及相邻块不串色；D5.7 后 CTest **22/22**。
- [x] D5.5 DDS 写出：`lm<N>_sh0/1/2.dds`，无 mip、linear、clamp
  - 新增 `lightmap_dds.h/.cpp` 的 `writeLightmapAtlasDds()`，并在 `dawn_package` 增加 `lightmapAtlasName()` / `lightmapAtlasDdsFileName()`；每个 atlas 输出 `lm<N>_sh0.dds`、`lm<N>_sh1.dds`、`lm<N>_sh2.dds`。
  - 使用 `bimg::imageWriteDds`，容器为 RGBA16F、`numMips=1`、`numLayers=1`、无 cube、`srgb=false`；bimg 实际写 DX10 头（148B），incident/direction half 数据原样按行序输出，不做 Y 翻转。
  - 原版 PRT 纹理为 `TextureFormat.RGBAHalf`、`linear=true`、无 mip（`DawnEncodeTextures.cs:170-175`），通过 `SetPixels(EncodeOffset, ...)` 写块（`:257-265`）；原版 `DawnBakingResult.cs:481,541-562` 的 RGBHalf 结果走 EXR，没有 DDS。因此 D5.5 是运行时容器，保持相同的 RGBA16F bits 和行序；`filter=linear` / `wrap=clamp` 不是 DDS 文件元数据，由 D6 上传采样器设置。
  - `--sh-atlas-dds-test` 覆盖 2 个 atlas / 6 个文件、文件名、RGBA16F、无 mip、尺寸、raw bytes 逐字节比对，并拒绝空 images 和错误 channel 大小；CTest 新增 `dawnbake_sh_atlas_dds`，D5.7 后 **22/22**。
- [x] D5.6 逐 texel 校验脚本（对比 `.lmgz` 与 DDS 解码值）
  - `lightmap_dds.h/.cpp` 新增 `verifyLightmapAtlasDds()`：读取 3 张 DDS，校验 RGBA16F、尺寸、无 mip、无 cube、linear；D7.2 后按 `.lmgz` 的层 0 `IncidentLighting`、层 1 `SHVectorR`、层 2 零值对每个 `bIsMapped != 0` texel 的 4 个分量做 half-bit 精确比对；v1 暂不校验 `PrtHQ/PrtLQ`（真实 D4.5 样本 `prtHQ=0`）。
  - 目标坐标由调用方给出 DDS target 原点（原版规则 `D3 data rect - padding`，无 padding 时等于 rect）；只跳过未映射 texel，避免 D5.4 膨胀导致的 unmapped 变化误报。原版逐 texel 循环参考 `DawnLightmap2DImporter.cs:46-57,74`，`ToColor` 保留 `.w` 参考 `DawnImporter.cs:249-252`，`SetPixels` blit 参考 `DawnEncodeTextures.cs:257-265`。
  - `--sh-atlas-dds-verify-test` 先写合成 `.lmgz`、经 D5.3/D5.4 生成并写出 DDS，再读回校验 3 个 mapped texel 的 4 分量；修改一个源 incident 分量后必须报 `texel mismatch`。CLI `--verify-sh-atlas-dds` 接受 `<file.lmgz> <dds-dir> <atlas-index> <target-x> <target-y> <atlas-size>`；CTest 新增 `dawnbake_sh_atlas_dds_verify`，当前 **22/22**。
- [x] D5.7 sidecar 合并（保留原有字段，新增 `lightmap` 段）
  - `dawn_package` 新增 `mergeLightmapSidecar()`：读取已有场景 `.hdrp.json` 和 D3 fragment，验证 fragment 的 `lightmap.version/texelPerUnit/atlases/objects`（atlas 索引与 rect 边界）后，只替换根 `lightmap`；用 `nlohmann::ordered_json` 保留场景原有字段及其顺序（`hdrp`、`materials`、`lights`、`objects`、自定义字段）。
  - 原版结果映射参考：`DawnBakingResult.cs:94-104` 把 `LightmapIndex` + `LightmapScaleOffset` 记入 MeshInfo，`:777-778` 导入时写回 `Renderer.lightmapIndex/lightmapScaleOffset`；`DawnLightDataAsset.cs:331-379` 把 lightmapIndex/ST 写入 LightmappedRendererData。我们的 sidecar 以对象名 + `atlas/rect/st` 表达同一信息。
  - CLI `--merge-sidecar <scene.hdrp.json> <lightmap.json> [<out.hdrp.json>]`，省略输出时原地更新；自动创建输出父目录，重复合并幂等。`--sidecar-merge-test` 覆盖字段保留、stale lightmap 替换、原地合并、幂等、坏根对象、缺 lightmap、坏 JSON、越界 rect 和缺失场景文件；CTest 新增 `dawnbake_sidecar_merge`，当前 **22/22**。

**D5 出口条件**：T1 的数值部分通过；贴图能在引擎里加载（先不接 shader）。

---

## D6 运行时采样（1–2 周）

### D6.1 资产与数据

- [x] `RenderObject` 增加 `lightmapAtlas` / `lightmapST` / `lightmapSize`
- [x] sidecar 解析 `lightmap.objects`
- [x] `src/render/lightmap/`：DDS 加载（保留 RGBA16F，不走 `TextureLoader` 的 RGBA32F 转换）
- [x] atlas 上传 + 采样器（linear/clamp/无 mip）

实现与验收：

- `RenderObject` 使用 `lightmapAtlas == UINT32_MAX` 表示无 lightmap；sidecar 的 `st` 直接保存，缺失时按 `(S/A, S/A, x/A, y/A)` 计算；`lightmapSize` 保存 rect 像素尺寸。
- `SceneLoader` 解析根 `lightmap`：校验 atlas `file/size/layers/format` 和 object `atlas/rect/st/contributeGI`，按 `RenderScene::objects` 的 `name` 匹配；`contributeGI=false` 不绑定。参考原版 `DawnLightmap2DTask.cs:269-274`（lightmapIndex/ST）、`DawnLightDataAsset.cs:331-379`、`DawnMeshComponent.cs:198-225`（per-renderer lightmap 数据）。
- `LightmapSet::load()` 读取 `lm<N>_sh{0,1,2}.dds`，校验正方形 RGBA16F、无 mip/无 layer array/非 sRGB，再合并为单个 RGBA16F 2D texture array，layer 布局为 `atlas*3 + channel`（`LightmapAtlas.layerBase`）；采样器 `BGFX_SAMPLER_UVW_CLAMP`，默认 linear，无 mip。原版 `DawnBakingResult.cs:855-884` 的 PRT texture array 同样使用 Bilinear/Clamp。**不经过 `TextureLoader`**，避免其 `RGBA32F` 转换（`src/asset/texture_loader.cpp:64-66`）。
- `--asset-test` 在加载到 `lightmap` 时会调用 `LightmapSet` 上传并打印 `atlases/layers/size`；实测 D3D11 `uploaded lightmap SH array: atlases=1 layers=3 size=64`。真实烘焙的 `.dds` 仍需 `--verify-sh-atlas-dds` 校验。
- 新增单测：sidecar 绑定/越界 rect、DDS RGBA16F 读取；`hbg_tests` **241 cases / 32,432 assertions**，CTest **22/22**。

### D6.2 顶点与 uniform

- [x] `varying.def.sc`：`a_texcoord1` + `v_texcoord1`；非实例 VS 用 `u_object_lightmapST` 在 VS 内生成最终 `lmUV`
- [x] `varying_instanced.def.sc`：同上；`i_data3` 已被 model matrix 占用，ST 新增 `i_data4 : TEXCOORD27`（stride 20 floats），实例 VS 用 `i_data4` 生成最终 `lmUV`
- [x] `prepass.idl` Object block：`lightmapST` + `lightmapParams`；默认 identity/disabled，`params.y=atlasIndex*3`
- [x] `pass.idl`：`lightmapParams2=(1,3,0,0)`
- [x] 重新生成 `uniforms.h` / `uniforms.sc`
- [x] `DrawItem/DrawBatch/DrawSortKey` 按 `lightmapAtlas` 分组，避免实例批次混用 atlas；shadow 实例保持 16 floats
- [x] 槽位审计：forward 16 个 PS stage 全占用（8=shadowContext buffer，15=cookie）；**不能直接在 forward.fs 新增 sampler**。D6.3 改为同一 forward view 内的独立 baked-GI program/draw（SH=15，baseColor=1，normal=2，且在 item state/material bind 后重绑）；主 forward 仅跳过 ambient
- [x] 单测：新增 lightmap atlas 批次拆分；uniform 布局/描述符同步；`hbg_tests` **242 cases / 32,455 assertions**；CTest **22/22**
- [x] Shader 抽查：`forward.vs` / `forward_instanced.vs`、forward/prepass/gbuffer/debug 代表变体 dxbc+spirv 编译通过

### D6.3 Forward

- [x] 新增独立 baked-GI program/draw（避免 forward 16 槽冲突）：`baked_gi.sc` 解码 SH，`s_lightmapSH` stage 15，baseColor 1，normal 2
- [x] 主 forward 在 baked-GI 实际启用时跳过 ambient SH；不采样 lightmap
- [x] baked-GI draw 使用 VS 已变换的 `v_texcoord1`，输出 `bakedGI * albedo` + mask
- [x] 静态物体不再叠加 ambient SH
- [x] 无 lightmap 物体行为不变（`triangle.gltf` D3D11 截图与 D6.2 基准 hash 完全一致）

实现与验收：

- `baked_gi.fs` 在 forward opaque view 内、主 forward draw 之前执行；复用 depth prepass/depth attachment/framebuffer，避免额外 render graph pass 的 view/clear 生命周期问题。输出 RGB additive，alpha 保留 lightmap mask，为 D6.4 MRT 预留。
- `pass.idl` 的 `lightmapParams2.w` 作为 baked-GI active 标志；`direct_lighting.sc` 在 `HBG_FORWARD_LIGHTMAP_GI` 编译路径下同时检查 pass active 与 object `lightmapParams.x`，只去掉 ambient diffuse/SH，保留环境 specular。
- `DrawListDesc.requireLightmap` + `m_bakedGIDrawList` 只收集 opaque+contributeGI 对象；`lightmapAtlas` 继续参与分批。
- 修复 render graph 的 view 生命周期：每个 pass 先显式 `BGFX_CLEAR_NONE`；shadow atlas clear pass 每帧保留（仅首帧真正 clear），避免 frame 0 额外 pass 导致后续 frame view ID 漂移并错误继承 depth clear。
- D6.3 当时按 `.rgb=L1, .a=L0` 解码；D7.2 已确认原始 data 实际是 `IncidentLighting` 颜色 + `SHVectorR` Unity 方向编码，现行 shader 不再走 SH 解码。
- 验证：D3D11 与 Vulkan 默认 GPU light-list buffer 路径 `triangle_lm_ds`（double-sided）均输出 baked GI（D7.2 fixture 重生成）；`--frames=3` 稳定；`triangle_lightmap.gltf`（mask+double-sided）绿色 baked GI；`triangle.gltf` no-lightmap hash 与 D6.2 基准一致。`hbg_tests` **243 cases / 32,463 assertions**，CTest **23/23**。

### D6.4 Deferred

- [x] 复用独立 baked-GI pass 输出 `bakedIndirect` RT（rgb+mask），不往 gbuffer.fs 加 sampler
- [x] `forward_renderer.cpp`：RT 创建/传递/释放
- [x] deferred 合成：`deferred_lighting.fs.sc` 已写 ambient/SH，因此用独立全屏 `baked_gi_compensate.fs` 输出 `baked - ambient` additive；避免占用已满的 deferred sampler stage
- [x] `ssgi_composite.fs.sc`：`indirect *= (1.0 - bakedMask)`
- [x] forward-only 模式路径（沿用 D6.3 additive；deferred 帧 `lightmapParams2.w=0`）

实现与验收：

- `baked_gi.fs` 用 `m_gbufferDrawList`（仅 visible lightmap 对象实际输出 mask）绘制到 transient RGBA16F `bakedIndirect`，颜色附件 + depth equal 复用 prepass depth；pass 内显式清色，未覆盖像素 mask=0。
- SH sampler 放 stage 15，并在每个 item 的 `setState`/material bind 之后重绑；实测“绑定在 batch 循环外”会导致 mask=1 但 rgb=0。
- `baked_gi_compensate.fs` 解码 gbuffer surface，按 `hbgEvaluateAmbient` 同口径重算 ambient+SH，仅对 mask=1 像素减掉 ambient 并加 baked；环境 specular 保留。
- `ssgi_composite.fs` 从 `bakedIndirect.a` 取 mask；deferred frame 中 `lightmapParams2.w` 保持 0，避免 forward-only 误跳 ambient。
- D3D11 验证：`triangle_lm_ds` deferred 与 D6.3 forward 一致（ambient 补偿精确；D7.2 方向贴图重生成后采样 `(140,62,38)`）；D6.4 实现当时 `triangle.gltf` no-lightmap deferred hash 与 D6.2 基准一致（`CFCEF6A1831823CF878AE9E895BF592B02AFB6B20776BBA43419905877CBF4EA`）；当前共享工作区外部 shader 改动会改变该 hash，需以干净分支重跑。
- 未通过项：Vulkan deferred / SSGI 运行时被外部 `shadows.sc` 的 `setStorageBuffer(16)`（Vulkan compute binding limit）阻塞；`hbg_shaders` 全量 target 同时受外部 forward stage 冲突影响，D6.4 所需 baked/compensate 变体已单独编译并通过。

### D6.5 Debug

- [x] `--debug-view=lightmap`：复用 `baked_gi` 将 bakedGI*albedo + mask 写入 HDR color，`debug_lightmap.fs` 全屏显示；forward/deferred 共用 pass。
- [x] `--debug-lightmap-mask`：二元 lightmap mask 视图，对齐 HDRP MaterialDebug Lightmap 的红色标记语义；DebugUI 的 Debug view 下拉自动包含 `Baked Lightmap`。
- [x] `config/uniforms/debug.idl` 增加 `lightmapParams`，`DebugLightmap` 通过 `DebugUniformBinder` 上传。
- [x] D3D11 验证（D6.5 当时）：`triangle_lm_ds` forward/deferred radiance debug hash 一致，采样 `(255,132,66)`；mask 均为 `(255,0,0)`（mask 背景近黑）；无 lightmap 场景全黑；正常渲染 `(252,215,183)`。D7.2 改为方向贴图后重生成 D6 fixture，正常采样为 `(140,62,38)`，forward/deferred 仍 0 diff。
- [x] D6.5 当时 `hbg_tests`：244/244，32,482 assertions；CTest 26/26；`hbg` 构建通过。Vulkan/SSGI 仍受外部 `shadowSat` t16 阻塞（与 D6.4 相同）。

### D6.6 T4 forward/deferred

- [x] 修复 forward 重复 submesh 的 baked-GI 重复叠加：forward 改为 `bakedIndirect` + 全屏 `baked_gi_forward_composite.fs`，不再在 forward opaque 前逐 item additive。
- [x] D3D11、1280×720、`triangle_lm_ds`、`--camera-orbit --frames=3`：forward 与 deferred 整图逐字节一致，PSNR=∞，采样 `(140,62,38)`，背景 `(71,72,94)`（D7.2 方向贴图重生成后复验）。
- [x] 无 lightmap 控制：`triangle.gltf` forward/deferred 整图逐字节一致；证明 T4 修复未影响 no-LM 路径。

### D6.7 T5 SSGI 互斥

- [x] D3D11、1280×720、`triangle_lm_ds` deferred、`--ssgi --no-ssgi-temporal` vs `--no-ssgi`：整图逐字节一致，PSNR=∞，最大差 0。
- [x] SSGI 实际生效的控制：无 lightmap `triangle.gltf` 的 SSGI on/off 为 PSNR 45.107 dB、最大差 11、50,425 个像素不同；被 mask 的 LM 场景为 0 差异。
- [x] `ssgi_composite.fs` 的 `indirect *= (1.0 - bakedIndirect.a)` 与 D6.4 mask 路径一致。
- [x] 构建/测试：`hbg` 成功；全量 `hbg_tests` 244/245，唯一失败 `tests/render/test_shadow.cpp:545`，属外部 shadow 改动；排除 shadow 用例后 212/212 通过。D6.6/D6.7 的 D3D11 渲染验证独立通过。

**D6 出口条件**：T4 forward/deferred ≥ 40 dB；T5 SSGI 互斥通过。

---

## D7 校准 & 验收（1 周）

- [x] D7.1 T2 UV V 轴校准（非对称场景）
  - 新增 `hbg_dawnbake --export-t2`：4m×4m 白地板，UV2 `u=x, v=z`；+X 红灯 `(2.5,2,0)`、+Z 绿灯 `(0,2,2.5)`，128×128 lightmap、atlas 2048、rect `[2,2]`。
  - Dawn 实烘结果（Agent 路径，`v2.0000...05.lmgz`）：`IncidentLighting` 红峰在 `u=127,v=63`，绿峰在 `u=64,v=127`，与 +X/+Z 一致。
  - 运行时验证（D7.2 方向贴图格式）：`--debug-view=lightmap` 640×360；右侧红 `(255,105,0)`、左上绿 `(107,255,0)`、中心黄 `(255,255,0)`；D7.1 当时用合成 incident 的 L0 通道也得到同方向结论。
  - **结论：`uv2.y` 不取 `1-v`，Dawn 行序与运行时 DDS/UV 采样一致，无需 V 翻转。** D7.2 进一步确认 `SHVectorR` 是方向编码，不是 SH 系数。
- [x] D7.2 T1 方向贴图/缩放校准（单面单平行光）
  - 新增 `hbg_dawnbake --export-t1 <dir> [guid]`：4m×4m 白地板，UV2 `u=x,v=z`，64×64 lightmap，单平行光（默认 +Y、强度 1、白色、无阴影、黑天空），albedo=1。
  - 原版参考：`DawnLightmap2DImporter.cs:53` 读 `IncidentLighting` 写颜色贴图，`:74` 读 `SHVectorR` 写方向贴图；`DawnEncodeTextures.cs:399,408` 分别 `SetPixels`；Unity `SampleDirectionalLightmap` 用 `direction.xyz - 0.5` 与 `direction.w`。
  - T1 +Y 实烘（Agent）：`IncidentLighting=(1.000254,1.000254,1.000254)`，`SHVectorR=(0.499861,0.999723,0.499861,1.000254)`，`SHVectorG/B=(0.282095,-0.488603,0,0)`。**结论：`SHVectorR` 是 Unity 方向编码，不是 SH 系数；`SHVectorG/B` 的 SH 系数当前不参与运行时。**
  - DDS/atlas 语义改为：层 0 = `IncidentLighting`（颜色），层 1 = `SHVectorR`（方向+方向性），层 2 = 零；`hbg_dawnbake --write-lightmap-dds`、`buildLightmapAtlasImages()`、`verifyLightmapAtlasDds()` 同步。
  - Shader 改为 `hbgDecodeBakedGI(incident,direction,n) = incident.rgb * (dot(n,direction.xyz-0.5)+0.5) / max(direction.w,1e-4)`；`u_pass_lightmapParams2.x=1.0`。
  - D3D11 640×360 T1 runtime `--debug-view=lightmap`：中心/多点 `(255,255,255)`（ACES 下白色；+Y 法线的解码公式为 `incident * 1.0`）。
  - T2 真实方向贴图重新验证：中心黄 `(255,255,0)`、右侧红 `(255,105,0)`、左上绿 `(107,255,0)`；与 UV2 方向一致。
  - D6 fixture 已重生成方向贴图并复验：`triangle_lm_ds` forward/deferred 1280×720 整图 0 diff，PSNR=∞，采样 `(140,62,38)`；T5 SSGI on/off 0 diff。
  - 构建/自测：`hbg`、`hbg_dawnbake` 构建通过；`hbg_tests` **248/248，32,525 assertions**；`--export-t1`、`--write-lightmap-dds`、`--sh-atlas-dds-verify-test`、`--lmgz-test`、`--proto-test` 通过。`ctest -R dawnbake` **20/20**（补建 `hbg_fake_dawn` 后）；全量 CTest **31/32**，唯一失败 `hbg_vfx_reference_forces_d3d11` 为外部 VFX 浮点参考差异，与本轮 atlas/shader 无关。
- [ ] D7.3 灯光单位校准（1/2/4m 衰减）
- [ ] D7.4 T1 色差 < 5%
- [ ] D7.5 T3 接缝无黑缝/串色
- [ ] D7.6 T6 性能：打包 < 2s（100 物件）、显存 < 100MB
- [ ] D7.7 校准值写入 `config/dawn.json` 与主文档 §15
- [ ] D7.8 与 Unity 烘焙同场景对比报告（图 + 数据）

**D7 出口条件**：T1–T6 全部通过并记录。

---

## D8 收尾（3–5 天）

- [ ] D8.1 CLI 文档：`--help` + README 段落
- [ ] D8.2 错误处理：缺 DLL / 超时 / 磁盘满 / 版本不匹配
- [ ] D8.3 `ctest` 增加 dawn 用例（golden round-trip、packer、lmgz reader）
- [ ] D8.4 回归：M0–M12 现有用例全绿
- [ ] D8.5 清理：不提交 `Jobs/`、`_FileCache/`、临时 DDS
- [ ] D8.6 主文档状态更新为"已完成"

---

## 附录 A：结构体尺寸表（D0.6 已实测）

| 结构 | C# `Marshal.SizeOf` | C++ `sizeof` | 校验 |
|---|---|---|---|
| `FGuidInfo` | 16 | | |
| `FSHAHashInfo` | 20 | | |
| `FLightProfileInfo` | 65536 | | |
| `FLightInfo` | 176 | | |
| `FLightFullInfo` | 200 | | |
| `FSkyLightInfoHeader` | 112 | | |
| `FTexture2DInfoHeader` | 16 | | |
| `FMaterialColor` | 4 | | |
| `FMaterialInfoHeader` | 52 | | |
| `FSceneBounds` | 40 | | |
| `FSceneBoxBounds` | 24 | | |
| `FMeshElementInfo` | 16 | | |
| `FMeshInfoHeader` | 48 | | |
| `FMeshInstanceInfoHeader` | 312 | | |
| `FLandscapeInfoHeader` | 172 | | |
| `FSceneInfoHeader` | 108 | | |
| `FLightmap2DInput` | 160 | | |
| `FLightmap2DSample` | 192 | | |
| `FLightmap2DOutputHeader` | 48 | | |
| `Matrix4x4` | 64 | | |
| `float2` / `float3` / `float4` | 8 / 12 / 16 | | |

关键偏移（C# `Marshal.OffsetOf`）：

| 结构 | 字段偏移 |
|---|---|
| `FLightInfo` | Position 0, Normal 12, dPdu 24, dPdv 36, Color 48, Dimensions 60, Shaping 72, Attenuation 80, FalloffExponent 84, Flags 88, IESTextureSlice 92, IndirectColor 96, BoundMin 108, BoundMax 120, LightTextureMode 132, ShadowTranslucency 136, bEnablePenumbra 140, PenumbraTint 144, PenumbraLuma 160 |
| `FLightmap2DSample` | SHVectorR 0, G 16, B 32, IncidentLighting 48, SHCorrection 64, SkyOcclusion 68, AOMaterialMask 80 |
| `FMaterialInfoHeader` | Hash 0, ShadingModelID 20, BlendMode 24, bTwoSided 28, Flags 32, OpacityMaskClipValue 36, IndexInSceneInfo 40, DiffuseBoost 44, EmissiveBoost 48 |
| `FMeshInfoHeader` | Guid 0, LODIndex 16, NumTriangles 20, NumVertices 24, NumElements 28, LightmapUVIndex 32, LightmapUVDensity 36, LightmapTexelDensity 40, IndexInSceneInfo 44 |
| `FMeshInstanceInfoHeader` | LevelGuid 0, Guid 16, Transform 32, InverseTransform 96, InverseTransposeTransform 160, LightmapId 224, BoundingBox 228, MeshIndex 252, TexcoordIndex 256, Flags 260, DiffuseBoost 264, EmissiveBoost 268, RayOffset 272, FullyOccludedSamplesFraction 276, MeshLightAttenuationRadius 280, MeshInstanceVisibilityMask 284, MeshInstanceVisibilityMaskInHLODScene 288, LODLevelInHLODScene 292, UV2MapSize 296, UV2Offset 304 |
| `FLightmap2DInput` | JobID 0, MeshID 16, Flags 32, IsHLODActorMapping 36, ScenePartitionIndex 40, padding 44, Size 48, LightmapUVIndex 56, OverrideBakingParams 60 |


---

## 附录 B：验收数据记录

| 用例 | 后端 | 分辨率 | 结果 | 备注 |
|---|---|---|---|---|
| T0 协议 diff | - | - | | |
| T1 单面单平行光 | D3D11（Vulkan 待外部 shadowSat 修复） | 640×360 | incident=1、方向解码为 1.0（白色） | T1 +Y 场景 |
| T2 UV 朝向 | D3D11 | 640×360 | +X 红在右、+Z 绿在上，无 V 翻转 | Dawn `IncidentLighting` + `SHVectorR` 方向 debug |
| T3 接缝 | D3D11 / Vulkan | 1280×720 | | |
| T4 forward vs deferred | D3D11（Vulkan 待外部 shadowSat 修复） | 1280×720 | PSNR=∞（逐字节一致） | `triangle_lm_ds` forward composite vs deferred |
| T5 SSGI 互斥 | D3D11 | 1280×720 | 逐字节一致（PSNR=∞） | 无 LM 控制 45.107 dB，证明 SSGI 生效 |
| T6 打包/烘焙耗时 | - | - | | |

---

## 变更记录

| 日期 | 变更 |
|---|---|
| 2026-11-19 | 初版：D0–D8 拆分，确定 atlas + SH L1 + sidecar 方案 |
| 2026-11-20 | D5.7：sidecar 合并、CLI、字段保留/幂等测试；CTest 22/22 |
| 2026-11-20 | D6.1：lightmap sidecar 绑定、RGBA16F DDS 加载、texture array 上传；`hbg_tests` 241/32,432，CTest 22/22 |
| 2026-11-20 | D6.2：UV1/ST、Object/Pass uniform、实例 i_data4、atlas 分批；`hbg_tests` 242/32,455，CTest 22/22 |
| 2026-11-20 | D6.3：forward additive baked-GI program、ambient/SH 去重、requireLightmap、render graph view 生命周期修复；`hbg_tests` 243/32,463，CTest 23/23 |
| 2026-11-20 | D6.4：deferred `bakedIndirect` RT、ambient 补偿全屏 pass、SSGI mask、SH sampler stage 15/重绑问题修复；D3D11 deferred LM/no-LM 验证通过 |
| 2026-11-20 | D6.5：`--debug-view=lightmap`/`--debug-lightmap-mask`、`DebugLightmap` + `lightmapParams`、forward/deferred debug hash 一致；`hbg_tests` 244/32,482，CTest 26/26（D6.5 当时） |
| 2026-11-20 | D6.6/D6.7：forward 改 `bakedIndirect` + 全屏 composite，修复重复 submesh 双重 indirect；T4/T5 D3D11 均逐字节一致 |
| 2026-11-20 | D7.1：T2 非对称场景实烘；UV2 V 轴不翻转，运行时 +X/+Z 方向正确 |
| 2026-11-20 | D7.2：确认 `SHVectorR` 为 Unity 方向编码；atlas 改为 incident+direction；T1 +Y 白、T2 真实方向正确；T4/T5 方向贴图复验 0 diff |

