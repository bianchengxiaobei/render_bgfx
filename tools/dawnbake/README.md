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

# 实例尺寸 + shelf pack + 100 块 stress 自检（D3.3–D3.8）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --atlas-test

# rect + UV2 chart 调试图（D3.7）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --dump-uv=out\uv_debug.png

# 完整 job 目录自检（临时目录，自动清理）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --package-test

# .lmgz 解析 / 查看
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --lmgz-test
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --inspect-lmgz out\v2.00000000000000000000000000000005.lmgz

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

# .lmgz -> atlas DDS（<atlas-size> <x> <y>）
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --write-lightmap-dds out\t2\v2.00000000000000000000000000000005.lmgz out\t2 2048 2 2

# 读 golden → 重写 → byte-diff
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --roundtrip `
  tests\dawn\golden\min_scene\11111111222222223333333344444444.scene `
  tests\dawn\golden\min_scene\11111111222222223333333344444444.jobs

# 查看文件头与对象数量
.\build\vs2022\tools\dawnbake\Debug\hbg_dawnbake.exe --inspect <file.scene|scenegz|jobs|jobgz|mesh|meshgz|mat|matgz>
```

`.scenegz/.jobgz` 当前使用 gzip stored blocks（格式合法、无压缩）；本地直读的 `.scene/.jobs` 同时保留。`--roundtrip`/`--inspect` 支持 stored gzip 及 mesh/material。

T2 校准结论：`uv2.y` 不取 `1-v`；Dawn `IncidentLighting` 与运行时 DDS/UV 行序一致。

T1/D7.2 结论：Dawn `SHVectorR` 是 Unity 方向贴图编码，不是 SH 系数；+Y 单平行光输出 `SHVectorR=(0.5,1.0,0.5,1.0)`、`IncidentLighting=1`。运行时层 0 存 `IncidentLighting`，层 1 存 `SHVectorR`，按 `dot(n, direction.xyz-0.5)/direction.w` 解码，缩放系数 1.0。

D3 UV2 使用固定 xatlas commit（`jpcy/xatlas@f700c779`），可用 `-DHBG_XATLAS_DIR=<local checkout>` 切到本地副本。`--uv-test` 验证 `resolution=0` 的 texel 单位语义、自动 atlas、统一归一化，以及空/无索引/零面积 mesh 的 `tryUnwrapLightmapUv()` 跳过告警；D2 exporter 喂入生成 UV2 时需 `normalizeUv2=false`。`--atlas-test` 验证原版 `DawnLightmapSizeCalculate` 尺寸公式、2048 shelf pack（每块 padding 2）、单/双 atlas 缩放回退、超限报错和 100 块 stress。`--dump-uv` 输出 rect/UV2 chart PNG；`--export-min` / `--package-test` 还会写出 `<guid>.lightmap.json`，供 D5.7 `--merge-sidecar` 合并及 D6 运行时解析。

D5.1 `.lmgz` 读取按原版 `FLightmap2DOutput`：48B header → jobInputIndex → 3 个 PRT flag → Lights → 192B `FLightmap2DSample` 数组，无版本头。`--lmgz-test` 写入/读回合成 output；`--inspect-lmgz <file>` 打印 header/size/samples/lights。`gzipDecompress()` 在链接 zlib 时支持标准 deflate，否则只支持 stored block；原版 `DawnLightmap2DImporter.cs:9-23` 使用 `SerializeObject` 读取该文件。官方 Agent 实跑样本 `v2.00000000000000000000000000000005.lmgz` 已验证（约 43KB，64×64，4096 samples）。

D5.2 结果绑定：`lightmap_result.h/.cpp` 的 `validateLightmap2DResult()` 检查 `JobID/MeshID`、正尺寸、`HeaderInfo.Size` 与 `FLightmap2DInput.Size` 的 padding 关系（无 padding 时相等；有 `LIGHTMAP_FLAGS_PADDING` 时各边 +2，对齐原版 importer），以及 `LightmapData.size == Size.x*Size.y`；`bindLightmap2DResult()` 用 `JobID` 唯一匹配 `BakingJobInputs.lightmap2DJobs`、用 `MeshID` 唯一匹配 `SceneInfo.meshInstances[].header.guid`。`--verify-lmgz <job-dir> <guid32hex>` 支持 raw 或 gzip scene/jobs，逐个校验 `.lmgz`；`--bake` 返回成功前也会执行。原版尺寸断言参考 `DawnLightmap2DImporter.cs:30-44`。

D5.3 atlas：`lightmap_atlas.h/.cpp` 的 `buildLightmapAtlasImages()` 复用 D5.2 binding 和 D3 placement；D7.2 后层 0 写 `IncidentLighting`（RGBA16F），层 1 写 `SHVectorR`（方向+方向性），层 2 为零；目标原点按原版 `UVWidthPixel + 2*Padding` 规则取 `D3 data rect - padding`，`bIsMapped` 另存为 mask。`--sh-atlas-test` 覆盖 padded/unpadded、half 值、边界清零、mask、越界/atlas 索引/rect 尺寸/padding/重叠/空输出；CTest **22/22**（D5.3 时 19/19）。原版参考 `DawnLightmap2DImporter.cs:46-57,74` 和 `DawnEncodeTextures.cs:259-265,399`。

D5.4 块内膨胀：`LightmapAtlasOptions::dilationIterations` 默认 4；`buildLightmapAtlasImages()` 先把每个结果复制到块内临时缓冲，再按 `DawnDilateTexture.cs:24-99` / `DilateLightmap.compute:24-94,96-164` 的 3×3 邻域平均做 4 次迭代，使用 `bIsMapped` 作为有效性。方向贴图 alpha 是 directionality，不能沿用原版 LDR 的 alpha/black 阈值，因此膨胀复制 4 个分量、只在块内传播，避免跨 atlas 块串色；`mapped` 更新为膨胀后的 coverage。原版 PRT 分支本身没有调用膨胀，D5.4 按计划补在 SH blit 前。`--sh-atlas-test` 还覆盖 0/1/4 次迭代、平均值、alpha 保留和相邻块隔离；D5.7 后 CTest **22/22**。

D5.5 DDS：新增 `lightmap_dds.h/.cpp` 的 `writeLightmapAtlasDds()`，以及 `lightmapAtlasName()` / `lightmapAtlasDdsFileName()`；每个 atlas 输出 `lm<N>_sh0.dds`、`lm<N>_sh1.dds`、`lm<N>_sh2.dds`。`bimg::imageWriteDds` 使用 RGBA16F、`numMips=1`、`numLayers=1`、无 cube、`srgb=false`，实际写 DX10 头（148B）；incident/direction half 数据按 atlas 行序原样输出，不做 Y 翻转。原版 PRT 纹理同样是 `TextureFormat.RGBAHalf`、`linear=true`、无 mip（`DawnEncodeTextures.cs:170-175`），并通过 `SetPixels` 写块（`:257-265`）；原版 `DawnBakingResult.cs:481,541-562` 的 RGBHalf 结果保存为 EXR，没有 DDS，因此 D5.5 是运行时容器，保持相同 bits/行序。`filter=linear`、`wrap=clamp` 不是 DDS 文件元数据，由 D6 上传采样器设置。`--sh-atlas-dds-test` 覆盖 2 atlas / 6 文件、文件名、RGBA16F、无 mip、尺寸、raw bytes 比对，以及空 images/错误 channel 大小拒绝；CTest 新增 `dawnbake_sh_atlas_dds`，D5.7 后 **22/22**。

D5.6 逐 texel 校验：`verifyLightmapAtlasDds()` 按 `.lmgz` 的层 0 `IncidentLighting`、层 1 `SHVectorR`、层 2 零值对比 DDS 解码值，只检查 `bIsMapped != 0` 的 texel（避免 D5.4 膨胀改变 unmapped texel）；每个 texel 比较 RGBA 四个 half 分量，target 原点按原版 `D3 data rect - padding` 传入；v1 暂不校验 `PrtHQ/PrtLQ`（真实 D4.5 样本 `prtHQ=0`）。`--sh-atlas-dds-verify-test` 覆盖合成 `.lmgz` → DDS → 解码和错误分量检测；`--verify-sh-atlas-dds <file.lmgz> <dds-dir> <atlas-index> <target-x> <target-y> <atlas-size>` 用于真实文件手工校验。原版行序/块循环参考 `DawnLightmap2DImporter.cs:46-57,74`，`ToColor` 保留 `.w` 参考 `DawnImporter.cs:249-252`，`SetPixels` 参考 `DawnEncodeTextures.cs:257-265`。CTest 新增 `dawnbake_sh_atlas_dds_verify`。

D5.7 sidecar 合并：`mergeLightmapSidecar()` 读取已有场景 `.hdrp.json` 和 D3 `<guid>.lightmap.json`，校验 fragment 的 `lightmap.version/texelPerUnit/atlases/objects` 与 rect 边界，然后用 `ordered_json` 保留原有 `hdrp/materials/lights/objects/custom` 字段，只替换/新增根 `lightmap`。CLI `--merge-sidecar <scene.hdrp.json> <lightmap.json> [<out.hdrp.json>]` 省略输出时原地更新，并自动创建输出目录；`--sidecar-merge-test` 覆盖字段保留、stale lightmap 替换、原地/幂等、坏根对象、缺 lightmap、坏 JSON、越界 rect 和缺失场景。原版结果映射参考 `DawnBakingResult.cs:94-104,777-778` 和 `DawnLightDataAsset.cs:331-379`。CTest 新增 `dawnbake_sidecar_merge`，当前 **22/22**。

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
- `--process-test` 用 `cmd.exe` 验证退出码、日志捕获、超时杀进程和错误输入；`--result-test` 用 fake Dawn 验证结果成功/缺失/超时；`--job-lifecycle-test` 验证临时目录/保留/复用。
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
hbg_dawnbake --agent-upload http://127.0.0.1:5011 <job-id> <local-file> <remote-path>
```

- 本地 Coordinator mock 位于 `tools/dawn-coordinator`，直接引用原版 `DAWNHiveCore.dll` 的 `CoordinatorServiceBase`，实现 `BeginMissionV2/CheckJob/EndMission/MissionPing` 等 RPC；无需改 DawnDeamon 配置。Dawn 插件不在默认 `..\..\..\dawnlight\com.tencent.dawn` 时，用 `dotnet build tools\dawn-coordinator -p:DawnRoot=<path>` 覆盖。

- proto 位于 `tools/dawnbake/proto`；开启选项后由 protoc/grpc_cpp_plugin 生成 C++，并编译 `dawn_agent_client.cpp`；post-build 自动拷贝 vcpkg 运行库到 exe 目录。
- 流程按真实 proto：`BeginMission → RunJob(JobFullDesc+TaskDescList) → GetJobResult(stream) → EndMission`；原清单中的 `BeginJobSpecification/AddTask/EndJobSpecification` 是本地 Hive 构造步骤，不是 ClientAgent RPC。
- `UploadFile` 已实现 64 KiB 流式上传；`BatchUploadFileInServerCache` 未实现，本地 Agent 共享 job 目录时不需要。
- 安装坑：vcpkg 自带 Ninja 1.13.2 会在 c-ares 的 try_compile 卡死；本次将 `downloads/tools/ninja-1.13.2-windows/ninja.exe` 换成 Ninja 1.11.1 后 c-ares 5.7 分钟通过，gRPC 全量编译约 58 分钟。
- 实测：本地 mock Coordinator 5010 + DawnDeamon 5009 + DawnAgent 5011，`--bake ... --agent http://127.0.0.1:5009` 输出 `DAWN_PROGRESS phase=done completed=1 total=1 percent=100.0`、`Dawn agent: NONE (0) ack=0 tasks=1/1`、`Dawn results: validated`；结果 `v2.00000000000000000000000000000005.lmgz` 经 Agent 下载回 client job 目录。
- 关键：lightmap 任务 GUID 必须为 `(0,0,LightmapTask,5)`，`TaskDesc.Parameters="LIGHTMAP2D"`；否则 Dawn 会立即 `Job Not Processed`。`GetJobResult` 收到 `D.O.N.E` 后 cancel context 再 `Finish()`，避免偶发挂起。
- 默认 `HBG_BUILD_DAWN_GRPC=OFF`；开启后 CTest 为 **22/22**（完整）。
