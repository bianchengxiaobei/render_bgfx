# Dawn GI 接入检查清单

> 主文档：[`dawn-gi-plan.md`](dawn-gi-plan.md)。本文件是唯一进度来源：做完一项就勾一项并写验收数据。
> 状态标记：`[ ]` 未开始 / `[x]` 完成 / `[!]` 阻塞（在行尾写原因）。

---

## 当前状态

- 阶段：**D4 完成（OnlyLocal + 本地 Coordinator mock 打通官方 Agent 作业流，产出真实 `.lmgz`）；D5.1–D5.7 完成；D6.1–D6.7 完成（D6.6/D6.7 T4/T5 已在 D7.2 方向贴图数据上复验）；D7.1 完成（T2 UV V 轴）；D7.2 完成（Dawn 方向贴图解码 + T1 单平行光标定）；D7.3 完成（点光 1/d² 衰减校准）；D7.4 完成（T1 线性色差校准）；D7.5 完成（T3 接缝校准）；D7.6 完成（T6 100 物件性能/显存）；D7.7 完成（校准值固化到 config/dawn.json）；D7.8 参考路径对比完成（Unity 实机截图受 D0.12 阻塞）；D8.1/D8.2/D8.3/D8.4/D8.5 完成；D3 清单完成（D3.1–D3.9）；D2.1–D2.17 完成（D2.11–D2.15 细节见代码/测试与 `tools/dawnbake/README.md`）**；D1 已完成（gzip deflate 已补）；D0.8 本地已解除：官方未打补丁 `Dawn.exe` 直启/Agent 均完成烘焙；OptiX 与 CPU OIDN（`denoiserMode=2`）两条路径均成功，CPU 结果 196,921B，仅保留 `string pointer is null` 日志警告；dawnbake **36/36**；`hbg_tests` 上一轮 **315/315**（当前工作区外部 `config/pipeline.json` 改动导致 1 个 settings 用例失败，与本轮无关）；全量 CTest 上次 **137/137**（当前 148 项，未重跑）
- 协议版本：`1002`（Dawn 插件 2.4.5，Dawn.exe 1.3.5.20241220）
- 最近更新：2026-11-26

---

## D0 Spike & 基础设施（3–5 天）

- [x] D0.1 无 Unity 环境：改用 `tools/dawnbake/golden`（C# 直接编译协议源码）生成 `.scene/.jobs`；官方 Unity golden 待有环境时补
- [x] D0.2 验证 `Dawn.exe` 直启：`-local -localdirectory <dir>\ <guid>` 可绕过 Agent，读取 `<guid>.scene` / `<guid>.jobs`（未压缩，版本头 1002）
- [x] D0.3 验证二进制：无需 `optix*.dll`（`dumpbin` 仅依赖 `nvcuda/vulkan-1/OpenImageDenoise`；OptiX 走驱动 `nvoptix.dll`）
- [x] D0.4 记录启动依赖：工作目录为 exe 目录；`-localdirectory` 必须以 `\` 结尾；`Dawn.data` 必需；OIDN device 插件必需
- [x] D0.5 工具骨架：`tools/dawnbake/golden`（C#）+ `tools/dawnbake/README.md`；C++ `hbg_dawnbake` 骨架并入 D1
- [x] D0.6 结构体尺寸/偏移表已生成（见附录 A），与 C# `Marshal` 一致
- [x] D0.7 黄金样本入库：`tests/dawn/golden/min_scene/`（`.scene` 1516B、`.jobs` 800B、`expected.json`）
- [x] D0.8 Spike：Dawn 1.3.5 曾在本机 `DeviceResource` 阶段崩溃；2026-11-22 复测**官方未打补丁** `Runtime\.Dawn\Win64\Binaries\Dawn.exe` 已可完成烘焙
  - 直启 `--run-dawn` 连续 4 次 + 停服务后 1 次，均 exit=0、`Lighting complete`；日志仍出现 `string pointer is null`，但随后正常结束；该日志**不能作为设备失败判据**。
  - Agent 路径使用同一官方 exe（未打 CUDA patch）也完成 T4 64×64 烘焙，`Dawn results: validated`。
  - 压力复验：T6 100 个 lightmap job 直启官方 exe，26.8s 完成、exit=0、`Lighting complete [Lighting = 25.7 sec]`，200 次 `launch optix denoiser`，不再出现 DeviceResource 崩溃。
  - CPU OIDN 路径复验：把临时 `.jobs` 的 `denoiserMode` 从 0 改为 2 后，直启日志出现 `DenoiserMode:OIDN`、OIDN 2.2.2 CPU 设备信息和 `Lighting complete`；Agent 实烘产出 `v2.000...05.lmgz` **196,921B**（8.57s），`--inspect-lmgz` 为 64×64/4096，`Dawn results: validated`。证明 CPU OIDN 本身可工作，历史“CPU device 静默失败”是错误判据。
  - **枚举核对（以 1.3.5 runtime 为准）**：日志/字符串实测 `DenoiserMode` 为 `0=DAWN, 1=OPTIX, 2=OIDN, 3=OPENIMAGE_RT`；而原 Editor `BakingJobDefine.cs` 的 `EDenoiserMode` 是 `0=OPTIX, 1=OIDN, 2=DISABLE, 3=DAWN`，`DawnSettings.cs` 的 `ELightmapDenoiserMode` 是 `0=Optix, 1=OpenImage, 2=Disable`。编辑器旧枚举与 1.3.5 runtime 存在数值错位，强制 OIDN 必须写 runtime 的 **2**，不能按 Editor 的 `OpenImageDenoiser=1` 推断。
  - 2026-11-19 的历史 workaround（`oidnNewDevice(CPU=1)`→`CUDA=3`）仅保留作记录，不再用于当前验证。
  - 仍需 D0.9 干净机复测确认；D0.10 仅剩日志清理/健壮性价值，不再是功能阻塞。

**D0 出口条件**：驱动方式已明确（直启）；golden 可读；尺寸表已落表；本地官方 exe 直启/Agent 均可烘焙。干净机复测见 D0.9。

### D0 阻塞记录（2026-11-19，2026-11-22 修正）

> **修正**：以下 2026-11-19 的“CPU device 静默失败/必然崩溃”结论已被 2026-11-22 的 `denoiserMode=2` 复测推翻。
> 当前官方 exe 的 CPU OIDN 路径可正常产出 `.lmgz`，`string pointer is null` 只是日志层错误，不再作为阻塞依据。
> 本段仅保留历史取证过程；当前结论以 D0.8 为准。

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
  - 2026-11-21 部分验证：仅复制 `hbg_dawnbake.exe` + 运行 DLL 到临时目录，`PATH` 收窄到 `System32;Windows`；`--package-test`、`--agent-spec-test`、`--agent-cache-test`、`--agent-multi-upload-test`、`--agent-transfer-test`、`--agent-download-test`、`--agent-multi-download-test` 全部 exit=0。证明不依赖仓库、Vulkan SDK 或开发 PATH；仍不是干净操作系统/干净驱动复测，不能关闭 D0.9。
  - 2026-11-22 补充：停掉 Coordinator/DawnDeamon 后，官方未打补丁 `Dawn.exe` 直启仍 exit=0 且 `Lighting complete`；说明不依赖本地服务/Agent 环境，但仍非干净机器。
  - 2026-11-22 进程级干净 bundle：只复制 `Dawn.exe`、`Dawn.data`、OIDN/TBB DLL 到临时目录，清空进程环境变量后仅保留系统必需项，`PATH=C:\Windows\System32;C:\Windows`；默认 `DenoiserMode:DAWN` exit=0、`Lighting complete`；`denoiserMode=2` 的 `DenoiserMode:OIDN` 也 exit=0、`Lighting complete`。但日志仍加载系统 Vulkan 校验层（1.3.216，VUID/VkStructureType 报错），说明该方式无法排除系统 SDK/驱动，D0.9 仍未关闭。
- [ ] D0.10 向 Dawn 提供方索取修复版 `Dawn.exe`（仅清理 OIDN 空指针日志；CPU OIDN 功能已验证可用，不再视为功能阻塞）
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
- [x] gzip 读写（zlib），与 `.scenegz` 等扩展名对应
  - `gzipCompress()` 链接 zlib 时写标准 deflate，10B header 对齐 .NET `GZipStream`（`1F 8B 08 00 00 00 00 00 04 00`），末尾写 CRC32/ISIZE；未链接 zlib 时回退 stored block。`gzipDecompress()` 优先 zlib，兼容原版 `.NET GZipStream` 产物。
  - 原版 `FDawnHiveInterface.cs:1332-1339` 的 `Compress()` 使用 `GZipStream(..., CompressionMode.Compress, false)` 写 `.scenegz/.jobgz`；本实现与其容器格式一致。
  - `--package-test` 验证 `.scenegz/.jobgz/meshgz/matgz` 解压后与原始文件一致，且 scene/jobs 压缩后小于 stored。`--export-min` 实测 scene 7636→338B、jobs 800→119B；手工用 .NET `GZipStream` 解压本工具产物、以及用本工具读取 .NET 产物均通过。
  - 官方 Agent 实跑（2026-11-21，gz-only）：本地 Coordinator 5010 + DawnDeamon 5009 + DawnAgent 5011，job 目录只保留 deflated `.scenegz` 338B / `.jobgz` 119B（删除 raw `.scene/.jobs`），`--bake ... --agent` 仍输出 `DAWN_PROGRESS phase=done`、`Dawn results: validated`，结果 `.lmgz` 42,798B；证明原版 DawnAgent/Dawn 直接读取 gzip deflate，且 `buildAgentJobSpec()`/`expectedDawnResults()` 已支持 `.jobgz` 回退。

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
- 已知限制：`FBakingJobInputs` 中未使用的 job 数组只接受空数组。

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
- [x] D2.11–D2.15 场景/灯光/天空/Parcel 导出扩展
- [x] D2.16 ShadowMask relevant lights、SDF jobs/groups、`.sdfgz` Agent 结果
- [x] D2.17 lightmap group packing 与超尺寸 lightmap split/merge（`LIGHTMAP2D_GROUP`/`LIGHTMAP2D_SPLIT`）

**D2 出口条件**：手搭 1 个三角面 + 1 盏平行光场景，Dawn 不报错并产出 `.lmgz`。当前 D2.1–D2.17 完成；Dawn 已读取并完成烘焙，但 local 模式未落盘 `.lmgz`，结果通道走 D4/D5 Agent。

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
  - gzip 使用 **deflate**（zlib；header/trailer 对齐 .NET `GZipStream`），未链接 zlib 时回退 stored blocks；已验证 .NET `GZipStream` 可解本工具产物，本工具也可读 .NET 产物。
- `--package-test` 校验并通过：scenegz/jobgz 解压后与本地文件一致、scene/jobs/mesh/material 重新解析无尾字节、meshId/flags/texture format 正确。
- CTest 新增 `dawnbake_package_export`，dawnbake 全量 8/8 通过；`--roundtrip`/`--inspect` 支持 gzip（stored/deflate）及 mesh/material。
- **D2.9 golden 字段级比对**已实现（`--golden-test <dir>`）：
  - 读取 `expected.json`，校验文件名/大小、协议版本、scene/jobs 全部关键字段、mesh/material/instance/light/sky/jobs 交叉引用，以及两个文件均无尾字节；当前 353 个字段检查。
  - 结构断言照 C# `tools/dawnbake/golden/Program.cs` 的 `BuildMinimalScene/BuildMinimalJobs` 与 `BakingSceneDefine.cs`/`BakingJobDefine.cs`，不是从 C++ writer 反推。
  - 发现 golden 是手搭协议样本，与 D2 导出语义存在预期差异：索引未交换（`0,1,2,0,2,3`）、`LODLevelInHLODScene=0`、材质 `Format=0`、灯光 `Flags=12352`（无 cast shadow/non-inverse）、天空 `MipDimensions=(1,1,1)`、`PartialJobsTotalNum=0`；D2.8 包按原导出器语义输出（交换索引、LOD=-1、Format=19、Flags 含额外位、sky z=0、partial=1）。
  - 新增 CTest `dawnbake_golden_structure`，8/8 通过。
- **D2.10 Dawn 读取 + 烘焙验证**（2026-11-19）：
  - 用 `--export-min` 生成 raw `<guid>.scene/.jobs` + scenegz/jobgz/meshgz/matgz，送入 `Dawn.exe -local -localdirectory <dir>\ <guid> -lm 0 -1 1`。
  - 日志确认完整读取：`Num Lightmap2D Jobs:1`、`Jobs Range Info`、材质/网格上传、`BakeLightmap2D() start`、`FLightmapStaticLaunch::Execute(Normal) ... launchSize(64,64)`，最后 `Lighting complete [Startup = 138 ms, Lighting = 6.92 sec]`，进程退出码 0。
  - 当时误判本机 CPU OIDN 失败；2026-11-22 已用 `denoiserMode=2` 验证 CPU OIDN 可产出 `.lmgz`，下述 CUDA patch 仅为历史验证记录，不再是当前结论。
  - 当前官方 exe 的 CPU OIDN 与 OptiX 路径均可用；仅剩 OIDN 空指针日志，D0.10 降级为日志清理。
  - local 模式未在 job 目录生成 `.lmgz`；结果通道/落盘路径留到 D4/D5（Agent 或 direct launcher）确认。

- **D2.11–D2.15 场景导出扩展**：`--export-scene`/Parcel bridge、sidecar GI 过滤、sky GUID、灯光 bakeMode/mixedMode/lightingChannel/色温/shadowType/falloff/IES/Rect/Disc 等已落地，细节与验收命令见 `tools/dawnbake/README.md`。
- **D2.16 ShadowMask SDF 全链路**（2026-11-25）：
  - 原版参考：`DawnExporter.cs:327-377`、`DawnLightmap2DExporter.cs:160-194`、`DawnShadowMaskExporter.cs:99-159`、`DawnPackLightmapTask.cs:200-295`、`DawnShadowMaskGroupExporter.cs`、`DawnShadowMaskGroupTask.cs`、`DawnShadowMaskImporter.cs`。
  - mixed+shadowmask 时收集 `meshInstances[].relevantLights`（Directional 总有，局部灯按 bbox 相交，realtime 跳过）；lightmap job 的 `DIRECT` 只在相关灯含 Baked 时保留。
  - 每个 mesh 写 `SdfShadowInput`（JobID=meshGuid B=1 C=index、MeshID、BILINEAR/PADDING、size、upsample=4、UV=1、相关 mixed cast-shadow 灯 GUID）；按 `config.lightmap.taskPackSize`（默认 512）打包 `SdfShadowGroupInput`（JobID=(102,1,index,0xFFFFFFFF)、GroupSize=packSize 或单任务 max、upsample=4、job index/offset，task padding flag（0/1）计入 packing），并补 `SdfShadowParameter`/`ShadowSettingParameter`（`bAllowSignedDistanceFieldShadows=1`）。
  - 结果按 `FSDFShadowOutput` 读取（48B header + custom maps，12B `FSDFShadowInfo`），支持 `--sdfgz-test`/`--inspect-sdfgz`/`--verify-sdfgz`；Agent 任务参数 `SHADOWMASK_GROUP`，结果文件为组内每个 job 的 `v2.<jobguid>.sdfgz`。校验 `ShadowMaps[i].LightID == ShadowmaskLights[i]`，并拒绝顺序/灯 GUID 不匹配。
  - 真实 Agent（Coordinator 5010 + DawnDeamon 5009 + Agent 5011）4/4 tasks、`Dawn results: validated`，下载 2 个 `.lmgz` + 2 个 `.sdfgz`（424B/333B）；`--inspect-sdfgz` 50×50 2500 samples、31×31 961 samples；`--verify-sdfgz` 3461 samples/2986 mapped。
  - 双灯真实 Agent（job6，两个 mixed shadowmask 平行光）：4/4 tasks，2 个 `.sdfgz` 各 2 maps，50×50 5000 samples、31×31 1922 samples；`--verify-sdfgz` 6922 samples/5972 mapped，LightID 顺序 `E04E3F61`、`B1EA6248` 与输入一致。
  - SDF group 打包（job7，taskPackSize=256）：2 个 job 合为 1 个 256 group，offsets `(0,0,0)`/`(52,0,1)`，`tasks=3/3`，Dawn 仍输出逐 job 50×50/31×31；非打包分支（job8，512/311 lightmap）各生成单 job group，GroupSize 512/311、offset `(0,0,index)`。
- 待办：`UV2MapSize/UV2Offset` 仍由 `MeshExportOptions` 注入，D3 atlas 计算后填入；`--export <gltf>` 已由 `--export-scene` 覆盖；D4/D5 结果通道已完成 Agent 下载。

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
- [x] D3.4 AtlasNode packer：降序、2048、padding 2
  - `packLightmapSquares()` 按 `S_i` 降序（同尺寸按输入索引稳定排序）用原版 `AtlasNode` 二叉分割打包；默认 atlas 2048。2026-11-26 从 shelf 替换为 `Editor/Utility/RectPackingUtility.cs:284-378` 的 AtlasNode。
  - `padding=2` 为**每块四周**留白，块间净距 4 texel；rect 为实际光图块 `(x,y,S,S)`，不含 padding。
  - 溢出时返回 `false`（D3.5 负责二分缩放/第二 atlas）。
  - `--atlas-test` 覆盖尺寸公式、降序打包、无重叠/越界、确定性、空输入与溢出，并校验 task mapping 升序 + AtlasNode 的 offset；CTest **36/36**。
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
  - 2026-11-21 复跑 deflated + gz-only 包：Agent `Jobs` 目录仅有 `.scenegz` 338B / `.jobgz` 119B（raw 7636/800B，已删除 raw `.scene/.jobs`），官方 Dawn 正常读取并产出 `.lmgz` 42,798B；`--bake --agent` 输出 `phase=done`、`Dawn results: validated`。
  - 同轮修复 Agent 标识大小写：`JobManagement.cs:38` 使用 `%08X` 大写 GUID，但 `buildAgentJobSpec()` 之前把 `jobID` 小写化，导致带 A-F 的 GUID 被 DawnAgent 以 `requested task job ... doesn't match current job ...` 拒绝。现在 `spec.jobId` 按原版格式大写，`taskID` 保持 `guidHex` 大写；`missionID` 仍沿用调用方小写。
  - 2026-11-26 补 lightmap group packing：按原版 `DawnPackLightmapTask` 生成 `MappingGroupJobs`（`(0,0,index,0xFFFFFFFF)`、GroupedSize=taskPackSize、offsets=(x,y,lightmap job index)）；入组 job 不再发单个 `LIGHTMAP2D`，改发 `LIGHTMAP2D_GROUP`，结果仍为每个 job 的 `.lmgz`。job9（taskPackSize=256、双灯）2 个 group task、`Dawn results: validated`，2 个 `.lmgz` + 2 个 `.sdfgz`；`--verify-lmgz` 3145/2729、`--verify-sdfgz` 6922/5972。
  - 2026-11-26 补 lightmap split：按原版 `DawnLightmap2DSplitTask`/`DawnLightmap2DSplitExporter` 为超尺寸未入组 job 生成 `MappingSplitJobs`，`JobID=(splitGroupIndex,103,globalSplitIndex,lightmapJobIndex)`、`SplitSize=taskPackSize`、`OverlapSize=3`、`MappingOffsets=(ix,iy,job)`，Agent 发 `LIGHTMAP2D_SPLIT`；`--verify-lmgz` 按 `DawnLightmap2DSplitImporter.GroupSplitLightmapOutput` 合并。job10（dense 512/311、pack256、samples=4）512 拆 9 tile、311 拆 4 tile，加 2 个 SDF group 共 `tasks=15/15`、`Dawn results: validated`；合并后 512×512/262144、311×311/96721，`--verify-sdfgz` 528392/195938 mapped。`--split-test` 覆盖 split ID/offset/padding/merge。
  - 2026-11-26 补 split→atlas：新增 `readLightmapSidecar()` 和 `--write-job-atlas <job-dir> <guid> <lightmap.json> <out-dir> [edge-padding]`，把 job 结果（含 split merge）按 scene `uv2Offset/uv2MapSize` 与 sidecar rect 匹配后写入所有 atlas DDS；padding job 的结果 header 为 raw+2，匹配必须用 raw `uv2MapSize`/sidecar size，不能再拿结果尺寸匹配；`--verify-job-atlas` 按 `placement-padding` 回读校验；`--bake ... --atlas-out <dir>` 在结果校验后自动查找 sidecar 并走同一路径（对应原版 `DawnEncodeTextures` 自动后处理）；带 `PADDING` 的 block 额外 ring 按 atlas 边距 clamp（默认 2 时通常取 1），非 padding block 仍严格报越界。`--job-atlas-test` 覆盖 sidecar 顺序保持、split merge、padded split、DDS 回读、2 atlas、跨 atlas 相同 rect 的 index-first 匹配和 post-bake 输出（2 atlases/3 blocks/8 split tiles）。真实 job10 生成 1 atlas/2 blocks/358,865 texels 并校验通过；dawnbake CTest **36/36**。
  - Agent task cost 按原版 `TaskManagement.cs:57`、`DawnLightmap2DGroupTask.cs:35-42`、`DawnLightmap2DSplitTask.cs:35-42`、`DawnShadowMaskGroupTask.cs:41-49` 计算：单 lightmap=`w*h*samples`，lightmap group=`groupedSize²*samples*(bounces+1)`，lightmap split=`splitSize²*samples*(bounces+1)`，SDF group=`groupSize²*lightCount*samples*ShadowRayFactor*MaxUpSamplingFactor`；`--agent-spec-test` 覆盖单个/group/split 的 cost 与混合 packed/oversized 路径。
  - 2026-11-26 packer 对齐原版：`packLightmapSquares()`（final atlas，降序）和 `packMappingGroups()`（task mapping，升序）改用 `Editor/Utility/RectPackingUtility.cs:284-378` 的 `AtlasNode`；SDF packed group 通过 `packSdfShadowGroups(jobs, packSize, lightmapGroups)` 直接继承 lightmap `MappingGroupJobs` 的 `GroupedSize`/`MappingOffsets`，未入组 job 按原版升序生成单 job group。`--atlas-test` 增加 mapping offset 断言，`--scene-export-test` 校验 SDF offset 与 lightmap group 一致；dawnbake CTest **36/36**。真实 job7/job9/job10 的旧 offset 是 shelf 版本，新布局待 Agent 复跑。
  - 2026-11-26 补 lightmap `PADDING`：原版 `DawnLightmap2DExporter.cs:161,191` 在 `Allocation.Padding>0` 时写 `LIGHTMAP_FLAGS_PADDING`；C++ 导出现在对 lightmap job 同样按 `config.lightmap.padding>0` 设置，SDF 与 lightmap 的 raw size/padding flag 一致；`--scene-export-test` 断言两者 flag 相同。task mapping/split 的 padded cell 因此统一为 raw size+2。
  - 本轮仍使用 D0 的 CUDA workaround `Dawn.exe`（OIDN CPU 初始化失败未修复），仅作 D4/D5 验证，不进入交付。
  - `GetJobResult` 收到 `D.O.N.E` 后调用 `TryCancel()` 再 `Finish()`，避免 server stream 已成功但客户端 `Finish` 偶发挂起。
- [x] D4.6（备选）文件上传
  - 已实现 `AgentClient::uploadFile()` + `--agent-upload`，按原版 Agent 修复：首包带 `jobID/filePath/fileSize/hash`（SHA1 大写）；先读取 `NotInCache` 再发送 3 MiB 数据，`DoneInCache` 直接成功，空文件不发送数据包。新增 `--agent-upload-test` / CTest `dawnbake_agent_upload` 覆盖非空、空文件、>3 MiB 多分块、cache hit、Fail 和缺失源文件；真实 DAWNAgent 上传 `hbg_upload_probe.txt`，Agent 日志 `Succeed recive file hbg_upload_probe.txt`。
  - 真实 6 MiB 大文件复验（原版 T6 active job `44444444000000000000000000000006`）：`--agent-upload` 上传 6,291,456B，334ms，Agent 日志 `Succeed recive file hbg_upload_large.bin`；随后 `--agent-download` 回读 6,291,456B，704ms，SHA-256 完全一致，验证 3 MiB 多分块与 3 MiB 边界后的 `fileDataDone`。
  - 已实现 `AgentClient::batchUploadFileInServerCache()` + `--agent-cache-upload`，按原版 `Agent.proto:49-50,127-138` 的 `BatchUploadFilesInServerCacheReq` 构造 `jobID + repeated UploadFileInfo{hash,filePath}`，不带 `fileData`；ack 数量必须与请求一致，接受 `Done/DoneInCache`，`NotInCache/Fail` 返回 remote path + per-file message。
  - 新增 `--agent-cache-test` / CTest `dawnbake_agent_cache`：进程内 gRPC fake `AgentService` 校验 jobID、hash/filePath 顺序、成功 ack、cache miss 和 per-file `Fail` message。
  - 真实 cache 复验（同一 T6 active job）：先经 `UploadMultiFiles` 上传 `hbg_multi_a.bin`，再 `--agent-cache-upload` 同 hash/path，Agent 日志 `1 found in cache, 0 not in cache, 1 succ copied`；换成全零 hash 返回 `NotInCache`（exit=1），与原版 `BatchUploadFileInServerCache` 分支一致。
  - 已实现 `AgentClient::downloadFile()` + `--agent-download`，按原版 `Agent.proto:58,155-173` 的 `DownloadFileReq/DownloadFileAck` 流式接收 `Data/DataDone/Fail/Fail_FileNotFound`；`Data` 和 `DataDone.fileData` 都写入（原版 Agent 短文件只在 `DataDone` 带 payload），先写 `.part`，`DataDone` 后原子替换目标，失败清理半文件。
  - 新增 `--agent-download-test` / CTest `dawnbake_agent_download`：进程内 fake Agent 覆盖分块成功、`DataDone` 带 payload、空文件、not found、fail；当时 dawnbake **26/26**。
  - 真实官方 Agent 验证（job `8888888899999999AAAAAAAAAAAAAAAA`，T6 active）：`--agent-download ... v2.000...05.lmgz` 得到 **184,476B**，SHA-256 与 Agent 作业目录源文件完全一致；修复前因漏写 `DataDone.fileData` 得到 0B。
  - 对照 `DAWNAgent.dll`/`DAWNClientAgent.dll` 实际协议发现 Editor 旧 `Agent.proto` 落后：实际 DLL 还有 `DownloadMultiFiles` RPC、`DownloadFileAck.filename=5`、`ClientAgentBeginJobAck.isSupportDownloadMultiFiles=5`。原版 ClientAgent 在任务结果到达时调用 `DownloadMultiFiles`（`DAWNClientAgent/JobExecutor.cs:1454`）并逐文件校验 hash。已补齐 `AgentClient::downloadMultiFiles()` + `--agent-multi-download-test` / CTest `dawnbake_agent_multi_download`：支持同一 ack 内多文件连续 Data/DataDone、逐文件 SHA1（`FileCacheSystem.ComputeFileHashAsync`，大写 hex）校验、`.part` 原子提交；fake 覆盖 empty/DataDone payload、interleaved 校验、not found、Fail、errMsg、hash mismatch。真实官方 Agent 的 ClientAgent 日志 `/Got DownloadMultiFiles 1 files/` 与该 RPC 一致。自动结果下载已改为 `downloadFiles()`：先 `DownloadMultiFiles`，RPC 不支持或失败时回退逐个 `DownloadFile`；`RunJob` 的 GeneralAck 不含支持位，原版从 `BeginJobAck.isSupportDownloadMultiFiles` 判定。新增 `--agent-download-files-test` / CTest `dawnbake_agent_download_files`，覆盖批量成功和 `UNIMPLEMENTED` 回退。另有 `--agent-force-download`（需同时 `--agent-download-results`）强制重下已存在结果；真实 DAWNAgent 2.4.5 复跑 job `44444444555555556666666677777777`，输出 `DAWN_PROGRESS phase=done completed=1 total=1`、`Dawn results: validated`，结果 190,301B，Agent 日志 `Got DownloadMultiFiles 1 files`，客户端 SHA1 校验通过，`--inspect-lmgz` 为 64×64/4096 samples。
  - `--bake ... --agent ... --agent-files ... --agent-download-results` 已接入 `runJob()`：收到 `D.O.N.E` 后、`EndMission` 前仅对缺失的 task result 调用 `DownloadFile`，此时 Agent 仍处于 current job；下载后再统计 `available` 并校验 `.lmgz`。`--agent-download-results` 必须同时提供 `--agent` 和 `--agent-files`。原版 Agent `DownloadFile` 只服务 `_curJobContext` 并校验 JobID，作业结束后单独调用会报 `doesn't match current job`。
  - 已实现 `AgentClient::uploadMultiFiles()` + `--agent-multi-upload`，按原版 `Agent.proto:143-152` 的 `UploadMultiFilesReq` 内联每个 `UploadFileInfo.fileData`，校验 ack 数量及 `Done/Fail`。
  - 新增 `--agent-multi-upload-test` / CTest `dawnbake_agent_multi_upload`：fake Agent 校验 hash/path/payload、成功 ack 和 per-file fail；当时 dawnbake **27/27**。
  - 真实官方 Agent 多文件复验（T6 active job）：`--agent-multi-upload` 一次上传 2 个文件（1 MiB + 2 MiB，SHA1 大写），303ms，Agent 日志 `Succeed recieved 2 files, 0 failed`；随后两个文件经 `DownloadFile` 回读，SHA-256 均与源文件一致（合计 659ms）。
  - 已实现 `AgentClient::uploadJobFilesToAnotherAgent()` + `--agent-transfer-job`，按原版 `Agent.proto:61,176-180` 的 `UploadToAnotherAgentReq{jobID, anotherAgentAddress, filePaths}` 调用 `GeneralAck`，`code!=0` 返回 message。
  - 新增 `--agent-transfer-test` / CTest `dawnbake_agent_transfer`：fake Agent 校验目标 Agent、jobID、filePaths 和 `Fail` ack；dawnbake **28/28**。
  - 本轮官方 Agent 作业中 `UploadMultiFiles` 已实际收到 6 个 job 文件（含 scenegz/jobgz/meshgz/matgz），上传路径验证通过；本地 Agent 共享 job 目录仍不需要上传。
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
  - `--sh-atlas-dds-verify-test` 先写合成 `.lmgz`、经 D5.3/D5.4 生成并写出 DDS，再读回校验 3 个 mapped texel 的 4 分量；修改一个源 incident 分量后必须报 `texel mismatch`。CLI `--verify-sh-atlas-dds` 接受 `<file.lmgz> <dds-dir> <atlas-index> <target-x> <target-y> <atlas-size>`；CTest 新增 `dawnbake_sh_atlas_dds_verify`，当前 **23/23**。
- [x] D5.7 sidecar 合并（保留原有字段，新增 `lightmap` 段）
  - `dawn_package` 新增 `mergeLightmapSidecar()`：读取已有场景 `.hdrp.json` 和 D3 fragment，验证 fragment 的 `lightmap.version/texelPerUnit/atlases/objects`（atlas 索引与 rect 边界）后，只替换根 `lightmap`；用 `nlohmann::ordered_json` 保留场景原有字段及其顺序（`hdrp`、`materials`、`lights`、`objects`、自定义字段）。
  - 原版结果映射参考：`DawnBakingResult.cs:94-104` 把 `LightmapIndex` + `LightmapScaleOffset` 记入 MeshInfo，`:777-778` 导入时写回 `Renderer.lightmapIndex/lightmapScaleOffset`；`DawnLightDataAsset.cs:331-379` 把 lightmapIndex/ST 写入 LightmappedRendererData。我们的 sidecar 以对象名 + `atlas/rect/st` 表达同一信息。
  - CLI `--merge-sidecar <scene.hdrp.json> <lightmap.json> [<out.hdrp.json>]`，省略输出时原地更新；自动创建输出父目录，重复合并幂等。`--sidecar-merge-test` 覆盖字段保留、stale lightmap 替换、原地合并、幂等、坏根对象、缺 lightmap、坏 JSON、越界 rect 和缺失场景文件；CTest 新增 `dawnbake_sidecar_merge`，当前 **23/23**。

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
- [x] D7.3 灯光单位校准（1/2/4m 衰减）
  - 新增 `hbg_dawnbake --export-falloff <dir> [guid32hex]`：4m×4m 白地板，法线 +Y，albedo=1；三盏无阴影点光位于法线正上方 `y=1/2/4m`，颜色分别 R/G/B，intensity=24、range=10、黑天空，128×128 lightmap，仅 direct job。
  - 原版参考：`DawnLightExporter.cs:351-375` 点光 `Attenuation=1/range`、`FalloffExponent=1`；`DawnVirtualLight.cs:64` 注明 `Attenuation` 用于 LightGridCalc；`DawnLightExporter.cs:328` 的 `LightPower=GetLightPower(indirectColor)`、`:1131-1133` 的 `GetPower()` 为颜色灰度×强度，是元数据而非直接辐射倍率；`isInverseSquared` 分支见 `:275-280`。
  - Dawn Agent 实烘（`v2.00000000000000000000000000000005.lmgz`）：中心 `IncidentLighting=(23.6138,5.9734,1.4554)`，`SHVectorR=(0.4939,0.9998,0.4940,0.9998)`。归一化比值 `G/R=0.25296`、`B/R=0.06163`，理论值 `0.25/0.0625`；按 R=24 归一为 `(24,6.071,1.479)`。三灯 `LightPower` 分别为 `7.176/14.088/2.736`，但输出只随 `intensity/d²`，证明 `LightPower` 不参与直接亮度缩放。
  - **结论：Dawn 点光有效辐射为 `Color * intensity / distance²`（本场景 normal 正对光源，cos=1）；`Attenuation=1/range` 是 LightGrid/包围盒字段，`FalloffExponent=1` 在 inverse-square 路径被忽略；现有 `exportLight` 与原版一致，无需额外灯光单位系数。**
- [x] D7.4 T1 色差 < 5%
  - 新增 `hbg_dawnbake --export-t1-color <dir> [guid32hex]`：沿用 T1 单面单平行光，输入 sRGB 颜色 `(0.8,0.6,0.4)`、intensity=0.5、黑天空、direct-only、albedo=1、64×64 lightmap；`exportLight` 以 `srgbToLinear=true` 走原版线性颜色路径。
  - 原版参考：`DawnLightExporter.cs:238-239` 写 `Color`/`Attenuation`，`DawnExporter.cs:170` 的 `ToFloat3(HDRColor)=baseColor*intensity`；`DawnEncodeTextures.cs:457-464` 在线性/HDR 路径直接 `ToColor`、不额外 gamma，`:148` 使用 RGBAHalf 线性纹理；`DawnLightmap2DImporter.cs:53` 读 `IncidentLighting`。当前 shader `hbgDecodeBakedGI` 也保持线性。
  - Dawn Agent 实烘中心 `IncidentLighting=(0.301884,0.159252,0.066417)`，解析预期 `(0.301914,0.159273,0.066434)`；逐通道相对误差最大 **0.0259%**（均值 0.0164%），CIE76 ΔE=**0.0032**，满足 <5%。`SHVectorR=(0.499861,0.999723,0.499861,1.000254)`。
  - **结论：T1 线性色差通过；原版线性颜色/强度路径、RGBAHalf 写入和运行时解码没有引入 gamma 或通道缩放。**
  - 验证：`--write-lightmap-dds ... 128 2 2` + `--verify-sh-atlas-dds` **OK**（4096 mapped）；`--light-test` 通过；`ctest -R dawnbake` **20/20**。
- [x] D7.5 T3 接缝无黑缝/串色
  - 新增 `hbg_dawnbake --export-t3-tile <dir> <0..3> [guid32hex]`：2m×2m 单块地板，四块中心 `(-1,-1)/(1,-1)/(-1,1)/(1,1)`，全局点光 `(0,3,0)`、intensity=24、range=10、黑天空、direct-only、64×64 lightmap，albedo=1。
  - 四块分别用 `--export-t3-tile` + Dawn Agent 实烘，再以 `--write-tile-atlas <out> 256 2 <tile0.lmgz> 2 2 <tile1> 70 2 <tile2> 2 70 <tile3> 70 70` 合并 256×256 atlas；每块 payload 64×64、块间 4px 间隙，`LightmapAtlasOptions::edgePadding=2` + 默认 4 次 3×3 膨胀，payload 外 ring 也写入 atlas。
  - 原版参考：`DawnDilateTexture.cs:24-99` 4 次迭代；`DawnPackLightmapTask.cs:30-56` 的 `UV2Offset/UV2MapSize`；`DawnLightmap2DImporter.cs:40-44` 的 `UVWidthPixel + 2*Padding`；`DawnEncodeTextures.cs:416-421` 的膨胀调用。运行时仍按 sidecar `rect/st` 映射到 payload，ring 只服务边界 bilinear。
  - 原始 atlas 边界采样：相邻 tile 世界边界的 `IncidentLighting` 最大相对差 **0.0129%**（4 条边均 <0.02%），远低于 5%；四个 padded block 共 **18,496** texel mapped，ring 无零值/黑边。
  - D3D11 1280×720 `--debug-view=lightmap` 截图（`hbg_d75_runtime	3_seam_final.png`）为连续径向渐变，中心无黑缝/串色；截图曝光只看 0.25× 可视化缩放，原始 DDS 未缩放。
  - 结论：T3 **通过**；采用“逐块 Dawn bake + D5.3/D5.4 atlas 合并”的 v1 路径，避免多 task 直接映射，同时覆盖块内膨胀、块间 padding 和运行时 bilinear 边界。

- [x] D7.6 T6 性能：打包 < 2s（100 物件）、显存 < 100MB
  - 新增 `hbg_dawnbake --export-t6 <dir> [guid32hex]` 与 `--t6-test`：10×10 共 100 个 2m 白地板，每块 64×64 lightmap、albedo=1，全局点光 `(0,12,0)`、intensity=1000、range=100、黑天空，100 个唯一 mesh/job/sidecar 对象、direct-only；默认 atlas `2048`、padding 2。
  - 原版参考：`DawnLightmap2DExporter.cs:98` 为每个 lightmap 分配 `(0,0,index,5)` TaskGuid；`DawnJobExporter.cs:34` 以 `MaxLightmapSize` 调用 `PackLightmaps`；`DawnPackLightmapAtlas.cs:87-224` 按尺寸排序、单 atlas 满后缩放/新建；`DawnLightmapSizeCalculate.cs:11-17,32-44` 的尺寸公式；`DawnEncodeTextures.cs:148-149` 的 RGBAHalf 颜色图；`DawnLightmap2DImporter.cs:53,74` 读取 IncidentLighting/SHVectorR。运行时按 `src/render/lightmap/lightmap.cpp:124-130,158-166` 创建 3 层 RGBA16F texture array。
  - 本轮 CPU 打包（`packLightmapAtlases`，100 个 64×64）多次约 **1.0–3.3 ms**（Debug；三次 1.028/1.098/1.082 ms），远低于 2s；`--export-t6` 完整写包 137ms（含 100 个 meshgz、scene/jobs/sidecar）。
  - Dawn Agent 实烘（GUID `44444444000000000000000000000006`，`--timeout 1200`）：**100/100 tasks、wall 55,491 ms**；100 个 `.lmgz` 的 `ExecutionTime` 合计 **707.325 s**（多 Agent 并行，故 wall 小于总和）。结果目录 `C:\Users\chenfuling\AppData\Local\Temp\hbg_d76`，单结果 64×64/4096 samples。
  - 显存：1 张 2048 atlas × 3 层 RGBA16F = `2048*2048*3*4*2 = 100,663,296 B = 96 MiB`；三个 DDS payload 合计相同，另各有 148B DX10 头。满足项目 `<100 MB`（MiB 口径）。
  - 将 100 个 `.lmgz` 按 sidecar rect 合成 2048 atlas（`--write-tile-atlas ... 2048 2`）成功：100 blocks、3 个 DDS 各 32 MiB payload；该合并+4 次膨胀耗时 6.8s，属于 D5 后处理，不计入 D7.6 的“打包 <2s”。
  - 验证：`ctest -R dawnbake` **21/21**；`--t6-test`、`--proto-test`、`--golden-test`、`--sh-atlas-test` 通过。全量 CTest **45/45** 通过（D7.6 当时；D7.7 全量重建后暴露外部 VFX binary v3/v2 资产不一致，见 D7.7）。
- [x] D7.7 校准值写入 `config/dawn.json` 与主文档 §15
  - 新增 `config/dawn.json` v1：`uv2VFlip=false`、`texelsPerUnit=6.0`、单块 `maxLightmapSize=1024`、`atlasSize=2048`、`maxAtlases=2`、`padding=2`、`dilationIterations=4`、`directional=true`、`rgba16f/3 layers`；`colorSpace=linear`、`directionalLightmapScale=1.0`、`lightUnitScale=1.0`、`pointFalloff=inverse-square`、`lightPowerAffectsRadiance=false`。
  - 新增 `dawn_config.h/.cpp` 与 `--dawn-config-test [path]`：严格校验 schema、未知字段、类型、范围和上述标定值；同时校验 `LightmapUvOptions`、`LightmapSizeSettings`、`LightmapPackingSettings` 及 `LightmapAtlasOptions::dilationIterations` 的代码默认值与配置一致。`--export-t2` 与 `--export-t6` 已从配置读取 UV 翻转、atlas/padding、方向标志、层数/格式、线性色空间和灯光单位系数。
  - 原版参考：`DawnSettings.cs:122-131,827-834`（`MaxLightmapSize/TexelPerUnit/Padding`）、`DawnLightmapSizeCalculate.cs:11-17,32-44`、`DawnPackLightmapAtlas.cs:320-324`（普通 mesh LightmapScaleOffset y 为正）、`DawnLightmap2DExporter.cs:193`（方向 flag）、`DawnLightmap2DImporter.cs:53,74`（两层语义）、`DawnEncodeTextures.cs:148-149`（RGBAHalf 线性）、`DawnLightExporter.cs:275-280,328,351-375`（isInverseSquared/Attenuation/LightPower）。`edgePadding` 仍按 D7.5 命令行显式传 2，不写入全局默认。
  - 验证：`--dawn-config-test` 输出 `dawn config OK: atlas=2048 texels_per_unit=6.0 padding=2 layers=3`；`--t6-test` 通过；CTest 新增 `dawnbake_config`，D7.7 当时 dawnbake **24/24**、非 VFX 用例 **25/25**、全量 CTest **67/67**。
- [!] D7.8 与 Unity 烘焙同场景对比报告（图 + 数据）— **Unity Editor 实机截图阻塞，参考路径对比已完成**
  - 本机无可用 Unity Editor：`E:\Learn\Unity\Learn` 为 `2021.3.8f1c1`，但 `D:\Soft\Unity Hub\Editor` 不存在，原版 Dawn 插件也未安装；因此无法重烘 Unity 并截 Game View。结论不能替代 D0.12。
  - 已完成 `docs/dawn-gi-unity-comparison.md`：对同一份官方 Dawn T2 `.lmgz`（128×128、rect `[2,2]`、2048 atlas）分别走**原版 Unity 导入语义**（`DawnEncodeTextures.cs:148-149`：Incident RGBAHalf、Direction RGBA32）与**HBG 运行时**（三层 RGBA16F、`baked_gi.sc:8-21`），Unity 解码公式取自 `EntityLighting.hlsl:328-358`。
  - 数据（按 Unity `Color32` 截断 + 线性采样模型）：T2 方向 8-bit 编码最大误差 `0.0039168`、解码最大绝对差 `0.0197983`、平均 `0.0005640`、PSNR **58.39 dB**；T1 最大亮度差 `0.003392`（0.339%）。Unity 8-bit 方向量化后 runtime 640×360 仅 8,668/230,400（3.76%）像素不同，最大 **2 LSB**，仅 1 个像素 >1 LSB，PSNR **66.97 dB**。HBG 16F 精度高于 Unity 原版，不是方向/公式错误。
  - 色彩空间已按原版路径收敛：`DawnBakingResult.cs:479,527-534` 把方向纹理导出为 `.png`，按默认无 `DirectionalMapImporterSettings` preset 路径以 `TextureImporterType.DirectionalLightmap` 导入；Unity 生成的该类型资源 meta 为 `textureType: 12`、`sRGBTexture: 0`。原版只在 `Default`/`SingleChannel` 显式设 `sRGBTexture=false`，`DirectionalLightmap` 依赖 Unity 类型默认，故仍需 Editor 确认 type setter 行为。4 参数 `Texture2D(..., RGBA32, false)` 的 `false` 是 `mipChain`，不再作为 sRGB 风险依据。正常路径先 `ImportAsset` 生成 Importer；`Importer == null` 的 fallback `UpdateTextureMetaFile` 只改 textureType/maxSize/compression，不写 sRGBTexture。仍需验证 `SetPixels/EncodeToPNG` 是否原样保留 8-bit 字节；自定义 preset 需单独核对。
  - 图：`docs/images/dawn-d78/lightmap_compare.png`、`docs/images/dawn-d78/runtime_compare.png`；数据：`docs/images/dawn-d78/metrics.json`；分析工具：`tools/dawnbake/unity_reference_compare.py`（支持 `--write-unity-dds` 与 `--write-unity-png`）。
  - 待补：安装/指定 Unity 2021.3.8f1c1 + Dawn 2.4.5，重烘同场景并截 Game View；届时把实机截图加入报告后才能关闭 D7.8。
  - 已知偏差：HBG `baked_gi.sc` 对最终辐亮度做 `max(...,0)`，Unity `SampleDirectionalLightmap` 不裁剪；T1/T2 的 +Y 法线未触发，背面/反向法线待测。原版方向贴图未显式设 wrap/filter，HBG 使用 clamp，rect 内由 D7.5 padding 覆盖。

**D7 出口条件**：T1–T6 全部通过并记录；D7.8 参考路径已完成，Unity Editor 实机截图待 D0.12。

---

## D8 收尾（3–5 天）

- [x] D8.1 CLI 文档：`--help` + README 段落
  - `hbg_dawnbake --help` 退出 0，覆盖全部命令（含 D7.8 侧 `--dawn-config-test`、`--protocol-version-test`、`--agent-spec-test`）；README 已同步 `--protocol-version-test`、`--agent-spec-test` 和直启错误处理说明。
- [x] D8.2 错误处理：缺 DLL / 超时 / 磁盘满 / 版本不匹配
  - 缺可执行文件、缺 `<guid>.scene/.jobs`、非法 GUID、超时杀进程已有 `--process-test`；新增缺失可执行文件断言。
  - `CreateProcessW`/日志创建遇到 `ERROR_MOD_NOT_FOUND` 追加“缺少 DLL”提示，`ERROR_DISK_FULL`/`ERROR_HANDLE_DISK_FULL` 追加“disk full”；`writeFile`/gzip 写入失败同样识别 `ENOSPC`。磁盘满未实际制造，仅做错误映射。
  - scene/jobs 读取新增版本范围校验（`1000..1002`），越界在解析前报 `version ... is not supported`；新增 `--protocol-version-test` 和 CTest `dawnbake_protocol_version`。
- [x] D8.3 `ctest` 增加 dawn 用例（golden round-trip、packer、lmgz reader）
  - `dawnbake` **31/31**：golden structure/round-trip、atlas pack、lmgz reader、DDS verify、config、process、protocol version、agent spec、agent upload、agent cache、agent multi-upload、agent transfer、agent download、agent multi-download、agent download files 等均通过。
- [x] D8.4 回归：Dawn 新增用例全绿；Dawn/非 VFX 33/33 通过；全量 101 项待工作区空闲复跑
  - Dawn/非 VFX 33/33 通过（`hbg_tests`、dawnbake 31、`hbg_idl_check`）；`dawnbake_agent_cache`/`transfer` 的偶发 ctest 挂起已通过释放 AgentClient channel 后再 Shutdown 修复。全量测试集已被并行会话扩到 101 项；2026-11-22 两次全量运行分别受后台 shader_build 和并行 `hbg_app` 重建干扰，不计为 Dawn 回归失败。
  - `hbg_vfx_reference_sorted_262144_vulkan` 在全量并发下曾超时一次，隔离复跑 67.9s 通过；按外部 VFX 负载敏感记录，不计入 Dawn 回归。
- [x] D8.5 清理：不提交 `Jobs/`、`_FileCache/`、临时 DDS
  - 删除零字节 `atlasout.txt`；`.gitignore` 增加 `/Jobs/`、`/_FileCache/`、`/atlasout.txt`；`git status` 无 `Jobs/`、`_FileCache/` 或临时 DDS。
- [!] D8.6 主文档状态更新为"已完成" — **D0.9/D0.12、D7.8 Unity 实机截图未关闭，不能标“已完成”；D0.10 仅日志清理**

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
| T3 接缝 | D3D11 | 1280×720 | 邻边最大相对差 0.0129%，18,496 texel mapped | 四块 2m 地板逐块 bake + 256 atlas，edgePadding=2 |
| T4 forward vs deferred | D3D11（Vulkan 待外部 shadowSat 修复） | 1280×720 | PSNR=∞（逐字节一致） | `triangle_lm_ds` forward composite vs deferred |
| T5 SSGI 互斥 | D3D11 | 1280×720 | 逐字节一致（PSNR=∞） | 无 LM 控制 45.107 dB，证明 SSGI 生效 |
| T6 打包/烘焙耗时 | - | - | pack **约 1–4 ms**；Agent **55,491 ms / 100 tasks**；VRAM **96 MiB** | 100 个 64² lightmap、2048 atlas；见 D7.6 |

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
| 2026-11-21 | D7.8：Unity 线性 8-bit 参考路径解码对比；T2 解码 PSNR 58.39 dB、runtime 3.76% 像素差最大 2 LSB；DirectionalLightmap 导入 `sRGBTexture:0` 有强证据但依赖 type setter 默认，PNG 字节路径与 Unity Editor 实机截图待 D0.12 |
| 2026-11-21 | D8.1/D8.2/D8.3/D8.4/D8.5：CLI/README 核对、缺 DLL/磁盘满/版本错误处理、协议版本 CTest；重建 adapter 后全量 CTest **77/77**；dawnbake 28/28、非 VFX 30/30；清理 atlasout 并补 ignore |
| 2026-11-21 | D1 gzip：`.scenegz/.jobgz` 从 stored 改为 zlib deflate，10B header 对齐 .NET `GZipStream`；`--package-test` 校验解压一致/压缩生效，.NET 双向兼容实测通过；`--roundtrip` 支持 deflate |
| 2026-11-21 | D4.5 gz-only Agent：job 目录仅保留 deflated `.scenegz`/`.jobgz`，原版 DawnAgent/Dawn 仍完成并 `Dawn results: validated`；`buildAgentJobSpec()` 回退 `.jobgz`，`jobID` 改为原版 `%08X` 大写，修复含 A-F GUID 的大小写拒绝；新增 `--agent-spec-test` / CTest `dawnbake_agent_spec` 锁定 gz-only 与大小写 |
| 2026-11-21 | D4.6 upload：按原版 Agent `UploadFile` 修复首包缺 `hash`、未处理 `NotInCache`、空文件多发数据包；新增 `dawnbake_agent_upload` 覆盖 SHA1/空文件/cache hit/Fail；真实 Agent 上传 `hbg_upload_probe.txt` 成功；dawnbake 31/31 |
| 2026-11-22 | D8.4 回归：Dawn/非 VFX 33/33；`hbg_vfx_asset_check` 重新生成过期 `lit_visual.vfxb` 后 355s 通过；全量 93 项快照未取得单次全绿，当前测试集已扩到 101 项 |
| 2026-11-22 | D0.9 进程级干净 bundle：仅 Dawn.exe/Dawn.data/OIDN/TBB + 系统 PATH；默认与 `denoiserMode=2` 均 exit=0、`Lighting complete`；系统 Vulkan 校验层仍被加载，D0.9 保持未关闭 |
| 2026-11-22 | D0.10 修正：`denoiserMode=2` 强制 CPU OIDN，官方 exe 直启 `DenoiserMode:OIDN` + CPU 设备信息 + `Lighting complete`；Agent 实烘 196,921B `.lmgz`，`--inspect-lmgz` 64×64/4096、validated。CPU OIDN 不再是功能阻塞，仅剩空指针日志 |
| 2026-11-22 | D0.8 复测解除：官方未打补丁 `Dawn.exe` 直启 5/5 exit=0、`Lighting complete`，T6 100 job 直启 26.8s 完成，Agent 路径 1/1 产出 `.lmgz`；OIDN CPU 仍报 null，但回退 OptiX，不再崩溃 |
| 2026-11-21 | D4.6 真实多文件上传/cache：官方 active T6 job 上 `UploadMultiFiles` 上传 1 MiB + 2 MiB 两个文件（303ms，日志 `Succeed recieved 2 files`），`DownloadFile` 回读 SHA-256 一致；随后 `BatchUploadFileInServerCache` 命中缓存，错误 hash 返回 NotInCache |
| 2026-11-21 | D4.6 真实大文件上传：官方 DAWNAgent active T6 job 上传 6 MiB `hbg_upload_large.bin`（334ms，日志 `Succeed recive file`），再用 `DownloadFile` 回读，SHA-256 一致（704ms） |
| 2026-11-21 | D4.6 upload 分块/测试稳定性：按原版 3 MiB 分块并新增 >3 MiB fake 上传；fake Agent client channel 在 Shutdown 前释放，修复 cache/transfer ctest 偶发挂起；排除外部 VFX asset_check 后全量 88/88 |
| 2026-11-21 | D4.6 cache upload：按 `Agent.proto` 实现 `batchUploadFileInServerCache()` + `--agent-cache-upload`（hash/filePath，不带 fileData），ack 数量/枚举/路径错误处理；新增进程内 fake Agent gRPC 测试 `dawnbake_agent_cache`；`dawnbake_result_poll` 成功分支 timeout 5s→15s 以消除全量并发抖动；dawnbake 25/25、全量 CTest 74/74 |
| 2026-11-21 | D4.6 download：按 `Agent.proto:58,155-173` 实现 `downloadFile()` + `--agent-download`（Data/DataDone/Fail_FileNotFound、`.part` 原子提交）；新增 fake Agent 测试 `dawnbake_agent_download`（分块/空文件/not found/fail）；当时 dawnbake 26/26、全量 CTest 75/75 |
| 2026-11-21 | D4.6 download 修复：原版 Agent 短文件把数据放在 `DataDone.fileData`，客户端此前只写 `Data` 导致 0B；修正后真实 T6 active job 下载 184,476B，SHA-256 与源文件一致；自动结果下载改到 `runJob()` 内、`EndMission` 前，要求 `--agent-files` |
| 2026-11-21 | D4.6 multi upload：按 `Agent.proto:143-152` 实现 `uploadMultiFiles()` + `--agent-multi-upload`（内联 fileData，Done/Fail ack）；新增 fake Agent 测试 `dawnbake_agent_multi_upload`；当时 dawnbake 27/27、全量 CTest 76/76 |
| 2026-11-21 | D4.6 inter-agent transfer：按 `Agent.proto:61,176-180` 实现 `uploadJobFilesToAnotherAgent()` + `--agent-transfer-job`（GeneralAck code/message）；新增 fake Agent 测试 `dawnbake_agent_transfer`；dawnbake 28/28、全量 CTest 77/77 |
| 2026-11-21 | D4.6 multi-download：对照 DAWNAgent 2.4.5 实际协议补 DownloadMultiFiles（旧 Editor proto 缺失）、DownloadFileAck.filename=5、SHA1 大写 hex 校验；fake 覆盖多文件连续流、empty、not found、fail、errMsg、hash mismatch；dawnbake 29/29、全量 CTest 79/79 |
| 2026-11-21 | D4.6 自动结果下载：新增 `downloadFiles()`，优先 `DownloadMultiFiles`，RPC 不支持/失败时回退逐个 `DownloadFile`；新增 `dawnbake_agent_download_files` 覆盖批量成功与 `UNIMPLEMENTED` 回退；dawnbake 30/30、全量 CTest 84/84 |
| 2026-11-21 | D4.6 强制重下实测：新增 `--agent-force-download`；真实 DAWNAgent 2.4.5 job `44444444555555556666666677777777` 强制经 `DownloadMultiFiles` 重下 190,301B，SHA1 校验通过，`Dawn results: validated` |
| 2026-11-21 | D0.9 部分验证：exe+运行 DLL 独立目录、`PATH=System32;Windows`，package/agent spec/cache/multi-upload/transfer/download/multi-download 全部通过；未覆盖干净 OS/驱动，D0.9 仍保持未关闭 |

