# Dawn GI 接入计划（C++ 重写 + 引擎集成）

> 目标：把 Tencent MagicDawn 的离线 GI（lightmap-2D + SH L1）接入 hdrp-bgfx。
>
> 参考实现：`E:\Learn\dawnlight\com.tencent.dawn`（Unity 插件 v2.4.5，协议版本 `1.0.2`）。
> 本文所有 `file:line` 均指该仓库；本仓库（hdrp-bgfx）文件用相对路径。
>
> 配套清单：[`dawn-gi-checklist.md`](dawn-gi-checklist.md)。每完成一项回填 `[x]` 并写验收数据。

---

## 0. 范围与非目标

### 做

1. **协议与导出层 C++ 重写**：把插件的 `DawnProtocol` + `SerializeUtil` + `Exporter` + `Importer` 用 C++20 重写为独立离线工具 `hbg_dawnbake`，不依赖 Unity。
2. **UV2 + Atlas**：xatlas 生成 lightmap UV，场景级 atlas 打包，元数据写入 sidecar。
3. **运行时采样**：forward / deferred 两条路径接入 baked indirect，支持 SH L1 方向光贴图，与 SSGI 正确互斥。

### 不做（v1 明确排除）

- PRT / APV / LightProbe / SDF ShadowMask / Landscape / HLOD / StitchSeam
- Group / Split lightmap（`LIGHTMAP2D_GROUP` / `LIGHTMAP2D_SPLIT`；v1 原排除，D2.16/D2.17 已补做）
- 实时预览（RealtimeViewer）、LightmapViewer
- 多 atlas 流式加载（v1 允许 1–2 张 atlas，超出直接报错）
- BC6H/RGBM 压缩（v1 用 RGBA16F）
- HDRP 式的 APV/SSGI 替代路径（我们只做 offline baked GI）

### 授权前置

Dawn 插件 README 声明版权归腾讯 MagicDawn 团队，未授权禁止使用/分发。**开工前先确认内部授权范围**，否则只做技术验证。

---

## 1. 关键结论（已从参考实现确认）

| 结论 | 依据 |
|---|---|
| 真实 GI 求解在预编译 `Dawn.exe` + `Dawn.data` 中，不在本仓库 | `Runtime/.Dawn/Win64/Binaries/` |
| 入/出接口 = 磁盘文件 + gzip，不是私有二进制协议 | `FDawnHiveInterface.OpenChannel` → `Path.Combine(m_currentJobDir, ChannelName)` |
| 序列化 = 文件头 int32 版本 + `Marshal.StructureToPtr`（blittable 结构体内存快照）+ 数组原始字节 | `Editor/DawnProtocol/SerializeUtil.cs` |
| 版本常量 = `1002`（1.0.2），存在 `1_0_0` / `1_0_1` 两套序列化分支 | `BakingCoreDefine.cs:9-15`、`BakingJobDefine.cs` 的 `Serialize_1_0_0/1_0_1` |
| 文件命名 = `v{ver}.{guid:X8}...{ext}`，扩展名即压缩格式 | `Editor/Utility/ImportExport.cs:10-37` |
| atlas 由**客户端**打包；实例头里带 `UV2MapSize` / `UV2Offset` | `DawnPackLightmapTask.cs:50-53`、`DawnMeshExporter.cs:602-603` |
| `FLightInfo` 是 176B 显式布局；`FLightmap2DSample` 是 192B / Pack=16 | `BakingSceneDefine.cs:214`、`BakingJobDefine.cs:523` |
| 光贴图数据 = `IncidentLighting` 颜色 + `SHVectorR` Unity 方向编码；`SHVectorG/B` 为未参与运行时的 SH 系数 | `DawnLightmap2DImporter.cs:53,74`；D7.2 T1 实测 |
| 插件把 `TangentBuffer` 写成 `cross(T,N)`、`BiTangentBuffer` 写成 `T`（与命名相反） | `DawnMeshExporter.cs:773-775` |
| `Dawn.exe` 默认由 Agent 拉起（argv 仅 4×32bit GUID）；**已实测支持 `-local -localdirectory` 直启** | `Editor/LightingSystem/JobManagement.cs:38`、D0.2 |

---

## 2. 总体架构

```
┌────────────────────────────── 离线（新增） ──────────────────────────────┐
│ tools/dawnbake (hbg_dawnbake.exe)                                        │
│                                                                          │
│  glTF + sidecar                                                          │
│      │                                                                   │
│      ├─ unwrap/  xatlas           →  UV2_local + naturalSize             │
│      ├─ pack/    lightmap_packer  →  rect(x,y,S,S) + ST                  │
│      ├─ convert/ scene_to_dawn    →  .scenegz/.meshgz/.matgz/...        │
│      ├─ submit/  dawn_launcher    →  Dawn.exe（或 Agent gRPC）           │
│      └─ import/  lmgz_reader      →  SH atlas (.dds) + dilation          │
│                                                                          │
│  产物：<out>/lm0_sh0.dds, lm0_sh1.dds, lm0_sh2.dds, scene.hdrp.json      │
└──────────────────────────────────────────────────────────────────────────┘
                                    │
┌──────────────────────────── 运行时（本仓库） ────────────────────────────┐
│ src/render/lightmap/   atlas 上传 / 方向解码 / ST                         │
│ shaders/lighting/baked_gi.sc   hbgSampleBakedGI()                        │
│ forward.fs.sc / gbuffer.fs.sc / deferred_lighting.fs.sc / ssgi_composite  │
│ sidecar: objects[].lightmap { atlas, st, rect }                           │
└──────────────────────────────────────────────────────────────────────────┘
```

数据流（一次烘焙）：

```
RenderScene ──▶ FSceneInfo + FBakingJobInputs ──▶ Dawn job dir ──▶ Dawn.exe
                                    │
                                    └──▶ .lmgz（每实例一块） ──▶ 膨胀 ──▶ atlas DDS
```

---

## 3. 里程碑总览

| 阶段 | 主题 | 交付物 | 验收 | 估时 |
|---|---|---|---|---|
| D0 | Spike & 基础设施 | job dir 黄金样本、`hbg_dawnbake` 骨架、`--dawn-proto-test` | Dawn.exe 能否直启有结论；黄金样本入库 | 3–5 天 |
| D1 | 协议层（已完成，gzip deflate） | `tools/dawnbake/include/hbg/dawn/*`、`tools/dawnbake/src/dawn_serialize.cpp` | 与 C# 黄金样本逐字节一致（`.scene` 1516B / `.jobs` 800B，round-trip 0 diff） | 1.5–2 周 |
| D2 | 场景转换（D2.1–D2.10 完成；`.lmgz` 出口已由 D4.5 打通） | `convert/` + 材质贴图化 + light/sky 映射 | 简单场景能生成完整 job；Dawn 能读 | 2–3 周 |
| D3 | UV2 + Atlas（D3.1–D3.9 完成；运行时 sidecar 读取待 D6） | `unwrap/` + `pack/` + sidecar 输出 | 100 块无重叠、无越界、padding 正确 | 1–2 周 |
| D4 | 作业驱动（D4.1–D4.7 完成；OnlyLocal + 本地 Coordinator mock 实跑打通） | `submit/`（直启或 gRPC） | 一条命令完成 烘→等→收 | 3 天–3 周（取决于 D0） |
| D5 | 结果导入（D5.1–D5.7 完成；运行时加载/采样待 D6） | `import/` + `.dds` + 膨胀 + sidecar | 逐 texel 校验 + 无接缝 | 3–5 天 |
| D6 | 运行时采样（D6.1–D6.7 完成） | shader/uniform/sidecar | forward/deferred 结果一致，无双重间接 | 1–2 周 |
| D7 | 校准 & 验收 | 校准场景 + 对比报告 | 见 §11 矩阵 | 1 周 |
| D8 | 收尾 | CLI 文档、错误处理、回归 | `ctest` 增加 dawn 用例 | 3–5 天 |

D0 的结论直接决定 D4 规模：若 `Dawn.exe` 必须挂 Agent，D4 追加 gRPC 客户端（+2–3 周）。

---

## 4. 协议层规格（D1）

### 4.1 序列化规则

1. **文件头**：`int32 version = 1002`（`GPUBAKING_EXPORT_VERSION`），先于对象数据。
2. **结构体**：按 C 内存布局整块写入（`Marshal.StructureToPtr`）。
   - x64 MSVC 默认对齐；`float3`=12B/align4，`float4`=16B/align4，`Matrix4x4`=64B/align4。
   - 带 `[MarshalAs(ByValArray)]` 的定长数组按内联数组写入（`FSHAHashInfo`=20B）。
   - `LayoutKind.Explicit` 的结构体按给定 `Size` 写（`FLightInfo`=176）。
3. **数组**：`int32 NumElements`，随后 `N * sizeof(T)` 原始字节；`N==0` 时只写 0。
4. **类**：按 `Serialize(FImportExportContext)` 中字段顺序逐项写，无额外头。
5. **压缩**：`*.scenegz / jobgz / meshgz / matgz / lmgz` 为 GZip。使用 zlib 的 `deflateInit2(..., 15+16, ...)`，读用 `inflateInit2(..., 15+32, ...)`。
6. **字符串**：协议中无字符串字段；不要自创。

### 4.2 版本分叉（必须实现）

- `FBakingJobInputs.Serialize`：`>=1001` 走 `Serialize_1_0_1`，否则 `Serialize_1_0_0`（`BakingJobDefine.cs`）。
- `FMeshInfo.Serialize`：`>=1001` 且 `Index16BitBuffer.NumElements == 0 && VertexBuffer.NumElements > 0` 时才写 `Index32BitBuffer`（`BakingSceneDefine.cs:578-584`）。
- 其它 `if (Context.Version >= ...)` 分支逐处照抄；**不得"优化"掉空数组**。

### 4.3 文件命名与目录

```
v2.<A:X8><B:X8><C:X8><D:X8>.scenegz
v2.<A:X8>...<D:X8>.jobgz
v2.<index>.<A:X8>...<D:X8><LOD>.meshgz
v2.<index>.<shaHex40>.matgz
v2.<A:X8>...<D:X8>.lmgz        # 结果（每个 lightmap job 一个）
```
- `Version = LM_SCENE_VERSION = 2`（`ImportExport.cs:10`）。
- job 相关文件写入 `<workDir>/Jobs/<jobId>/`；缓存资产写入 `<workDir>/`（`FDawnHiveInterface.OpenChannel`）。
- GUID 拆成 4 个 uint32，按 `A/B/C/D` 顺序、大写 X8。

### 4.4 关键结构体（v1 必需）

| 结构 | 说明 | 参考 |
|---|---|---|
| `FGuidInfo` | 4×uint32 | `BakingSceneDefine.cs:116` |
| `FSHAHashInfo` | byte[20] | `:198` |
| `FLightInfo` | 176B 显式；`Flags` 位编码类型 | `:214`，位定义见 `:8-46` |
| `FLightFullInfo` | `FLightInfo + Guid + LightPower` | `:261` |
| `FSkyLightInfo` | 头 + `EnvironmentCube` float4 数组（含 mip） | `:271-293` |
| `FMaterialInfoHeader` | Hash/ShadingModel/BlendMode/TwoSided/Flags/OpacityClip/Boost | `:375` |
| `FMaterialInfo` | Header + Diffuse/Transmission/Emissive/Normal/Specular 五张 RGBA8 贴图 | `:395` |
| `FMeshElementInfo` | StartIndex/PrimitiveCount/MaterialIndex/Flags | `:520` |
| `FMeshInfoHeader` | Guid/LOD/三角形数/顶点数/UVIndex/密度 | `:530` |
| `FMeshInfo` | Header + 索引16/32 + V/N/T/B + texcoord 列表 | `:556` |
| `FMeshInstanceInfoHeader` | 双矩阵、`LightmapId`、`UV2MapSize`、`UV2Offset` 等 | `:590` |
| `FSceneInfo` / `FSceneInfoHeader` | 场景聚合 + mesh instance 列表 | `:815-900` |
| `FBakingJobInputs` | 参数 + `Lightmap2DJobs` 等数组 | `BakingJobDefine.cs` |
| `FLightmap2DInput` | `JobID/MeshID/Size/LightmapUVIndex` | `BakingJobDefine.cs` |
| `FLightmap2DOutput` / `FLightmap2DSample` | 结果；Sample=192B/Pack16 | `BakingJobDefine.cs:518-560` |

> 结构体成员级字段直接照 C# 抄；D1 不追求"理解语义"，只追求布局一致。

### 4.5 C++ 编码约定

```cpp
#pragma pack(push, 8)
struct FGuidInfo { uint32_t a, b, c, d; };
static_assert(sizeof(FGuidInfo) == 16);
...
#pragma pack(pop)
```
- 每个结构体加 `static_assert(sizeof(...) == N)`；N 由 D0 的 `--dawn-proto-test` 打印 C# `Marshal.SizeOf` 后回填。
- `float3` 用自定义 `struct float3 { float x, y, z; }`（不要用 glm::vec3 的 16B）。
- 序列化器接口：
```cpp
class Writer { void write(const void*, size_t); void writeArrayHeader(int32_t n); };
class Reader { ... };
template <typename T> void serialize(Writer&, const T&);   // 特化到每个结构体
```
- gzip 用 zlib（`FetchContent` 或 vendored `bx` 自带？优先 zlib，独立可测）。

### 4.6 黄金样本（D0 产出）

在临时 Unity 工程用插件烘一个最小场景 → 拷贝 `<workDir>/Jobs/<jobId>/` 与缓存资产 → 存入本仓库：

```
tests/dawn/golden/<case>/
  v2.*.scenegz
  v2.*.jobgz
  v2.*.meshgz
  v2.*.matgz
  result/*.lmgz
  expected.json          # guid、尺寸、关键字段摘要
```

`--dawn-proto-test` 对每个文件：读取 → 反序列化 → 重新序列化 → 逐字节比对；并打印 `sizeof`/`offsetof` 表。

---

## 5. 场景转换规格（D2）

### 5.1 几何

- 去重：同一 `asset::Mesh` 只导一份 `FMeshInfo`；实例写 `FMeshInstanceInfo`。
- 顶点：`VertexBuffer` = position + w=1；`NormalBuffer` = normal + w=0。
- **切线（照抄插件，不要"修正"）**：`TangentBuffer = cross(T, N)`，`BiTangentBuffer = T`（`DawnMeshExporter.cs:773-775`）。
- 索引：顶点数 ≤ 65535 用 `Index16BitBuffer`，否则 `Index32BitBuffer`（版本分支见 §4.2）。
- SubMesh → `FMeshElementInfo`（StartIndex 以索引为单位，不是字节）。
- `LightmapUVIndex = 1`；`TexcoordBuffers` 写 `[uv0, uv2, uv2]`（第三份为 original UV2，占位即可）。

### 5.2 材质

插件是从 Unity MetaPass **采样**出五张贴图；我们已有 PBR 参数，直接"贴图化"：

| Dawn | 我们的来源 | 注意 |
|---|---|---|
| Diffuse | baseColor × baseColorFactor；sRGB→linear | `DawnMaterialExporter.cs:123` 的 `bConvertLinear` 逻辑相反：Unity Gamma 空间才转，我们统一转 |
| Transmission | `transmission > 0` 时用 alpha/遮罩 | `MATERIAL_FLAGS_SURFACE_DOMAIN` |
| Emissive | emissiveFactor × 贴图（HDR boost 乘进 `EmissiveBoost`） | 超过 1.0 的部分放 `EmissiveBoost` |
| Normal | normal 贴图；**Y 翻转要校准** | 插件有 `bUseDX5NM` 分支 |
| Specular | `metallicRoughness` 打包成 RGBA（插件用 Spec/Rough/Metallic/Ior） | 与 `DawnMaterialExporter` 的 PBR 打包顺序对齐 |

- `BlendMode`：Opaque→0、Mask→1、Blend→2（对齐 `EBlendMode`）。
- `bTwoSided`、`OpacityMaskClipValue` 直接映射。
- 贴图统一 RGBA8；`FTexture2DInfoHeader{SizeX, SizeY, WrapMode, Format}`。

### 5.3 灯光

`FLightInfo.Dimensions` 的语义随 `Flags` 类型位变化（`BakingSceneDefine.cs:228`）：

| 类型 | Dimensions | 其它 |
|---|---|---|
| Directional | —（用 `Normal`/`dPdu`/`dPdv` 定方向） | 太阳角度 → `Angles` |
| Point | (Radius, SoftRadius, Length=0) | `Attenuation`, `FalloffExponent` |
| Spot | (sin(θ/2), sin(θsoft/2), Length) | `Shaping` = 内外锥角 |
| Rect | (Width, Height, 0) | `Normal/dPdu/dPdv` 定面朝向 |
| Disc | (Radius, Radius, 0) | v1 可不支持，遇到报错 |

- 单位：glTF punctual 用 candela/lux；Dawn 的 `Color` 是线性辐射亮度。**校准测试见 §11**，先 1:1 映射再乘全局系数。
- `FLightFullInfo.LightPower` 与 `Attenuation` 的关系需实测（用 1m/2m/4m 距离测衰减）。
- `FLightProfileInfo`（IES）：v1 跳过，`IESTextureSlice = -1`。

### 5.4 天空

- 把 engine 的 equirect HDR 采样成 6 面 cube，逐 mip 写入 `FSkyLightInfo.EnvCube`（float4，顺序与插件一致，需用 `CubemapConvert.shader`/`EnvironmentProcessor` 对照）。
- `MipDimensions` 逐级减小；`IndirectLightingIntensity` = 1，`Flags` 先置 0。
- 若暂不做天空：把 `FSkyLightInfo` 全置 0，但**必须存在**（job 结构要求）。

### 5.5 静态标记

- sidecar `objects[].contributeGI`（默认 false）决定是否生成 UV2/实例/Job。
- 动态物体不进 `FSceneInfo`，运行时继续用 ambient SH/SSGI。

---

## 6. UV2 与 Atlas（D3）

### 6.1 xatlas

```cpp
xatlas::ChartOptions co;
co.normalDeviationWeight = 2.0f; co.normalSeamWeight = 4.0f;
xatlas::PackOptions po;
po.resolution = 0;     // 0 = 自动 atlas；输出 UV 是 texel 单位
po.padding = 2; po.bilinear = true; po.bruteForce = false;
```
- 每个唯一 mesh 跑一次；实例共享 UV2。
- `texelsPerUnit` = sidecar `lightmap.texelPerUnit`（默认 4~8 texels/m）。
- `naturalSize = ceil(max(rawUV2 spanX, spanY))`（xatlas texel 单位，不含 atlas 外围 padding）。
- 退化网格（零面积/无索引/空顶点/索引越界）由 `tryUnwrapLightmapUv()` 跳过并 `HBG_LOGW` 告警；`unwrapLightmapUv()` 仍对直接调用抛错。参考原版 `DawnMeshExporter.cs:531-536` 的跳过告警与 `DawnLightmapSizeCalculate.cs:27-30` 的零 UV 面积处理。

> 2026-11-19 实测（xatlas f700c779）：`resolution=0` + `texelsPerUnit=6` 对 2m×1m quad
> 产生一个自动尺寸 atlas（`atlasCount=1`，13×7），raw UV max 12.5×6.5（texel 单位）。
> 当前 API 没有旧版 `normalDeviation/normalSeam` 字段，使用 `normalDeviationWeight/normalSeamWeight`。
> UV2 必须按 raw span 做**统一**归一化（`(uv-min)/maxSpan`），保留 chart aspect/texel density；
> 不要分别按 U/V bounds 归一化。

`unwrapLightmapUv()` 返回 remap 后的 `asset::Mesh`；喂给 `exportMesh()` 时设
`MeshExportOptions::normalizeUv2=false`，避免 D2 的 per-axis 归一化破坏已生成的 UV2。

### 6.2 每实例尺寸

照抄 `DawnLightmapSizeCalculate.cs:11-17`：

```
Resolution = texelPerUnit * scaleInLightmap * LODScale   // LODScale = 0.5^lod
TexelSize  = Resolution * max(uvW, uvH) * sqrt(surfaceArea / (uvW*uvH))
S_i        = clamp(floor(TexelSize), 4, maxLightmapSize)   // 默认 1024，atlas 2048
```
`padding` 开启且 `S_i > 2` 时，上界再减 2（照原代码）；`uvW/uvH` 取 xatlas **raw texel UV bounds**（`uvMinTexels/uvMaxTexels` 的跨度），不要取 per-axis 归一化后的 bounds；这样 `naturalSize * texelsPerUnit` 与 `S_i` 一致。

### 6.3 打包

- 全部块为正方形：按 `S_i` 降序 AtlasNode pack（原版 `RectPackingUtility.cs:284-378`），`atlasSize=2048`（可配 1024/4096）；同尺寸按输入索引稳定排序。
- `padding=2` 为每块四周留白，实际块间净距 4 texel；rect 不含 padding。
- 溢出：先尝试 1 张 atlas；失败时在 `[minScale,1]` 对全部块做统一缩放二分（`ceil(scale*S_i)`，单块最小 4，下限 `minScale=0.5`；原版 `DawnPackLightmapAtlas.cs:59-85` 为 0.1）；仍失败再尝试 2 张 atlas，并对 2 张重复满分辨率/二分；2 张仍放不下则报错退出。
- 缩放后按 atlas 重新 AtlasNode pack；结果：`rect(x, y, S, S)`、`UV2MapSize=(S,S)`、`UV2Offset=(x,y)`，sidecar 记录 `scale` 用于 `st`。

### 6.4 sidecar 输出

```json
"lightmap": {
  "version": 1,
  "texelPerUnit": 6.0,
  "atlases": [{ "file": "lm0", "size": 2048, "layers": 3, "format": "rgba16f" }],
  "objects": {
    "SM_Floor_012": {
      "atlas": 0, "rect": [1024, 0, 256, 256],
      "st": [0.125, 0.125, 0.5, 0.0],
      "contributeGI": true
    }
  }
}
```
`st = (S/A, S/A, x/A, y/A)`，运行时 `lmUV = uv2 * st.xy + st.zw`。`hbg_dawnbake` 输出 `<guid>.lightmap.json` fragment（根 `lightmap`）；D5.7 的 `mergeLightmapSidecar()` / `--merge-sidecar <scene.hdrp.json> <lightmap.json> [<out>]` 会保留已有字段并替换/新增根 `lightmap`。最小场景为 `lm0` 2048、`Quad` rect `[2,2,64,64]`。

### 6.5 调试工具

`hbg_dawnbake --dump-uv=<out.png>`：用内置 1m cube 跑 xatlas + 2048 atlas，输出 rect 填色/边框和 UV2 三角形黄色线框（参考 `DawnLightmapPackingPreviewWindow.cs`，默认 Y 翻转）；用于人工检查重叠/越界/padding。100 块无重叠 stress 在 `--atlas-test` 内执行。

---

## 7. 作业驱动（D4）

### 7.1 直启（D0 已验证可行）

D0 实测命令行：

```
Dawn.exe -local -localdirectory <dir>\ <guid32hex> [-lm 0 -1 1]
```

- `<dir>\` **必须以反斜杠结尾**；Dawn 直接拼接 `<dir>\<guid>.scene` / `<guid>.jobs`。
- 输入是**未压缩**的 `int32 version(1002) + FSceneInfo/FBakingJobInputs`，不是 `.scenegz/.jobgz`。
- 工作目录建议为 exe 目录；`Dawn.data` 与 OIDN device 插件必需。
- `-lm start count round` 控制 lightmap job 范围（`0 -1 1` = 全部烘一遍）。
- 轮询 `.lmgz`/`.output`（待确认输出命名）+ 进程退出；超时杀进程；`--keep-job` 保留目录。
- D4.1 已实现 `dawn_launch.h/.cpp`：`buildDawnArguments()` 生成 `-local -localdirectory <job>\ <guid32hex>`；`runProcess()` 用 `CreateProcessW` 直启、工作目录设为 exe 目录、stdout/stderr 合并到 `<job>/Dawn.log`、JobObject 杀进程树、`--timeout` 超时。`--run-dawn`/`--process-test` 已接入，CTest **17/17**。
- D4.2 已实现 `runDawnJob()`：按原版 `CreateChannelName` 规则轮询 `v2.<TaskGuid>.lmgz`（job 目录），进程退出后再等 `--result-grace`；失败码映射原版 `EBakingError`（超时 -5、非零退出 -6、结果缺失 -20）。`--bake`/`--result-test` 已接入。
- D4.3 已实现 `JobWorkspace`：`--job-dir` 可替代位置参数，缺失时创建；默认只清理本次创建的临时目录，已存在目录永不自动删；`--keep-job` 保留自建目录。原版 `OpenJob` 建 `TLBSCache/Jobs/<GUID>`、`CloseJob` 不删、`Clear Cached Data` 显式清缓存；v1 采用安全子集。`--job-lifecycle-test` 已接入，CTest **17/17**。
- D4.7 已实现 CI 进度行 `DAWN_PROGRESS phase=... completed=N total=M percent=P`；`runDawnJob()` 的 `onProgress` 在结果文件数变化时触发，值为新 `.lmgz` 数 / `lightmap2DJobs` 数。原版 BUILDING 用 `m_RecieveTaskCount/NumOfTaskExported`、IMPORTING 用 `NumOfTaskImported/NumOfTask`（`Dawn4UnityWindow.cs:216-234`）。
- 2026-11-19 直启实测（D0 CUDA workaround exe）：D2.8 raw scene/jobs 启动 6.79s、退出码 0、日志含 `Lighting complete`；仍未生成 `.lmgz`，`--bake` 报 `RESULT_IMPORT_FAILURE(-20)`。结果通道仍需 Agent 或其他 local 输出参数，D5 导入要等该阻塞解除。

> **D0.8 已在本机解除（2026-11-22）**：官方未打补丁 `Dawn.exe` 直启 5/5 exit=0、`Lighting complete`，
> Agent 路径 1/1 产出 `.lmgz`。日志仍出现 OIDN CPU 的 `string pointer is null`，但随后回退
> `launch optix denoiser` 并正常结束，不再触发致命崩溃。2026-11-19 的 `oidnNewDevice(CPU=1)`→`CUDA=3`
> patch 仅保留作历史记录。CPU OIDN（`denoiserMode=2`）已实测产出 `.lmgz`；仍需 D0.9 干净机复测，D0.10 仅剩日志清理。
> local 模式仍未落盘 `.lmgz`，结果通道归 D4/D5。

### 7.2 Agent gRPC（备选）

启动 `Runtime/.Dawn/Win64/DotNet/DawnDeamon/DawnAgent/DAWNAgent.exe`（`appsettings.json`：Agent 5011 / Coordinator 5010），用 `Editor/Protos/ClientAgent.proto` + `Agent.proto` 生成 C++ 客户端：

```
BeginMission(V2) → RunJob(JobFullDesc+TaskDescList) → GetJobResult(stream) → EndMission
```
- `BeginJobSpecification/AddTask/EndJobSpecification` 是本地 Hive 构造步骤，不是 ClientAgent RPC。
- 文件通过 `UploadFile` / `BatchUploadFileInServerCache` / `UploadJobFilesToAnotherAgent` 上传。
- `MissionInfo` 使用 `CalculationScheme.OnlyLocal`，`userToken` 留空；原版 OnlyLocal 仍需 Coordinator，已用 `tools/dawn-coordinator` 本地 mock 打通。

> D4.4–D4.7 进展（2026-11-20）：vcpkg 已装 grpc 1.81.1 / protobuf 6.33 / abseil / c-ares / re2。关键坑：vcpkg 自带 Ninja 1.13.2 在 c-ares try_compile 卡死，换 Ninja 1.11.1 后通过。`HBG_BUILD_DAWN_GRPC=ON` 已编译 `dawn_agent_client` 并生成 C++ proto；CTest 17/17。
> `DawnDeamon` 5009 拉起 `DAWNAgent` 5011；本地 mock Coordinator 5010 返回 `BeginMissionV2/CheckJob`，Dawn 产出真实 `v2.00000000000000000000000000000005.lmgz`（约 43KB，64×64，4096 samples），client `GetJobResult` 收到 `D.O.N.E`，`DAWN_PROGRESS ... done`。

2026-11-21 进一步验证 gz-only：删除 job 目录 raw `.scene/.jobs`，仅保留 deflated `.scenegz`/`.jobgz`，Agent/Dawn 仍完成并 `Dawn results: validated`。同时修复 `spec.jobId` 大小写：原版 `JobManagement.cs:38` 使用 `%08X` 大写，之前小写化会使含 A-F 的 GUID 被 DawnAgent 拒绝；`taskID` 继续使用 `guidHex` 大写。`buildAgentJobSpec()` 与 `expectedDawnResults()` 现在都会回退读取 `.jobgz`。D4.6 的 `BatchUploadFileInServerCache` 已按原版 `Agent.proto:49-50,127-138` 实现：请求只含 `hash/filePath`、不含 `fileData`，ack 数量必须匹配，`Done/DoneInCache` 为成功；`--agent-cache-test` 用进程内 fake Agent 覆盖成功、cache miss、per-file fail。`UploadFile` 已按原版修复：首包必须带 `jobID/filePath/fileSize/hash`（SHA1 大写），先等 `NotInCache` 再发数据，空文件不发数据包，3 MiB 分块；新增 `dawnbake_agent_upload`，真实 Agent 上传 `hbg_upload_probe.txt` 成功，并在官方 active T6 job 上完成 6 MiB 上传/`DownloadFile` 回读 SHA-256 一致。`UploadMultiFiles`（`Agent.proto:143-152`）、`DownloadFile`（`:58,155-173`）和 `UploadJobFilesToAnotherAgent`（`:61,176-180`）也已实现并有 fake Agent 测试；`DownloadFile` 必须写 `DataDone.fileData`（原版短文件只在最后一条 `DataDone` 带 payload），真实 active Agent 下载 184,476B 与源文件 SHA-256 一致。`--bake --agent --agent-files --agent-download-results` 在 `EndMission` 前下载缺失结果，优先 `DownloadMultiFiles`、失败回退 `DownloadFile`，本地已有结果跳过，`--agent-force-download` 可强制重下；真实 Agent 强制重下 190,301B，Agent 日志 `Got DownloadMultiFiles 1 files`，SHA1 校验通过；原版 Agent 的 `DownloadFile` 只服务 current job，作业结束后调用会报 `doesn't match current job`。 另按 DAWNAgent 2.4.5 实际 DLL 补齐 `DownloadMultiFiles`（Editor 旧 proto 缺失）：`DownloadFileAck.filename=5`、`DownloadMultiFilesReq/Ack`，支持同一 ack 内多文件 Data/DataDone、逐文件 SHA1 大写 hex 校验和 `.part` 原子提交；新增 `dawnbake_agent_multi_download` 与 `dawnbake_agent_download_files`（批量成功 + 不支持回退）。
> 关键修正：任务 GUID 必须是 `(0,0,LightmapTask,5)`，`TaskDesc.Parameters="LIGHTMAP2D"`；否则 Dawn 立即 `Job Not Processed`，不会写结果。

### 7.3 CLI

```
hbg_dawnbake --scene=<gltf> --sidecar=<hdrp.json> --out=<dir>
             --dawn-exe=<path> | --dawn-agent=<path>
             --atlas=2048 --texel-per-unit=6 --quality=medium
             --job-dir=<dir> --keep-job --dump-uv=<png>
```

---

## 8. 结果导入（D5）

1. 读 `<jobDir>/*.lmgz`（gzip → `FLightmap2DOutput`）。
   - **D5.1 已完成**：`readLightmap2DOutputFile()` + zlib gzip；原版无版本头，布局为 header(48) / JobInputIndex / 3 flags / Lights / LightmapData；CLI `--lmgz-test`、`--inspect-lmgz`；CTest 当时 17/17；真实 `.lmgz` 已由 D4.5 产出（约 43KB，64×64，4096 samples）。Native 输出的 `JobInputIndex` 为未初始化值，D5.2 不使用。
2. `HeaderInfo.Size` 必须与 `FLightmap2DInput.Size` 匹配（无 padding 时相等；有 `LIGHTMAP_FLAGS_PADDING` 时各边 +2，对齐原版 importer 的 `UVWidthPixel + 2*Padding`）；`JobID/MeshID` 用来定位实例。
   - **D5.2 已完成**：原版 `DawnLightmap2DImporter.cs:30-44` 以 `Allocation.UVWidthPixel/UVHeightPixel + 2*Padding` 与 `HeaderInfo.Size` 做 `Debug.Assert`，导出端 `DawnLightmap2DExporter.cs:184-187` 写入 `JobID/MeshID/Size`。C++ 新增 `lightmap_result.h/.cpp`：校验 JobID/MeshID、正尺寸、Size（无 padding 时等于 input；有 padding flag 时各边 +2）、样本数；用 JobID 唯一匹配 `BakingJobInputs.lightmap2DJobs`，用 MeshID 唯一匹配 `SceneInfo.meshInstances[].header.guid`，重复/缺失均失败；`--verify-lmgz` 和 `--bake` 均接入。真实 D4.5 样本输出 `job=0 instance=0 meshIndex=0 size=64x64 samples=4096 mapped=4096`；CTest 18/18（D5.3 后 19/19）。
3. 每 texel 取 `IncidentLighting` 与 `SHVectorR` → 写层 0/层 1 的 RGBA16F atlas 对应 rect；层 2 为零。
   - **D5.3 已完成，D7.2 改语义**：`lightmap_atlas.h/.cpp` 的 `buildLightmapAtlasImages()` 复用 D5.2 binding，按 D3 data rect 写入；原版 importer 使用 `UVWidthPixel + 2*Padding` 的整块，因此目标原点为 `rect - padding`，padding 边界也一起拷入；`bIsMapped` 另存 mask。D7.2 后层 0=`IncidentLighting`、层 1=`SHVectorR`、层 2=零。原版参考 `DawnLightmap2DImporter.cs:53,74`、`DawnEncodeTextures.cs:399,408`。
   - D7.2 已确认：`IncidentLighting` 是颜色主通道，必须存；`SHVectorR` 是方向，不是 SH 的 L0。
4. **先膨胀后 blit**：D5.4 已完成。`LightmapAtlasOptions` 默认 4 次；build 先写块内临时 3×RGBA16F 缓冲，按 `DawnDilateTexture.cs:24-99` / `DilateLightmap.compute` 的 3×3 邻域平均迭代。方向贴图 alpha 是 directionality，不是 coverage；因此以 `bIsMapped` 为有效性，保留 4 个分量，并只在块内传播，避免跨块串色。
5. 输出 DDS（`bimg` 可解析）：D5.5 已完成。
   - 新增 `lightmap_dds.h/.cpp` 的 `writeLightmapAtlasDds()`，以及 `lightmapAtlasName()` / `lightmapAtlasDdsFileName()`；输出 `lm<N>_sh0.dds` / `lm<N>_sh1.dds` / `lm<N>_sh2.dds`（D7.2 后依次为 incident/direction/zero）。
   - `bimg::imageWriteDds` 写 RGBA16F、无 mip（DX10 头 148B）；incident/direction half 数据按 atlas 行序原样输出，不做 Y 翻转。原版 PRT 纹理同样是 `TextureFormat.RGBAHalf` 且 `linear=true`（`DawnEncodeTextures.cs:170-175,257-265`），但原版结果保存为 EXR/PNG，没有 DDS；D5.5 是运行时容器。
   - `filter=linear`、`wrap=clamp` 不是 DDS 文件元数据，由 D6 上传采样器设置。`--sh-atlas-dds-test` 覆盖 2 atlas / 6 文件、RGBA16F、无 mip、尺寸和 raw bytes 比对；D5.7 后 CTest **22/22**。
6. **逐 texel 校验**：D5.6 已完成。`lightmap_dds.h/.cpp` 新增 `verifyLightmapAtlasDds()`，D7.2 后按层 0 `IncidentLighting`、层 1 `SHVectorR`、层 2 零值对每个 `bIsMapped != 0` texel 的 4 个分量做 half-bit 精确比对；DDS target 原点由调用方给出（原版 `rect - padding` 规则）；v1 暂不校验 `PrtHQ/PrtLQ`（真实 D4.5 样本 `prtHQ=0`）。
   - 只校验 mapped 源 texel，避免 D5.4 膨胀改变 unmapped texel 导致误报；原版行序/块循环参考 `DawnLightmap2DImporter.cs:46-57,74`，`ToColor` 保留 `.w` 参考 `DawnImporter.cs:249-252`，`SetPixels` 参考 `DawnEncodeTextures.cs:257-265`。
   - `--sh-atlas-dds-verify-test` 写合成 `.lmgz` → DDS → 解码比对；`--verify-sh-atlas-dds <lmgz> <dir> <atlas> <x> <y> <size>` 支持真实文件手工校验（target 传 DDS 原点）。CTest 新增 `dawnbake_sh_atlas_dds_verify`，当前 **23/23**。
7. **更新 sidecar**：D5.7 已完成。`dawn_package` 新增 `mergeLightmapSidecar()`，读取已有 `.hdrp.json` 与 D3 lightmap fragment，校验 `version/texelPerUnit/atlases/objects` 与 rect 边界后，用 `ordered_json` 保留原有 `hdrp/materials/lights/objects/custom` 字段并替换/新增根 `lightmap`。CLI `--merge-sidecar <scene.hdrp.json> <lightmap.json> [<out.hdrp.json>]` 默认原地更新；`--sidecar-merge-test` 覆盖字段保留、stale 段替换、原地/幂等和错误输入；CTest **22/22**。原版结果字段参考 `DawnBakingResult.cs:94-104,777-778` 和 `DawnLightDataAsset.cs:331-379`。

> D7.2 已确认保留 `IncidentLighting`，直接复用现有 3 层布局，不新增 DDS 文件。

---

## 9. 运行时采样（D6）

### 9.1 资产与数据

- `asset::RenderObject` 增加：
  ```cpp
  uint32_t lightmapAtlas = UINT32_MAX;
  Vec4     lightmapST{0,0,0,0};
  Vec2     lightmapSize{0,0};      // rect 像素，供 debug
  ```
- `Sidecar` 解析 `lightmap.objects[<name>]`，按 `RenderScene::objects` 的 name 匹配。**D6.1 已完成**：`SceneLoader` 校验 `atlases/objects/rect/st/contributeGI`，`st` 缺失时按 `(S/A,S/A,x/A,y/A)` 推导；参考原版 `DawnLightmap2DTask.cs:269-274`、`DawnLightDataAsset.cs:331-379`、`DawnMeshComponent.cs:198-225`。
- 新增 `src/render/lightmap/lightmap.h/.cpp`：
  - `LightmapAtlas { uint32_t layerBase; uint32_t size; }`；`LightmapSet` 持有一个 RGBA16F 2D texture array，layer = `atlas*3 + channel`。原计划 `TextureHandle sh[3]` 会占三个 sampler；当前 forward 的 D3D11 unit 余量不足，因此合并成一个 array，D6.3 仍只需一个 sampler。
  - `loadAtlas(...)`：**不要走 `TextureLoader`**，它会把浮点图转成 RGBA32F（`src/asset/texture_loader.cpp:64-66`）；直接用 `bimg::imageParse` 保留 RGBA16F，校验正方形、无 mip/无 layer array/非 sRGB，再上传 linear/clamp 的 texture array。**D6.1 已完成**：`LightmapSet::load()` + `--asset-test` 上传路径；D3D11 实测 `uploaded lightmap SH array: atlases=1 layers=3 size=64`；单测覆盖 sidecar 绑定、越界 rect 和 DDS 读取，`hbg_tests` 241/32,432。

### 9.2 顶点与 uniform

| 文件 | 改动 |
|---|---|
| `shaders/include/varying.def.sc` | 加 `vec2 a_texcoord1 : TEXCOORD1;` 和 `vec2 v_texcoord1 : TEXCOORD4;` |
| `shaders/include/varying_instanced.def.sc` | 同上；**`i_data3` 已被 model matrix 第 4 列占用**，静态实例的 ST 新增 `i_data4 : TEXCOORD27`，instance stride 16→20 floats |
| `config/uniforms/prepass.idl` (`block Object`) | 加 `vec4 lightmapST;`（x,y=scale；z,w=offset）、`vec4 lightmapParams;`（x=enabled，y=atlas 基数） |
| `src/gfx/generated/uniforms.h` | `py -3 tools/idl_gen.py` 重新生成，勿手改 |
| `config/uniforms/pass.idl` | 加 `vec4 lightmapParams2;`（x=GI 缩放，y=层数，z=debug，w=forward baked-GI active） |

顶点布局已上传 `TexCoord1`（`src/render/material/material_system.cpp:33-38`），无需改 buffer。

**D6.2 实现记录**：

- `v_texcoord1` 在 VS 内完成 ST 变换：非实例路径使用 `u_object_lightmapST`，实例路径使用 `i_data4`；FS 直接把它当作 `lmUV`，不再重复乘 ST。
- `Object` uniform 的默认值为 `lightmapST=(1,1,0,0)`、`lightmapParams=(0,0,0,0)`；`lightmapParams.y = atlasIndex * 3`，无 lightmap 对象使用 identity/disabled。
- `DrawItem` / `DrawBatch` / `DrawSortKey` 增加 `lightmapAtlas`；批次键为 `stateKey + lightmapAtlas`，避免一个实例批次内混用不同 atlas。
- `kForwardInstanceStride=20` floats（16 matrix + 4 ST）；shadow 实例保持 16 floats。
- `Pass` 的 `lightmapParams2=(1, 3, 0, 0)` 为默认值；forward 帧在 baked-GI draw 实际启用时把 `.w` 置 1，主 forward 用它跳过 ambient diffuse/SH。

**槽位审计**：forward 的 16 个 PS stage **全部已占用**：0–4 材质、5 lightData、6 directional shadow、7 lightList、8 shadowContext buffer、9 punctual shadow、10 environment、11 contact shadow、12/13 point/area shadow、14 probe、15 cookie。**不能直接在 `forward.fs` 新增 `s_lightmapSH`**；Vulkan descriptor set 的 binding 也不能用 VS/PS 分开规避。

D6.3 因此改为**同一 forward view 内的独立 baked-GI program/draw**：该 program 只用 stage 15/1/2（SH/baseColor/normal），复用 depth prepass 和 forward framebuffer，不新增 forward 共享 sampler；SH sampler 在每个 item 的 state/material bind 之后重绑；主 forward shader 根据 `u_pass_lightmapParams2.w` 与 `u_object_lightmapParams.x` 跳过 ambient SH，不采样 lightmap。

### 9.3 共享采样函数

新增 `shaders/lighting/baked_gi.sc`：

```glsl
SAMPLER2DARRAY(s_lightmapSH, 15);  // 仅在独立 baked-GI pass 中绑定

vec3 hbgDecodeBakedGI(vec4 incident, vec4 direction, vec3 nWS) {
    float halfLambert = dot(nWS, direction.xyz - 0.5) + 0.5;
    return max(incident.rgb * halfLambert / max(direction.w, 1.0e-4), vec3(0.0));
}

vec3 hbgSampleBakedGI(vec2 lmUV, vec3 nWS) {
    float base = u_object_lightmapParams.y;
    vec4 incident  = texture2DArray(s_lightmapSH, vec3(lmUV, base + 0.0));
    vec4 direction = texture2DArray(s_lightmapSH, vec3(lmUV, base + 1.0));
    return hbgDecodeBakedGI(incident, direction, nWS) * u_pass_lightmapParams2.x;
}
```

- 原版 `DawnLightmap2DImporter.cs:53` 把 `IncidentLighting` 写入颜色贴图，`:74` 把 `SHVectorR` 写入方向贴图；`DawnEncodeTextures.cs:399,408` 分别 `SetPixels`。Unity `SampleDirectionalLightmap` 使用 `direction.xyz - 0.5` 与 `direction.w`（`com.unity.render-pipelines.core/ShaderLibrary/EntityLighting.hlsl`）。
- D7.2 实测确认 `SHVectorR` 是 Unity 方向编码（+Y 光为 `(0.5, 1.0, 0.5, 1.0)`），不是 SH 系数；`SHVectorG/B` 的 4 个系数当前不参与运行时。层 0 存 `IncidentLighting`，层 1 存 `SHVectorR`，层 2 保留零。
- `u_pass_lightmapParams2.x` 是全局校准系数（D7 定值，默认 1.0）。T1 +Y 单平行光解码为 incident=1 → 运行时白色，`x=1.0`。

### 9.4 Forward 路径（D6.3 完成）

- 不在 `forward.fs` 内采样 SH（16 个 stage 已满）。新增独立 `baked_gi.fs` program，只绑定 `s_lightmapSH`(15)、baseColor(1)、normal(2)，逐像素输出 `bakedGI * albedo`；在 forward 路径先渲染到 transient RGBA16F `bakedIndirect`（复用 depth prepass/depth-equal），再在 forward opaque 之后由 `baked_gi_forward_composite.fs` 全屏 additive 到 color。这样同一像素的重复 submesh 只会计一次 indirect，避免前置 per-item additive draw 的重复叠加。参考 HDRP `Lit.hlsl`：lightmap 在材质阶段进入 `builtinData.bakeDiffuseLighting`，forward opaque 对同一像素只保留最终着色结果，不存在逐 submesh 的 additive 重复。
- `lmUV` 由 VS 传入；D6.2 已把局部 UV1 变换为 `v_texcoord1 = a_texcoord1 * ST.xy + ST.zw`（非实例用 Object uniform，实例用 `i_data4`）。
- 主 forward 在 `u_pass_lightmapParams2.w > 0.5 && u_object_lightmapParams.x > 0.5` 时跳过 ambient diffuse/SH，但保留环境 specular；`w` 仅在 baked-Indirect/composite 实际启用时为 1。
- `DrawList` 新增 `requireLightmap` 过滤；`m_bakedGIDrawList` 只包含 opaque+contributeGI 对象，按 `lightmapAtlas` 分批。
- **互斥**：有 lightmap 的物体不再叠加 ambient SH；SSGI 合成阶段的 mask 互斥见 §9.5。
- **验证**：D3D11 `triangle_lm_ds` forward/deferred 正常渲染逐字节一致（T4，PSNR=∞，采样 `(140,62,38)`）；`triangle.gltf` 的 no-lightmap forward/deferred 也逐字节一致。D7.2 已改为 Dawn 方向贴图解码（`IncidentLighting` + `SHVectorR`），不再使用 `.rgb/.a` SH swizzle。
- **稳定性修复**：`RenderGraph` 每个 pass 显式 `BGFX_CLEAR_NONE`，shadow atlas clear pass 每帧保留但仅首帧真正 clear；否则 frame 0 的额外 clear pass 会改变后续 frame 的 view ID，导致 depth clear 被错误继承（表现为第二帧后模型消失）。

### 9.5 Deferred 路径（D6.4 完成）

1. 复用 `baked_gi.fs`，但用 **gbuffer draw list** 单独渲染一个 transient RGBA16F `bakedIndirect` RT（rgb=bakedGI*albedo，a=lightmapMask）；使用 depth prepass 的 depth-equal，不向 `gbuffer.fs` 新增 sampler。
   - 该 draw 使用独立 raster pass，颜色附件为 `bakedIndirect`、深度附件复用 prepass depth；pass 内显式 clear 颜色，非 lightmap 像素保持 mask=0。
   - SH sampler 放在 stage 15，并在每个 item 的 `setState`/material bind 之后重新绑定；否则 BGFX 的 view 状态顺序会丢掉该纹理（实测表现为 mask=1 但 rgb=0）。
2. `src/render/passes/forward_renderer.cpp`：通过 `RenderGraph::createTexture` 创建 transient RT，跟随 graph 生命周期，无需手动释放；deferred 路径读取 `resources.bakedIndirect`。
3. `shaders/passes/baked_gi_compensate.fs.sc`：deferred lighting 已写 ambient/SH，因此独立全屏补偿 pass 解码 gbuffer，计算与 `hbgEvaluateAmbient` 同口径的 ambient+SH，输出 `baked.rgb - ambient` 并 additive 到 color；保留环境 specular，不双重间接。
   - 不改 `deferred_lighting.fs.sc`：deferred raster/variant 的 stage 0–15 已全部占用（另有外部 `shadowSat` t16），无法再加 `s_bakedIndirect`；全屏补偿 pass 只需自己的 stage，规避冲突。
4. `shaders/passes/ssgi_composite.fs.sc`：`indirect *= (1.0 - bakedMask)`，mask 从 `bakedIndirect.a` 读取。
5. Forward-only 模式：forward frame 复用 §9.4 的 `bakedIndirect` + 全屏 composite；deferred frame 中 `lightmapParams2.w` 保持 0，避免 forward-only 对象误跳 ambient。当前 T4/T5 验收对象均为 forward/deferred opaque。
6. **验证（D3D11）**：`triangle_lm_ds` deferred 与 forward composite 均输出 `(140,62,38)`，整图逐字节一致（T4，PSNR=∞）；`triangle.gltf` no-lightmap forward/deferred 也逐字节一致。T5 在 SSGI on/off 下对 `triangle_lm_ds` 整图逐字节一致（PSNR=∞），非 lightmap 控制场景 `triangle.gltf` 的 SSGI on/off 为 45.107 dB（max diff 11），证明 SSGI 实际生效且被 lightmap mask 互斥。该 mask 对应 HDRP `LightLoop.hlsl` 中 SSGI 对 `bakeDiffuseLighting` 的替换/互斥语义。Vulkan/SSGI 运行时仍受外部 `shadows.sc` 的 `setStorageBuffer(16)` 阻塞。

### 9.6 Debug

- `--debug-view=lightmap`：复用 `baked_gi`，把 lightmap 对象的 `bakedGI*albedo` 画到 HDR color（alpha=lightmap mask），再由 `debug_lightmap.fs` 全屏显示；用于定位朝向/接缝问题。forward 与 deferred 走同一 debug pass，不依赖 `bakedIndirect`/SSGI。
- `--debug-lightmap-mask`：同一 pass 的二元 mask 视图（红=lightmapped，黑=非 lightmap），对齐 HDRP `MaterialDebug.DebugViewProperties.Lightmap` / `DebugDisplay.hlsl` 的 `DEBUGVIEWPROPERTIES_LIGHTMAP` 分支；默认仍显示 baked radiance。
- `DebugDisplayMode` 增加 `Lightmap`；`config/uniforms/debug.idl` 增加 `lightmapParams`（x=radiance scale，y=mask view）；`DebugLightmap` 使用 `DebugUniformBinder`，颜色 target 在 Lightmap 模式强制 RGBA16F 以保留 mask alpha。
- **验证（D3D11，D7.2 方向贴图）**：`triangle_lm_ds` 的 forward/deferred radiance debug 截图 hash 完全一致；mask debug 均为 `(255,0,0)`，mask 背景近黑 `(4,4,4)`。无 lightmap 的 `triangle.gltf` 输出全黑。正常 `triangle_lm_ds` 采样 `(140,62,38)`（D7.2 方向贴图重生成后）。

---

## 10. 坐标系与朝向校准（D7，必须实测）

三件事**不能靠推理**，各做一个测试：

1. **UV2 V 轴**：非对称场景（+X 红灯、+Z 绿灯、白地板），对比 Dawn 结果与预期；决定 `uv2.y` 是否取 `1-v`。**实测结论：不翻转。** Dawn `IncidentLighting` 红峰在 `u=127,v=63`、绿峰在 `u=64,v=127`；640×360 运行时 debug 为右红、上绿，DDS/UV 行序一致。原版普通 mesh 的 `LightmapScaleOffset` y 为正（`DawnPackLightmapAtlas.cs:324`），只有 landscape 分支才用负 y（`:320`）；importer 也按 Y 行写 `SetPixels`（`DawnLightmap2DImporter.cs:46-57`）。
2. **方向贴图归一化**：单面 + 单平行光（强度已知）+ albedo=1，确认 `IncidentLighting` + `SHVectorR` 解码的缩放；D7.2 实测 `u_pass_lightmapParams2.x=1.0`。
3. **灯光单位**：点光距离 1/2/4m 的亮度比，校准 `Attenuation/FalloffExponent/LightPower`。**实测结论：法线正上方三个点光在 1/2/4m 的中心亮比为 0.253/0.0616（理论 0.25/0.0625），有效辐射为 `Color * intensity / d²`；`Attenuation=1/range` 只用于 LightGrid，`FalloffExponent=1` 在 inverse-square 路径忽略，`LightPower` 为元数据，不额外乘亮度。**

校准结果写入本文档 §15 的"校准记录"表，并固化到 `config/dawn.json`（D7.7）；`hbg_dawnbake --dawn-config-test` 校验配置与验收常量一致，T2/T6 fixture 直接从该配置读取 UV 翻转、atlas/padding、方向层数、线性色空间和灯光单位系数。

D7.8 参考路径对比见 [`dawn-gi-unity-comparison.md`](dawn-gi-unity-comparison.md)：同一 T2 Dawn 实烘结果分别按原版 Unity 导入语义（Incident RGBAHalf、Direction RGBA32）和 HBG 运行时（RGBA16F）解码。按 `Color32` 截断 + 线性采样模型，方向 8-bit 量化引起解码最大绝对差 0.019798、PSNR 58.39 dB；runtime 仅 3.76% 像素不同、最大 2 LSB。原版方向纹理导出 PNG 后按默认路径以 Unity `DirectionalLightmap` 导入，该类型资源 meta 为 `sRGBTexture: 0`，线性采样分支有强证据但依赖 Unity type setter 默认；自定义 preset 需单独核对，PNG 字节路径与实机同场景截图仍受 D0.12 阻塞。D8.1/D8.2/D8.3/D8.4/D8.5 已完成：CLI/README、缺 DLL/磁盘满/协议版本错误、dawnbake 31/31、Dawn/非 VFX 33/33；全量测试集当前 101 项，待空闲复跑；D8.6 仍受 D0.9/D0.12 与 D7.8 阻塞；D0.10 仅日志清理。

---

## 11. 验收矩阵

| 编号 | 用例 | 通过标准 |
|---|---|---|
| T0 | 协议 byte-diff | 所有 golden 文件 round-trip 0 差异 |
| T1 | 单面单平行光 | 与解析解/参考图色差 < 5%；无漏光（D7.4 ΔE76=0.0032） |
| T2 | UV 朝向 | 非对称场景方向正确 |
| T3 | 接缝 | 多块地板拼 atlas，边界无黑缝/串色（D7.5：邻边最大相对差 0.0129%，18,496 padded texel mapped） |
| T4 | forward/deferred | 同一视角两路径 PSNR ≥ 40 dB |
| T5 | SSGI 互斥 | 静态物体开/关 SSGI 的间接项差异 < 2% |
| T6 | 性能 | 100 静态物件：打包 < 2s，烘焙总耗时记录；显存 < 100MB（D7.6：pack 约 1–4ms，Agent wall 55.5s，96MiB） |
| T7 | 回归 | `ctest` 全绿 + `idl_gen --check` + shader build |

---

## 12. 风险与回退

| 风险 | 影响 | 应对 |
|---|---|---|
| Dawn 1.3.5 运行时 OIDN/Vulkan 日志空指针崩溃 | D0.8 本地已解除；官方 exe 可回退 OptiX 完成烘焙 | 干净机器复测 D0.9；CPU OIDN 已实测可用，D0.10 仅清理日志 |
| `Dawn.exe` 必须挂 Agent | D4 +2–3 周 | D0 已排除：`-local -localdirectory` 直启可用 |
| 本仓库二进制缺 OptiX DLL | 完全跑不起来 | D0 已排除：OptiX 走驱动 `nvoptix.dll`，无 `optix*.dll` 依赖 |
| xatlas `resolution=0` 语义与预期不符 | D3 返工 | **已实测**：输出为 texel 单位，自动 atlas；统一按 raw span 归一化，D3.1 关闭 |
| Shader sampler 槽位冲突 | D6 编译/运行失败 | 先做槽位审计脚本；必要时把 `s_decal0..3` 合并成数组释放槽位 |
| Dawn 协议版本漂移 | 全部返工 | 锁定插件 2.4.5 / 协议 1002；升级流程见 §14 |
| SH 语义/缩放不明 | 画面偏亮/偏暗 | 只调全局系数，不改结构；T1 反推 |
| 授权问题 | 项目无法交付 | 内部使用已确认；对外分发仍需书面授权 |

---

## 13. 交付物清单

```
tools/dawnbake/
  CMakeLists.txt
  main.cpp
  protocol/dawn_protocol.h/.cpp        # 结构体 + 序列化 + gzip
  protocol/golden_test.cpp             # round-trip / sizeof 自检
  convert/scene_to_dawn.h/.cpp
  convert/material_bake.h/.cpp
  convert/light_convert.h/.cpp
  convert/sky_convert.h/.cpp
  unwrap/xatlas_wrapper.h/.cpp
  pack/lightmap_packer.h/.cpp
  submit/dawn_launcher.h/.cpp
  submit/dawn_agent_client.h/.cpp      # 仅 D4 备选
  import/lmgz_reader.h/.cpp
  import/dilate.h/.cpp
  import/dds_writer.h/.cpp

src/render/lightmap/
  lightmap.h/.cpp

shaders/lighting/baked_gi.sc
shaders/passes/gbuffer.fs.sc           # 修改
shaders/passes/deferred_lighting.fs.sc # 修改
shaders/passes/ssgi_composite.fs.sc    # 修改
shaders/lighting/light_loop.sc         # 修改
shaders/include/varying.def.sc         # 修改
shaders/include/varying_instanced.def.sc
config/uniforms/prepass.idl            # 修改
config/uniforms/pass.idl               # 修改
config/uniforms/debug.idl              # 修改

tests/dawn/golden/...                  # D0 黄金样本
config/dawn.json                       # 烘焙/校准配置
docs/dawn-gi-plan.md                   # 本文
docs/dawn-gi-checklist.md              # 配套清单
```

---

## 14. 实施守则

1. **每完成一个 D 项就回填 `dawn-gi-checklist.md`**，写清楚验收数据（PSNR/diff/耗时）。
2. 改动后跑：`cmake --build --preset vs2022-debug` → `ctest --test-dir build/vs2022 -C Debug --output-on-failure` → `py -3 tools/idl_gen.py --check`。
3. 改 IDL 后必须重新生成 `src/gfx/generated/uniforms.h` 与 `shaders/include/generated/uniforms.sc`，不手改生成文件。
4. 改 shader 后按现有流程重建；新增 pass 要登记到 `shaders/manifest.json`（如适用）。
5. 协议层任何字段/顺序改动都必须先更新 golden 样本与 §4 文档，再动代码。
6. 升级 Dawn 插件版本时：重新抓 golden → 跑 T0 → 对比 `EGPUBakingVersion` → 记录 diff 到本文"校准记录"。
7. 不提交 `Jobs/`、`_FileCache/`、大体积临时产物；golden 只保留最小覆盖集。

---

## 15. 校准记录（实施时填写）

| 项 | 值 | 场景/方法 | 日期 |
|---|---|---|---|
| UV2 V 轴 | 不翻转（0） | T2 非对称场景；Dawn 实烘 + 运行时 640×360 | 2026-11-20 |
| 方向贴图/缩放 | 1.0 | T1 单面单平行光；`IncidentLighting=1`、`SHVectorR=(0.5,1.0,0.5,1.0)` | 2026-11-20 |
| 灯光单位系数 | 1.0（无额外系数） | D7.3 法线正上方点光 1/2/4m；`IncidentLighting` 比值 0.253/0.0616，理论 0.25/0.0625 | 2026-11-20 |
| T1 线性色差 | ΔE76=0.0032（max 通道误差 0.0259%） | D7.4 sRGB `(0.8,0.6,0.4)`、intensity 0.5，对比解析线性值 | 2026-11-20 |
| T3 接缝 | 邻边最大相对差 0.0129%，ring 18,496 texel 全映射 | D7.5 四块 2m 地板逐块 bake，atlas 256、edgePadding=2 | 2026-11-21 |
| T6 100 物件 | pack 约 1–4ms；100/100 Agent wall 55,491ms；VRAM 96MiB | 10×10 个 2m 白地板、64² lightmap、2048 atlas、100 unique jobs；D7.6 | 2026-11-21 |
| atlas 默认尺寸 | 2048 | 100 物件试包；`config/dawn.json.lightmap.atlasSize` | 2026-11-21 |
| texelsPerUnit 默认 | 6.0 | 视觉验收；`config/dawn.json.lightmap.texelsPerUnit` | 2026-11-21 |
| 配置入口 | `config/dawn.json` v1 | D7.7；`--dawn-config-test` 校验 | 2026-11-21 |
| Unity 参考路径（D7.8） | 方向 8-bit 解码 max 0.019798、PSNR 58.39 dB；runtime max 2 LSB | 同一 T2 Dawn 实烘；原版方向导出 PNG + `DirectionalLightmap` 导入（meta `sRGBTexture:0`）+ Unity `SampleDirectionalLightmap`；PNG 字节路径与实机截图待 D0.12 | 2026-11-21 |

### 15.1 固化配置

`config/dawn.json` 是 D7 校准值的配置入口：

```json
{
  "version": 1,
  "lightmap": {
    "uv2VFlip": false,
    "texelsPerUnit": 6.0,
    "maxLightmapSize": 1024,
    "atlasSize": 2048,
    "maxAtlases": 2,
    "padding": 2,
    "dilationIterations": 4,
    "directional": true,
    "format": "rgba16f",
    "layers": 3
  },
  "lighting": {
    "colorSpace": "linear",
    "directionalLightmapScale": 1.0,
    "lightUnitScale": 1.0,
    "pointFalloff": "inverse-square",
    "lightPowerAffectsRadiance": false
  }
}
```

- 原版字段对应：`MaxLightmapSize/TexelPerUnit/Padding` 见 `DawnSettings.cs:122-131,827-834`；尺寸公式见 `DawnLightmapSizeCalculate.cs:11-17,32-44`；UV2 普通 mesh 的 y 为正见 `DawnPackLightmapAtlas.cs:320-324`；方向标志见 `DawnLightmap2DExporter.cs:193`；`IncidentLighting/SHVectorR` 层见 `DawnLightmap2DImporter.cs:53,74`；RGBAHalf/线性见 `DawnEncodeTextures.cs:148-149`；点光 inverse-square/Attenuation/LightPower 见 `DawnLightExporter.cs:275-280,328,351-375`。
- 本项目 `maxLightmapSize=1024` 是单块上限，`atlasSize=2048` 是打包图集尺寸；两者语义分开，避免把原版同名 `MaxLightmapSize` 混用。
- `edgePadding` 仍由 `--write-tile-atlas <out> <atlas-size> <edge-padding> ...` 显式传入（D7.5 用 2），不作为全局默认，避免改变 D5.4 的 `LightmapAtlasOptions` 旧行为。
- 校验：`hbg_dawnbake --dawn-config-test [path]` 读取并检查 schema/取值，同时断言 UV2 不翻转、灯光单位系数 1.0、方向缩放 1.0、RGBA16F/3 层、`LightmapUvOptions`/`LightmapSizeSettings`/`LightmapPackingSettings` 及 `LightmapAtlasOptions::dilationIterations` 默认值与配置一致；CTest `dawnbake_config`。

### 15.2 2026-11-25 ShadowMask SDF 补充

- `D2.16` 完成 `SDFShadowJobs`/`SDFShadowGroupJobs`：相关灯、单 job、group job、`SdfShadowParameter`/`ShadowSettingParameter`；Agent 使用 `SHADOWMASK_GROUP`，真实链路下载 2 个 `.sdfgz` 并通过 `--verify-sdfgz`（3461 samples/2986 mapped）。
- 双灯真实验证：job6 的 2 个 `.sdfgz` 各含 2 maps，`ShadowMaps[i].LightID` 顺序与输入 `ShadowmaskLights[i]` 一致；`--verify-sdfgz` 6922 samples/5972 mapped。
- `lightmap.taskPackSize` 默认 512，按原版 packed/single 两条分支生成 SDF group（task padding flag 0/1 计入 cell）；job7 双 job 合成 256 group（shelf 版 offset `(0,0,0)`/`(52,0,1)`，3/3 tasks），job8 512/311 任务各生成单 job group。2026-11-26 后 packed SDF group 继承 lightmap `MappingGroupJobs` 的 `GroupedSize`/`MappingOffsets`，单 job 按 max(w,h) 升序；旧 job7/job9 offset 仅作历史记录。
- 2026-11-26 将 final atlas 与 task mapping 打包替换为原版 `AtlasNode`：final atlas 降序，task mapping 升序；`--atlas-test` 增加 mapping offset，`--scene-export-test` 校验 SDF 与 lightmap group 同 offset；CTest **36/36**。
- 2026-11-26 补 lightmap `LIGHTMAP_FLAGS_PADDING`：原版 `DawnLightmap2DExporter.cs:161,191` 在 allocation padding>0 时设置；现在 lightmap 与 SDF job 都按 `config.lightmap.padding>0` 设置，task mapping/split 统一按 raw size+2 计算 cell。
- lightmap job 也按原版生成 `MappingGroupJobs`，入组 job 只发 `LIGHTMAP2D_GROUP`；job9 双灯（taskPackSize=256）仅 2 个 group task，`Dawn results: validated`，输出 2 个 `.lmgz` + 2 个 `.sdfgz`。
- 超尺寸未入组 lightmap 按原版 `DawnLightmap2DSplitTask` 生成 `MappingSplitJobs`/`LIGHTMAP2D_SPLIT`，`--verify-lmgz` 按原版 split importer 合并；job10（512/311、pack256、samples=4）13 个 split tile + 2 个 SDF group，`tasks=15/15`、`Dawn results: validated`，合并后 512×512/311×311。
- 新增 `--write-job-atlas`：job 结果（含 split merge）经 sidecar 的 `uv2Offset/uv2MapSize` 匹配后写所有 atlas DDS；有 `PADDING` 时结果 header 为 raw+2，匹配必须用 raw size。`writeLightmapSidecar()` 用 `ordered_json` 保留 object 顺序，job atlas 先按同序 index 匹配（支持多个 atlas 的相同 rect）再唯一回退；`--verify-job-atlas` 按 `placement-padding` 回读校验，真实 job10 1 atlas/2 blocks/358,865 texels 通过；`--bake ... --atlas-out <dir>` 在结果校验后自动执行同一路径；`--job-atlas-test` 覆盖 split→atlas、padded split→atlas、2 atlas/重复 rect 和 post-bake 输出。带 padding 的 block 额外 edge ring 按 atlas 可用边距 clamp，避免原版 packing padding 2 与默认 edgePadding 2 冲突。
- Agent task cost 已按原版 lightmap/group/split/SDF-group 公式补齐，并对 `INT_MAX-1` 截断；`--agent-spec-test` 覆盖 packed、oversized、split 混合任务。
- 原版参考与验收细节见 `docs/dawn-gi-checklist.md` D2.16 与 `tools/dawnbake/README.md`。

