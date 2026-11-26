# Shader Analysis 工具：规划与实施清单

> 状态：P0–P7 已完成（C++ 后端、报告索引/Diff/CSV/Reference、Web 浏览/寄存器/CBuffer/Source/Diff、使用文档）；P0b 与 Assembly→Registers 指令聚焦已完成
>
> 目标：在 `hdrp-bgfx` 内用 **C++20** 重写 Unity `com.unity.shaderanalysis` 的 Shader 性能/静态分析能力，并用 **Vite + React + TypeScript** 复刻其报告浏览与汇编/寄存器分析界面；数据后端对应本项目的 **DXBC（D3D11）** 与 **SPIR-V（Vulkan）**，不是 PS4/PS5 GCN。
>
> 本文档是后续实现的唯一入口：每个阶段完成后在本文档勾选并回填实际验证结果；实现与本文档冲突时，先修文档再改代码。

## 0. 基线、范围与已确认决策

### 0.1 代码基线

| 基线 | 值 |
|---|---|
| hdrp-bgfx 提交 | `be1ed57435f9ab4efdb074d92ddee2106f8c30fd`（2026-11-23，工作区有未提交改动；本文档行号以当前工作区为准） |
| Unity ShaderAnalysis | `E:\Learn\Graphics\Packages\com.unity.shaderanalysis`，仓库提交 `0efc28ea742b1966b9dd9cc4489e58b986a13faf` |
| bgfx.cmake | `v1.167.9621-609`（`cmake/Dependencies.cmake:13-18`） |
| bgfx 源码 | 工作区 build 下的 `bgfx-src`，提交 `a9f89bf75` |
| shaderc 能力 | 以 `build/vs2022/_deps/bgfx-src/tools/shaderc` 实际源码为准（见 1.3） |

> 注意：`docs/m11.3-variant-strategy.md:68` 写的 “manifest 510 个 binary” 与当前 `shaders/manifest.json` 不一致。按当前 manifest 去重统计为 **920 个唯一路径（dxbc 460 / spirv 460）**，编译调用点约 140 处。P0 已确认 `shaders/variants.json` 为 920 条（460/后端），并已在 m11.3 文档加注“以 variants.json 为准”；当前工作区已重新导出为 950 条（475/后端）。

### 0.2 范围

**做：**

- 解析 `shaders/variants.json`（由 `tools/shader_build.py` 导出的编译计划）。
- 调用项目自带 `shaderc` 编译每个变体，生成/提取 DXBC 与 SPIR-V。
- 解析 DXBC 反汇编与 SPIR-V（反汇编 + 反射），产出结构化 JSON 报告、CSV、diff。
- Web 端复刻原 Unity 工具的两个窗口：报告浏览器（`ShaderAnalysisInspectorWindow`）与汇编/寄存器分析器（`ShaderViewerWindow`）。
- 本地单测（doctest）与本地 Web 开发/构建。

**不做：**

- 不修改 Unity 包本身。
- 不实现 PS4/PS5/GCN 后端，不复刻 `ShaderAnalyzer` 的 PS4/PS5 汇编文本格式。
- 不承诺硬件占用率（CU/SIMD waves）等主机专用指标；这些字段在新 schema 中明确为 `null`/不可用。
- 不把 Web 构建或报告生成纳入 CI 门禁（已确认仅本地使用）。
- 不移植 Unity `Material`/`Shader` 资产的 GUID、AssetDatabase、ObjectField、EditorPrefs 机制。

### 0.3 已确认决策

1. 后端同步：DXBC + SPIR-V 同一阶段推进。
2. Web：Vite + React + TypeScript。
3. 变体清单：抽取为共享的 `shaders/variants.json`，构建脚本与 C++ 工具共用同一份编译计划。
4. 报告交付：静态 JSON + 本地只读静态服务器。
5. 源码映射：默认优化模式（无行号映射）；`--debug-info` 为可选模式，报告中标注“调试构建，非发布产物”。
6. 入库策略：只提交摘要 JSON + 少量 reference 基线；明细 JSON、`.bin`、`.disasm` 本地生成并 gitignore。
7. Web 仅本地使用，不做 CI 门禁。

## 1. 源码索引（实现前必须核对）

> 行号基于 2026-11-23 工作区；若代码变动，用符号名重新定位，不要凭记忆改行号。

### 1.1 Unity 原实现（行为来源）

| 能力 | 源码位置 | 要点 |
|---|---|---|
| 功能总览 | `com.unity.shaderanalysis/Readme.md:1-59` | 报告生成、Excel/CSV、Temp 目录、Reference Diff、源码打开、SCM 存储、编译单元定义 |
| 报告数据模型 | `Editor/API/ShaderBuildReport.cs:44-131` | `GPUProgram`：名称、源码、defines、multicompiles、multicompileCombinations、警告/错误计数 |
| 编译单元 | `Editor/API/ShaderBuildReport.cs:151-214` | `CompileUnit`：program/multicompile 索引、defines、warnings/errors、profile、entry |
| 性能单元 | `Editor/API/ShaderBuildReport.cs:216-265` | `PerformanceUnit`：raw report、parsed metrics、extra perf 文件 |
| 指标结构 | `Editor/API/ShaderBuildReport.cs:267-309` | `LogItem`、`ProgramPerformanceMetrics` 及逐字段 `Diff` |
| 报告索引 | `Editor/API/ShaderBuildReport.cs:311-336` | programs/compileUnits/performanceUnits 列表；`skippedPasses` |
| Defines 匹配 | `Editor/API/ShaderBuildReport.cs:483-493` | `HashDefines` 按字符串 hash 求和（用于 diff 匹配） |
| 源码临时文件 | `Editor/API/ShaderBuildReport.cs:496-532` | 用 `FileUtil.GetUniqueTempPathInProject` 写临时源码并打开 |
| 流水线基类 | `Editor/Platforms/ReportBuildData.cs:16-45` | `CompileUnit`（源文件/选项/目标/产物/warnings/errors）、`PerfReport` |
| 流水线阶段 | `Editor/Platforms/ReportBuildData.cs:99-130` | `BuildPerformanceUnits` 为每个编译单元建 perf 报告 |
| 并行编译 | `Editor/Platforms/ReportBuildData.cs:169-250` | 最多 8 并发；产物缺失时解析错误并重试；统计编译进度 |
| 编译日志解析 | `Editor/Platforms/ReportBuildData.cs:287-337` | 按行切分，`error`/`warning` 归类，stacktrace 聚合 |
| Shader 数据 | `Editor/Platforms/ShaderReportBuildData.cs:14-32` | `Pass`：名称、pass 索引、源码、源文件、multicompiles、combinedMulticompiles |
| Pass 枚举 | `Editor/Platforms/ShaderReportBuildData.cs:71-102` | 取 ActiveSubshader；按 include/exclude pass 名过滤；空名用 `D3` 序号 |
| 变体解析 | `Editor/Platforms/ShaderReportBuildData.cs:118-159` | `#pragma multi_compile`、`#pragma target`；Cartesian product |
| 编译单元构造 | `Editor/Platforms/ShaderReportBuildData.cs:161-208` | entry 来自 `#pragma vertex/fragment/...`；加 SHADER_STAGE_* define；目标固定 PS_5 |
| 报告导出 | `Editor/Platforms/ShaderReportBuildData.cs:214-254` | 跳过 pass 记录；program/compileUnit/perfUnit 组装 |
| Compute 支持 | `Editor/Platforms/ComputeShaderReportBuildData.cs:12-144` | `#pragma kernel` 解析；每 kernel 一个编译单元；target CS_5 |
| 正则/路径/默认选项 | `Editor/Internal/ShaderAnalysisUtils.cs:16-19, 25-96, 102-132, 134-210, 212-228` | shader model/multicompile/kernel 正则；Temp 目录布局；多编译展开；kernel 解析 |
| Asset 报告存储 | `Editor/Internal/ShaderAnalysisUtils.cs:233-290` | 每个 BuildTarget 一份 `AssetMetaData_<target>.asset`，按 asset GUID 存取 |
| Diff 算法 | `Editor/Internal/ShaderAnalysisUtils.cs:329-374` | 按 program 名匹配；按 defines hash 匹配 compile unit；逐指标相减 |
| 默认编译选项 | `Editor/Internal/ShaderAnalysisUtils.cs:376-427` | include 目录、defines、entry、`SHADER_TARGET`、Unity CGIncludes/PlaybackEngines |
| 过滤规则 | `Editor/API/ShaderProgramFilter.cs:46-79` | `+A,B` / `-A,B`；`A&B|C&D`（OR of AND） |
| CSV 列 | `Editor/API/Export/ExportFormat.cs:7-27` | Pass/索引/defines/microcode/VGPR/SGPR/LDS/waves/occupancy |
| CSV 写出 | `Editor/Internal/Export/CSV/CSVReportExport.cs:15-58` | 分号分隔、字符串加引号 |
| 报告窗口 | `Editor/Internal/ShaderAnalysisInspectorWindow.cs:467-483, 485-514, 568-660, 667-681, 740-759, 770-775` | 生成/导出/diff/reference；树形折叠与错误警告图标；过滤输入 |
| 平台作业 | `Editor/Platforms/BuildReportJobAsync.cs:149-172` | Material 的 pass 启用状态与 keywords 进入过滤 |
| Viewer 注册/加载 | `Editor/Viewer/ShaderViewerWindow.cs:107-268, 396-412, 449-502` | 五 Tab；分析时按需编译；优先加载 `extraPerfDataFile` |
| Viewer 代码行/寄存器图 | `Editor/Viewer/ShaderViewerWindow.cs:619-688, 763-961, 963-1001` | 生命周期条带、读写条、占用率渐变、依赖链高亮 |
| Viewer 跳转/ cbuffer | `Editor/Viewer/ShaderViewerWindow.cs:1009-1072` | 跳转竖线（固定随机种子 42）、cbuffer offset 顺序高亮 |
| 汇编解析 | `Editor/Viewer/ShaderAnalyzer.cs:13-21, 81-120, 122-336` | 寄存器类型、CodeLine/AliveRegister、生命周期与压力分析 |
| PS4/PS5 文本格式 | `Editor/Viewer/ShaderAnalyzer.cs:338-522` | 两种反汇编文本格式与寄存器表解析（仅作算法参考） |
| 跳转缩进 | `Editor/Viewer/ShaderAnalyzer.cs:525-550` | 基于跳转范围的栈式缩进 |
| Kernel 枚举 | `Editor/Viewer/ShaderAnalyzer.cs:597-636` | `#pragma kernel` 与 multi_compile 关键字 |
| 外部编译器封装 | `Editor/Platforms/ShaderCompiler.cs:33-128, 130-156` | 进程重定向、commandLine、取消/完成 |
| 平台入口 | `Editor/API/EditorShaderTools.cs:25-56, 85-98` | 能力检查、按资产类型派发、平台工厂注册 |

### 1.2 hdrp-bgfx 集成点

| 能力 | 源码位置 | 要点 |
|---|---|---|
| Shader 源 | `shaders/passes/*.sc`、`shaders/post/*.sc`、`shaders/ui/*.sc`、`shaders/vfx/*.sc`、`shaders/shadergraph/*` | bgfx `.sc`；include 在 `shaders/include`、`shaders/lighting`、`shaders/material`、`shaders/common` 等 |
| 变体构建 | `tools/shader_build.py:43-52` | `VARIANT_BITS`（HAS_NORMAL_MAP/HAS_MASK_MAP/ALPHA_CLIP/DOUBLE_SIDED）、`BACKENDS`（dxbc: windows/s_5_0；spirv: linux/spirv） |
| 编译命令 | `tools/shader_build.py:55-143` | `shaderc -f ... -o ... --type ... -i ... --platform ... -p ... -O 3 --Werror --varyingdef ... --define ...`；D3D 可跳过优化 |
| 变体展开 | `tools/shader_build.py:147-161` | `variant_defines(mask)` |
| 缓存/校验 | `tools/shader_build.py:164-189` | manifest 路径枚举、缺失输出校验、陈旧输出清理 |
| CLI | `tools/shader_build.py:192-233` | `--force/--jobs/--timeout/--only/--no-manifest/--optimization` |
| 关键变体组 | `tools/shader_build.py:246-289, 625-637, 884-914, 972-1406` | forward/gbuffer/prepass/shadow/deferred variant 等；manifest 字典按固定顺序构造 |
| 收尾 | `tools/shader_build.py:1408-1452` | 等待任务、统计、失败输出、校验、写 `shaders/manifest.json` |
| Manifest 结构 | `shaders/manifest.json:1-*` | `passes.{dxbc,spirv}.<pass>.{vertex,fragment(s),bufferFragment(s),compute(s)}` |
| 运行时读取 | `src/shader/shader_library.h:33-42, 155-165` | `PassFiles` 与 `loadManifest`；运行时按 path 载入 |
| CMake 选项 | `CMakeLists.txt:15-24` | `HBG_BUILD_SHADER_TOOLS`、`HBG_SKIP_MAIN_SHADER_BUILD`、`HBG_SHADERC` |
| CMake 编译目标 | `CMakeLists.txt:240-272` | 选择 `shaderc` 可执行文件；`hbg_shaders` 调 `tools/shader_build.py` |
| CMake 工具 target 风格 | `CMakeLists.txt:75-81, 320-327` | `hbg_target_options`；`add_subdirectory(tools/dawnbake)`；测试入口 |
| 测试约定 | `tests/CMakeLists.txt:1-26` | `hbg_tests` + doctest + `add_test`；`HBG_PROJECT_ROOT` |
| 工程约定 | `AGENTS.md`（“铁律/构建与测试/代码约定”节） | 先读源码；C++20、`hbg` 命名空间、PascalCase/camelCase、`m_`/`k_`；doctest |
| Git 忽略 | `.gitignore:1-22` | `/shaders/build/` 已忽略；新增 cache/明细需要补充 |

### 1.3 bgfx/shaderc 能力（当前构建实际支持）

| 能力 | 位置 | 说明 |
|---|---|---|
| `--disasm` | `build/vs2022/_deps/bgfx-src/tools/shaderc/shaderc.cpp:1069, 2583` | 仅 DirectX；解析到 `options.disasm` |
| DXBC 反汇编写文件 | `.../tools/shaderc/shaderc_hlsl.cpp:858-875` | `D3DDisassemble(...)` 后写 `<outputFilePath>.disasm` |
| SPIR-V verbose 反汇编 | `.../tools/shaderc/shaderc_spirv.cpp:803` | `glslang::SpirvToolsDisassemble(std::cout, ...)`，受 `g_verbose` 控制 |
| SPIR-V 反射输出 | `.../tools/shaderc/shaderc_spirv.cpp:751` | `program->dumpReflection()`（verbose） |
| SPIR-V 产物布局 | `.../tools/shaderc/shaderc.cpp:18-20, 2315-2327`；`shaderc_spirv.cpp:355-400, 906-916` | 先写 chunk magic + 2 个 hash；再 `RawBindings`（srv/uav）+ uniform 表；再 `u32 shaderSize` + SPIR-V words |
| Debug 信息 | `.../shaderc.cpp:1068, 2603`；`shaderc_spirv.cpp:758-761` | SPIR-V `--debug` 会设置 `disableOptimizer=true`，产物与发布优化不同 |
| 依赖文件 | `.../shaderc.cpp:1051, 2654` | `--depends` 生成 makefile 风格依赖，可用于缓存 key |
| 预处理 | `.../shaderc.cpp:1061, 2655` | `--preprocess` 只输出预处理结果 |
| spirv-cross target | `build/vs2022/_deps/bgfx.cmake-src/cmake/bgfx/3rdparty/spirv-cross.cmake:47-60` | `add_library(spirv-cross STATIC ...)` |
| spirv-opt target | `build/vs2022/_deps/bgfx.cmake-src/cmake/bgfx/3rdparty/spirv-opt.cmake:148-160` | 含 `disassemble.cpp`，可用于 `spvBinaryToText` |
| shaderc 依赖 | `build/vs2022/_deps/bgfx.cmake-src/cmake/bgfx/shaderc.cmake:22-29` | shaderc 已链接 `glslang`、`spirv-opt`、`spirv-cross` |

## 2. 原实现行为拆解（必须对齐的部分）

### 2.1 报告数据模型

原模型三层：

1. `GPUProgram`：一个 “pass/程序”；包含源码、defines、multicompile 组、组合结果，以及编译单元/性能单元集合。
2. `CompileUnit`：一次具体编译；由 `(program, multicompileIndex)` 唯一确定；保存全部 defines、警告、错误、profile、entry。
3. `PerformanceUnit`：对应编译产物的性能报告；保存 raw text、解析后的指标、额外 perf 数据文件路径。

新模型沿用三层语义，但“程序”改为 **源文件 + stage**（理由见 3.4）：
- `ShaderProgram`（源文件+stage+backend）
- `Variant`（一次编译；canonical defines 唯一）
- `VariantDetail`（反汇编/寄存器/资源/cbuffer 明细）

### 2.2 构建流水线

原 `ShaderBuildData` 的阶段（`Editor/Platforms/ShaderReportBuildData.cs:71-254`）：

1. 取 ActiveSubshader 的所有 pass；
2. 应用 pass include/exclude 过滤；
3. 解析每个 pass 的 `#pragma multi_compile` → multicompiles；
4. 解析 `#pragma target` → shaderModel；
5. 对 multicompiles 做 Cartesian product → combinedMulticompiles（过滤 `_`，应用关键字集合过滤）；
6. 为每个组合建编译单元：entry 来自 `#pragma vertex/fragment/hull/domain`；defines = shader keywords + 组合 defines + `SHADER_STAGE_*`；加 include 目录；
7. 最多 8 并发调用编译器；产物缺失时解析错误并重试；成功则解析 warnings/errors；
8. 生成 perf 单元（编译产物→perf 报告）；
9. 组装 `ShaderBuildReport`：跳过 pass、program、compile unit、perf unit。

`ComputeShaderReportBuildData` 的差异（`Editor/Platforms/ComputeShaderReportBuildData.cs:45-144`）：直接读源文件，按 `#pragma kernel NAME [defines...]` 每个 kernel 一个编译单元（`multicompileIndex = -1`），target CS_5。

**新实现映射**：`variants.json` 已经是“展开后”的编译清单，因此第 3–6 步在构建期由 `shader_build.py` 完成，C++ 不再解析 `#pragma multi_compile`；C++ 只负责“选择变体 → 编译 → 解析 → 报告”。这与原实现结果等价，因为 `variants.json` 是同一份构建计划的导出。

### 2.3 变体展开规则（保留在 `shader_build.py`）

- `ParseVariantMultiCompiles` 正则：`^[\s\r]*(#pragma\s+multi_compile\s+(?:([\w_]+)\s+)+)[\s\r]*$`（`Editor/Internal/ShaderAnalysisUtils.cs:18`）。
- `BuildDefinesFromMultiCompiles`（`:134-210`）：Cartesian product；token `_` 忽略；过滤条件为“包含任一关键字集合的全部关键字”（OR of AND）。
- `ParseShaderModel`：`#pragma target X.Y` → 去掉小数点；对应 shaderc 的 `--profile` 由后端决定（DXBC `s_5_0`，SPIR-V `spirv`），`SHADER_TARGET` 不是 bgfx 构建的输入。
- `ParseComputeShaderKernels`：`#pragma kernel NAME [defines...]`（`ShaderAnalyzer.cs:597-636` 也做同样解析）；新工具直接使用 variants.json 的 stage/compute 条目，不再重复解析。

### 2.4 编译调度与错误处理

原实现（`Editor/Platforms/ReportBuildData.cs:169-250`）：
- `k_MaxParallelCompilation = 8`；
- 同一时间最多 8 个 job；
- 产物不存在时：把 stderr 按 `^~` 分段输出；`throwOnError` 为真则抛异常；否则**重新入队**（原实现可能无限重试，属于已知缺陷）；
- 产物存在且 stderr 非空时：调用 `ParseShaderCompileErrors` 收集 warnings/errors；
- 进度按完成数更新。

新实现（有意差异，需在报告中注明）：
- 默认 `--jobs 8`（对齐原值）；
- 默认 `--retries 0`（避免原实现的无限重试）；可选 `--retries N`；
- 默认单个变体失败只记录，不中止；`--fail-fast` 对齐 `throwOnError`；
- 日志解析按后端分别实现（shaderc/FXC 文本），不直接复用 Unity 的 `^~` 规则；
- 进度输出 `--progress=plain|json|none`。

### 2.5 过滤规则

原 `ShaderProgramFilter.Parse`（`Editor/API/ShaderProgramFilter.cs:55-78`）：
- pass 过滤器：首字符 `+` → include 列表；`-` → exclude 列表；其余按 `,` 分割；
- keyword 过滤器：按 `|` 分组，每组按 `&` 分割；变体只要包含某一组的全部关键字即选中；
- 空/未提供 → 不限制。

新实现：
- `--pass` 匹配对象为 **源文件名（去扩展名）再去的 stage 后缀**（如 `forward.fs` → `forward`），同时接受完整 stem（`forward.fs`）和 stage（`fragment`）；显式 `+`/`-` 保持原 include/exclude 语义，未带前缀的 CLI 写法按 include 处理（`--pass forward` 等价 `+forward`）；逗号分隔 token 支持 `*`/`?`。
- `--variant-filter` 保持 `A&B|C` 语义，关键字为 define 的 **名称部分**（`NAME=VALUE` 取 `NAME`）；
- 额外提供 `--define NAME[=VALUE]` 精确过滤；无 `=` 时匹配名称，带 `=` 时匹配完整 define；
- 单测必须覆盖原实现的边界用例（见 7.5）。

### 2.6 Diff

原算法（`Editor/Internal/ShaderAnalysisUtils.cs:329-374`）：
1. 遍历 source 的 program；
2. 按 program **名称**在 reference 中查找；
3. 遍历 source compile unit，按 **defines hash**（`ShaderBuildReport.cs:483-493`）在 reference 中查找；
4. 两侧都必须有 performance unit；
5. 逐指标相减，记录 source/ref 的 program/multicompile 索引。

新实现：
- program 匹配键：`sourceRelativePath + stage + backend`；
- variant 匹配键：`canonicalDefines`（排序后的 `NAME=VALUE` 列表，用换行或 `\0` 连接后做 SHA-256 短 hash）；
- 两侧 status 必须为 `ok` 且有 metrics；
- 输出 `metricDeltas`、`addedVariants`、`removedVariants`、`sourceHashChanged`、`toolchainChanged`；
- 保留“逐字段相减”的语义，字段集合见 5.4。

### 2.7 CSV

原 CSV（`Editor/API/Export/ExportFormat.cs:7-27`；`CSVReportExport.cs:15-58`）：
- 分号 `;` 分隔；字符串字段加双引号；每行以分号结尾；
- 列：Pass Name、Multicompile index、All Defines、Multicompile Defines、Micro Code Size、VGPR/SGPR 相关、LDS、waves、SIMD/CU occupancy。

新 CSV：
- 继续使用分号 + 引号（便于 Excel 直接打开）；
- 列改为后端通用字段（见 5.8），主机专用字段输出空字符串或 `n/a`；
- 提供 `--format csv` 与 Web 端下载；不实现 Excel 二进制格式。

### 2.8 Viewer 解析与可视化

原 Viewer 的关键行为（`Editor/Viewer/ShaderAnalyzer.cs`、`ShaderViewerWindow.cs`）：
- 支持两种汇编文本格式（PS4 文本、PS5 带寄存器表的表格式）；新实现不复刻这两种格式，而是直接解析 DXBC/SPIR-V。
- `RegisterType`：Scalar/Vector/Exec/VCC/M0/TMA（GCN 语义）；新实现为 DXBC `r/v/o/x/cb/t/s/u` 与 SPIR-V SSA value/resource，见 3.4 映射。
- 解析每条指令的 reads/writes、寄存器压力、cbuffer 读取、跳转标签。
- 生命周期算法（`:195-232`）是**线性反向扫描**：
  - 反向遍历指令；维护 `lastRead`；
  - 遇到 write 且 `lastRead` 有记录 → 生成 [writeLine, readLine] 区间；
  - 遇到 read → 记录第一次（反向的“最后”）读位置；
  - 从未被写的只读寄存器 → 区间从 0 开始；
  - 再正向展开为每个代码行/汇编行的 alive 集合，记录 `start`/`aliveCodeLineCount` 等。
- 压力：每条指令/每行取 `max`；占用率用 PS4/PS5 查表（`:542-617`），新实现不复刻查表。
- 依赖高亮（`:963-1001`）：点击生命周期条带后，把全部条带变暗，再沿 read→write 依赖链恢复亮度；Escape 清除（`:257-261`）。
- 跳转竖线（`:1009-1044`）：`Random.InitState(42)`（`:274`）保证颜色稳定；hover 高亮跳转区间。
- CBuffer 顺序（`:1046-1072`）：按行显示 `Read N DWORD at offset X`；若 offset 小于上一次 offset，标红表示乱序；动态 offset 蓝色。

新实现必须保留的语义：线性生命周期估计、读写标记、压力条、cbuffer offset 顺序检测、依赖链高亮、跳转缩进/连线、搜索高亮；不保留 GCN occupancy 表与 PS4/PS5 文本格式。

### 2.9 平台抽象与不移植部分

- Unity 包本身不包含 PS4/PS5 编译器实现；平台能力由 `IPlatformJobFactory` 注入（`Editor/API/IPlatformJobFactory.cs`），实际编译逻辑在包外。新工具直接在 C++ 内实现 DXBC/SPIR-V 后端，不再需要“平台工厂”抽象。
- `BuildTarget`/`ShaderProfile`/`ShaderTarget` 枚举替换为 `Backend`（dxbc/spirv）+ `Stage`（vertex/fragment/compute）。
- `AssetMetadata`/GUID/AssetDatabase 替换为源文件相对路径 + SHA-256；reference 目录替代 `EditorPrefs` 的 reference path。
- `BuildReportFeature`（GenerateVariantKeywords/StaticAnalysis）不直接移植；用 CLI 开关控制是否生成明细/指标。

## 3. 目标架构

### 3.1 组件

```
tools/shader_analysis/
  CMakeLists.txt                 # hbg_shader_analysis / hbg_shader_analysis_tests
  include/hbg/analysis/          # 公开数据模型
    variant_manifest.h           # variants.json -> CompileTask
    report.h                     # SummaryReport/Variant/Detail/Metrics
    filters.h                    # pass/variant 过滤
    hashing.h                    # SHA-256 / 短 id
    process_runner.h             # 子进程
    compiler.h                   # shaderc 调用与缓存
    dxbc_parser.h                # DXBC 文本解析
    spirv_blob.h                 # bgfx blob 提取
    spirv_parser.h               # SPIR-V 反汇编/反射/SSA
    liveness.h                   # 线性生命周期（移植 Unity 算法）
    report_files.h               # 摘要扫描/索引
    diff.h                       # 报告 diff
    export_csv.h
    reference.h                  # reference 摘要复制
  src/                           # 实现
  tests/                         # doctest + fixtures
  web/                           # Vite + React + TS
  serve.mjs                      # 本地只读静态服务
  README.md
```

### 3.2 数据流

```
shaders/variants.json
        │  (CompileTask: source/output/stage/backend/defines/varyingdef/options)
        ▼
hbg_shader_analysis build
        │  1) 过滤 tasks
        │  2) 调 shaderc（缓存）
        │  3) DXBC: 读 <out>.disasm；SPIR-V: 提取 blob + 反汇编/反射
        │  4) 解析 -> 指标/生命周期 -> JSON
        ▼
shader_analysis/reports/<backend>/<source>.json          # 摘要
shader_analysis/reports/<backend>/<source>/<id>.json    # 明细
shader_analysis/reports/index.json
        │
        ▼
serve.mjs（只读仓库根） ──► Web（fetch JSON + 源码）
```

### 3.3 Unity → hdrp-bgfx 映射

| Unity 概念 | hdrp-bgfx 概念 | 新实现 |
|---|---|---|
| Shader/ComputeShader 资产 | `shaders/{passes,post,ui,vfx}/**.sc` | `sourcePath` + `stage` |
| `#pragma multi_compile` | `tools/shader_build.py` 的 defines 组合 | `variants.json` 的 `defines` |
| `#pragma target` | 后端固定 profile（dxbc `s_5_0` / spirv `spirv`） | `backend.profile` |
| Pass（subshader pass） | manifest 的 pass 槽位 | 可选展示元数据；报告以源文件为程序 |
| Material + keywords | 变体集合（材质/灯光 mask 等） | 变体过滤 `--variant-filter` |
| BuildTarget PS4/PS5 | dxbc/spirv | `Backend` |
| Asset GUID | 源文件相对路径 | `source.path` + `sha256` |
| `Library/ShaderPerformanceReports` | `shader_analysis/reports` | 摘要 JSON |
| Reference 目录 | `shader_analysis/reference` | 摘要 JSON |
| `AssetMetaData_<target>.asset` | `reports/index.json` | 索引 JSON |
| ShaderViewerWindow | Web `VariantPage` | Assembly/Registers/CBuffer/Source |

### 3.4 与原实现的关键差异（必须在 UI/文档中标注）

1. **程序身份**：原实现以 “pass” 为程序；新实现以 “源文件 + stage” 为程序。原因：manifest 中同一 `.bin`（如 `forward.vs.bin`）会被多个 pass 槽位复用，按源文件分析可避免重复编译与分析；同时 `variants.json` 的编译任务就是源文件级。manifest pass 名作为可选展示元数据（P5 从 `shaders/manifest.json` 反查）。
2. **寄存器模型**：GCN VGPR/SGPR → DXBC temp `r#` / SPIR-V SSA value；SGPR 压力、CU/SIMD occupancy、LDS、waves 不适用，输出 `null`/`n/a`。
3. **汇编格式**：不解析 PS4/PS5 文本；DXBC 用 D3DDisassemble 文本；SPIR-V 用 `spvBinaryToText`/`spirv-dis` 文本。
4. **源码映射**：DXBC v1 无行号映射；SPIR-V 默认优化模式无行号，`--debug-info` 才有 `OpLine`（且会关闭优化）。UI 必须显示当前模式。
5. **生命周期算法**：沿用 Unity 的线性反向扫描（不是 CFG 精确活跃分析）；指标标注“静态估算”。
6. **重试策略**：默认不重试（原实现可能无限重试）；`--retries` 可选。
7. **Material/Asset**：无 Unity 资产系统；变体过滤替代 Material keywords；reference 用目录而非 EditorPrefs。
8. **CBuffer 顺序**：按计划语义在同一 slot 内当前 offset 小于上一个 offset 时标红；原 Viewer `ShaderViewerWindow.cs:1062` 的 `last < current` 条件实际方向相反，未照搬该行为。

## 4. 共享变体清单 `shaders/variants.json`

### 4.1 方案：脚本导出 + 校验 + 从 JSON 执行（P0 / P0b）

不直接把 `shader_build.py` 的 400 行 manifest 组装逻辑重写成数据驱动（风险高、易改变构建产物）。P0 采用：

- 在 `tools/shader_build.py` 的 `compile_shader` 入口记录 `CompileTask`（无论是否实际调度）；
- 新增 `--dump-plan <path>`：只生成计划，不编译；不要求 `--shaderc`/`--bgfx-src`；
- 新增 `--plan-check <path>`：生成计划并与文件比较，不一致则退出 1；同样不要求编译工具；
- 计划模式不获取构建锁、不创建 executor 或输出目录；
- 首次生成并提交 `shaders/variants.json`；
- C++ 工具只读 `shaders/variants.json`；
- `shader_build.py` 仍负责生成 manifest 槽位，但 `--plan-check` 保证计划与构建代码 1:1。

P0b 已完成：
- `build_plan()` 把完整 `shaders/manifest.json` 的 `passes` 映射写入 `variants.json`；
- Python 新增 `--execute-plan <variants.json>`：直接按 JSON `tasks` 编译，并用 JSON `passes` 写 `shaders/manifest.json`；普通构建在 `shaders/variants.json` 存在时默认使用该模式，`--no-plan` 可回退硬编码调度；
- CMake `hbg_shaders` 先执行 `--plan-check`，再使用 `--execute-plan shaders/variants.json`；Python 不再在正常构建路径执行硬编码调度；
- `--dump-plan` / `--plan-check` 仍执行硬编码调度，用于生成和校验 JSON；任何构建代码改动导致 `passes`/`tasks` 漂移都会失败。

> 这样仍然满足“共享同一份变体清单、避免漂移”：Python 的硬编码调度是计划的源，`variants.json` 是规范化导出；正常构建从 JSON 执行，`--plan-check` 强制重新导出。

### 4.2 Schema（v1）

```json
{
  "version": 1,
  "generatedBy": "tools/shader_build.py --dump-plan",
  "optimization": 3,
  "backends": {
    "dxbc": { "platform": "windows", "profile": "s_5_0" },
    "spirv": { "platform": "linux", "profile": "spirv" }
  },
  "includeDirs": ["shaders", "shaders/include", "{bgfxSrc}/src"],
  "passes": {
    "dxbc": {
      "forward": {
        "vertex": "build/dxbc/forward.vs.bin",
        "fragments": ["build/dxbc/forward.fs.0.bin"]
      }
    }
  },
  "tasks": [
    {
      "source": "shaders/passes/forward.fs.sc",
      "output": "build/dxbc/forward.fs.0.bin",
      "stage": "fragment",
      "backend": "dxbc",
      "defines": ["HAS_NORMAL_MAP=1", "HAS_MASK_MAP=1"],
      "varyingDef": null,
      "d3dSkipOptimization": true,
      "optimization": 0
    }
  ]
}
```

字段规则：
- `source`/`varyingDef`：相对仓库根，POSIX 分隔符；`output` 相对 `shaders/`（与 manifest 一致），同样使用 POSIX 分隔符。
- `varyingDef=null` 表示使用默认 `shaders/include/varying.def.sc`；compute 任务也为 `null`。
- `defines`：保持构建命令中的顺序；C++ 另算 `canonicalDefines`（排序）用于匹配。
- `stage`：`vertex`/`fragment`/`compute`。
- `d3dSkipOptimization`：仅 `backend=dxbc` 生效；`optimization` 记录实际生效值（跳过优化时为 0，否则为 CLI 的 `--optimization`，默认 3）。
- `includeDirs`：`{bgfxSrc}` 由 C++ 的 `--bgfx-src` 或 CMake 默认值替换。
- `passes`：与 `shaders/manifest.json` 的 `passes` 完全一致（backend → pass → 槽位 → 输出路径）；`--execute-plan` 用它重建 manifest；普通构建默认读取 `shaders/variants.json`，`--no-plan` 使用旧硬编码调度。`passes` 中所有 `build/` 路径必须与 `tasks` 的 `(backend, output)` 集合完全一致。
- 任务顺序：保留 `shader_build.py` 的调度顺序（deterministic）；`--plan-check` 按顺序比较。
- 去重：同一 `(backend, output)` 只允许出现一次；生成时校验，重复即失败。
- 数量：当前 950 条（475/后端），与 `shaders/manifest.json` 的唯一输出数一致；新增/删除 shader 后以 `--dump-plan` 为准。

### 4.3 生成/校验流程

1. 开发新 shader 或改变体后：
   ```powershell
   py -3 tools/shader_build.py --dump-plan shaders/variants.json
   ```
2. 提交 `shaders/variants.json`；
3. 本地校验：
   ```powershell
   py -3 tools/shader_build.py --plan-check shaders/variants.json
   ```
4. 从 JSON 编译（普通 Python 构建默认使用；CMake `hbg_shaders` 使用同一模式）：
   ```powershell
   py -3 tools/shader_build.py --execute-plan shaders/variants.json `
     --shaderc <shaderc> --bgfx-src <bgfx-src>
   ```
5. CTest（本地）执行同样的检查；失败信息列出第一处差异（source/output/defines/passes）。

### 4.4 P0 实现步骤

- [x] 在 `tools/shader_build.py` 增加 `CompileTask` 数据类与模块级 `_PLAN`。
- [x] `compile_shader` 在 `_ONLY_FILTER` 判断之前记录 task；`_PLAN_ONLY` 模式不创建 executor、不建输出目录、不调度。
- [x] `--dump-plan <path>`：初始化参数后跑完整变体调度逻辑，写 JSON（稳定缩进 2、UTF-8、末尾换行）；不要求 `--shaderc`/`--bgfx-src`。
- [x] `--plan-check <path>`：同样生成计划，与文件逐项比较；输出差异并返回 1。
- [x] 生成 `shaders/variants.json`（920 条，460/后端；待提交）。
- [x] 在 `tests/CMakeLists.txt` 增加本地 CTest（Python 可用时）：
  `add_test(NAME shader_analysis_plan_check COMMAND ${HBG_PYTHON_EXECUTABLE} ${HBG_PYTHON_ARGS} ${CMAKE_SOURCE_DIR}/tools/shader_build.py --root ${CMAKE_SOURCE_DIR} --plan-check ${CMAKE_SOURCE_DIR}/shaders/variants.json)`。
- [x] 更新 `docs/m11.3-variant-strategy.md:68` 的旧数字（加注“以 variants.json 为准”）。

### 4.5 P0 验收

- [x] `--dump-plan` 输出条数与 manifest 唯一输出数一致：920 条（dxbc 460 / spirv 460）。
- [x] `--plan-check` 通过。
- [x] 用 `--only` 强制重编抽样 `.bin`，SHA-256 与改动前一致（`forward.fs.0.bin`、`deferred_lighting_variant.fs.0.bin`，各 dxbc/spirv）。
- [x] `shaders/manifest.json` 与改动前字节一致（SHA-256 `86BD090D3246087E8FE4FDB6503E5BFFC9FF013D32A5000126F1C4BCC4EE03DD`）。
- [x] `hbg_shaders` 的 Python 构建路径仍通过（直接执行 target 相同命令：920 cached；样例强制重编 4 个 `.bin` 通过且哈希一致）。完整 target 全量重编未执行：工作区 `shaders/include/generated/uniforms.sc` 曾被删除，恢复后 mtime 使 920 条全部失效，与 P0 改动无关。

### 4.6 P0b 实现与验收

- [x] `build_plan()` 写入完整 `passes`；`--plan-check` 同时比较 `tasks` 与 `passes`。
- [x] Python 新增 `--execute-plan <path>`：校验 tasks/passes 后按任务编译，并用 JSON `passes` 写 manifest。
- [x] `compile_shader` 支持按任务使用 `optimization`，不依赖全局 CLI 值。
- [x] CMake `hbg_shaders` 先 `--plan-check`，再 `--execute-plan shaders/variants.json`。
- [x] 当前工作区外部新增 shader 后重新导出为 950 tasks（475/后端），`--plan-check` 通过。
- [x] `--execute-plan --only build/dxbc/forward.vs.bin --no-manifest` 通过；passes/tasks 校验和缓存命中正常。

## 5. C++ 工具设计

### 5.1 目录与命名

遵循 `AGENTS.md`：
- 命名空间 `hbg::analysis`；类型 PascalCase；函数 camelCase；成员 `m_`；常量 `k_`。
- C++20；只用项目已有依赖（nlohmann/json、bx 可选、spirv-cross/spirv-opt 可选）。
- 不引入新的第三方库；Web 依赖仅在前端 `package.json`。

### 5.2 CMake 集成

- `CMakeLists.txt` 新增：
  ```cmake
  option(HBG_BUILD_SHADER_ANALYSIS "Build the shader analysis tool" ON)
  ```
- 在 `tools/shader_analysis/CMakeLists.txt`：
  - `hbg_analysis_core`（STATIC）：数据模型、manifest、过滤、哈希、diff、CSV；
  - `hbg_shader_analysis`（EXECUTABLE）：`main.cpp` + 编译/解析后端；
  - `hbg_shader_analysis_tests`：doctest（`HBG_BUILD_TESTS` 时）。
- 依赖：
  - `nlohmann_json`（`${HBG_NLOHMANN_JSON_TARGET}`）；
  - 若 `TARGET spirv-opt`/`TARGET spirv-cross`：链接并定义 `HBG_ANALYSIS_HAVE_SPIRV_TOOLS=1`；
  - 否则允许运行时回退到外部 `spirv-dis`（`--spirv-dis <path>`），并在报告中记录。
- 传递默认 shaderc 路径：
  ```cmake
  target_compile_definitions(hbg_shader_analysis PRIVATE
      HBG_ANALYSIS_SHADERC_DEFAULT="$<TARGET_FILE:shaderc>")
  ```
  仅在 `TARGET shaderc` 时定义；否则由 `--shaderc` 或环境变量提供。
- 在顶层 `CMakeLists.txt` 的 `add_subdirectory(tools/dawnbake)` 附近加入：
  ```cmake
  if(HBG_BUILD_SHADER_ANALYSIS)
      add_subdirectory(tools/shader_analysis)
  endif()
  ```
- 不把分析工具挂到 `hbg_shaders` 或 `hbg` 的依赖上（保持构建独立）。

### 5.3 CLI

```
hbg_shader_analysis build
  --root <repo> --variants <shaders/variants.json>
  --shaderc <path> --bgfx-src <path> --spirv-dis <path> --spirv-cross <path>
  --backend dxbc|spirv|all
  --pass <filter> --stage vertex|fragment|compute --variant-filter <expr>
  --define NAME[=VALUE] --jobs 8 --timeout 300 --retries 0 --fail-fast
  --force --debug-info --keep-temp --progress plain|json|none
  --out shader_analysis/reports

hbg_shader_analysis diff <currentReportsDir> <referenceReportsDir>
  --out <diff.json> [--fail-on-regression] [--ignore-timestamps]

hbg_shader_analysis export --report <dir|file> --format csv --out <file.csv>
hbg_shader_analysis set-reference --from <reportsDir> --to shader_analysis/reference
hbg_shader_analysis list --report <dir> [--backend ...] [--json]
hbg_shader_analysis validate --report <dir|file>
hbg_shader_analysis --self-test
```

行为：
- `build` 默认过滤掉 `status=failed` 不中止；退出码：0 成功/部分失败但未 `--fail-fast`，1 配置错误或 `--fail-fast` 失败，2 参数错误。
- `--progress=json` 逐行输出 `{event,source,backend,stage,status,index,total}`，供脚本消费。
- `--dry-run`：只打印将执行的命令与缓存命中，不编译。
- `--debug-info`：SPIR-V 加 `--debug`；DXBC 无可用行号映射，忽略并警告。
- `--keep-temp`：保留中间文件；默认缓存目录保留（可 `--no-cache` 临时关闭缓存）。

### 5.4 报告 JSON

#### 5.4.1 摘要（每个 source+stage+backend 一个）

路径：`shader_analysis/reports/<backend>/<sourcePathWithoutExt>.json`（如 `dxbc/passes/forward.fs.json`）。

```json
{
  "schemaVersion": 1,
  "toolVersion": "0.1.0",
  "generatedAt": "2026-11-23T12:34:56Z",
  "backend": "dxbc",
  "profile": "s_5_0",
  "optimization": 3,
  "debugInfo": false,
  "shaderc": { "path": "...", "version": "..." },
  "source": {
    "path": "shaders/passes/forward.fs.sc",
    "sha256": "...",
    "language": "bgfx-sc"
  },
  "stage": "fragment",
  "variantCount": 16,
  "variants": [
    {
      "variantId": "3f2a9c1b7d40",
      "name": "forward.fs.0",
      "defines": ["HAS_NORMAL_MAP=1", "HAS_MASK_MAP=1"],
      "canonicalDefines": ["HAS_MASK_MAP=1", "HAS_NORMAL_MAP=1"],
      "keywords": ["HAS_MASK_MAP", "HAS_NORMAL_MAP"],
      "status": "ok",
      "cacheHit": true,
      "compileMs": 0,
      "binaryBytes": 4567,
      "binaryPath": "shader_analysis/cache/dxbc/passes/forward.fs/3f2a9c1b7d40.bin",
      "disasmPath": "shader_analysis/cache/dxbc/passes/forward.fs/3f2a9c1b7d40.bin.disasm",
      "detailPath": "shader_analysis/reports/dxbc/passes/forward.fs/3f2a9c1b7d40.json",
      "metrics": { "...": "见 5.7" },
      "logs": { "warnings": [], "errors": [] }
    }
  ]
}
```

#### 5.4.2 明细（每个 variant 一个，懒加载）

```json
{
  "schemaVersion": 1,
  "source": "shaders/passes/forward.fs.sc",
  "stage": "fragment",
  "backend": "dxbc",
  "variantId": "3f2a9c1b7d40",
  "instructions": [
    {
      "index": 0,
      "opcode": "mov",
      "text": "mov r0.xy, v0.xy",
      "reads": [{ "class": "v", "index": 0, "mask": "xy" }],
      "writes": [{ "class": "r", "index": 0, "mask": "xy" }],
      "resourceRefs": [],
      "cbufferRead": null,
      "pressure": { "temp": 1 },
      "branch": { "kind": "none", "targetIndex": null, "indent": 0 },
      "sourceLine": null
    }
  ],
  "liveness": [
    { "class": "r", "index": 0, "start": 0, "end": 12 }
  ],
  "resources": [
    { "kind": "texture2d", "name": "s_albedo", "binding": 0, "size": null }
  ],
  "cbufferReads": [
    { "instruction": 42, "slot": 0, "offset": 16, "dwords": 4, "dynamic": false }
  ]
}
```

约定：
- 数组顺序固定：instructions 按反汇编顺序；liveness 按 `(class,index,start)` 排序；resources 按 binding 排序；cbufferReads 按 instruction 顺序。
- `variantId` = SHA-256(`sourceRelativePath + "\n" + stage + "\n" + backend + "\n" + canonicalDefines.join("\n")`) 前 12 个十六进制字符。
- `generatedAt` 只写摘要；diff 默认忽略。
- `null` 表示不适用（如 DXBC 的 SSA 字段），不要用 0 冒充。
- DXBC `CbufferRead.offset` 使用字节偏移：`cbN[i]` 按 `i * 16` 字节，再按 swizzle 首分量加 `component * 4`；`dwords` 等于本次读取分量数（无 swizzle 为 4）。

#### 5.4.3 索引

`shader_analysis/reports/index.json`：
```json
{
  "schemaVersion": 1,
  "generatedAt": "2026-11-23T12:34:56Z",
  "entries": [
    {
      "backend": "dxbc",
      "source": "shaders/passes/forward.fs.sc",
      "stage": "fragment",
      "summaryPath": "dxbc/passes/forward.fs.json",
      "variantCount": 16,
      "okCount": 16,
      "failedCount": 0,
      "sourceSha256": "...",
      "generatedAt": "2026-11-23T12:34:56Z"
    }
  ]
}
```
entries 按 `(backend, source, stage, summaryPath)` 排序。索引采用**整树重建**：每次 build 完成后递归扫描 `<backend>/**` 下摘要 JSON（跳过变体明细），保留未参与本次 build 的 program entry；不再维护单独的增量 state。

### 5.5 DXBC 后端

**输入**：shaderc `--disasm` 生成的 `<output>.disasm`（`shaderc_hlsl.cpp:858-875`）。D3DDisassemble 输出为纯汇编文本，默认无源码行号。

**解析步骤**：
1. 切行；忽略 `//` 注释、空行、`Resource Bindings`/`Input signature`/`Output signature` 等头部区段（或仅从中提取资源声明）。
2. 识别指令行：首 token 为 opcode（可带 `_sat` 等后缀，或前缀 `(p0)` 谓词）。
3. 识别标签：`label_N:`（以及 `switch` 的 `case`/`default` 结构）。
4. 解析操作数：
   - 寄存器类：`r#`（temp）、`v#`（input）、`o#`（output）、`x#`（indexable temp）、`cb#`（constant buffer）、`t#`（texture）、`s#`（sampler）、`u#`（UAV）、`g#`（global，少见）；
   - 掩码/混合：`.xyzw`；无掩码视为全 4 分量（读取计数时按实际分量数）；
   - 修饰：`-`、`_abs`、`_sat`、`1-r0` 等；解析出基础寄存器与掩码即可；
   - 立即数：`l(0,1,2,3)`、`0x...`、`1.0f` 等，不产生读写；
   - cbuffer 索引：`cb0[12]`、`cb0[r1.x + 4]`、`cb0[r1.x*4 + 8]`；静态偏移记录 `offset`，含寄存器索引记录 `dynamic=true`。
5. 读写归属：
   - 目标操作数 → writes（`mov`/`add`/`sample`/`ld`/`ftoi` 等）；
   - 源操作数 → reads；
   - `dcl_*` 只建立资源/输入输出声明，不产生数据读写；
   - `sample*` 的 `t#`/`s#` 归入 `resourceRefs`，不进入 temp liveness；
   - `ld`/`ld_structured`/`store_*` 同理。
6. 分支/结构：
   - `if_nz`/`if_z`/`else`/`endif`/`loop`/`endloop`/`break`/`breakc`/`continue`/`switch`/`case`/`default`/`endswitch`/`call`/`callc`/`ret`/`retc`/`discard`。
   - `indent`：`if/loop/switch` 入栈 +1，`else/case/default` 同级，`endif/endloop/endswitch` 出栈；`branch.kind` 记录类型；可解析的显式 `label_N` 记录 target index；`break/continue` 的目标在结构化语法中缺失，`targetIndex=null`（UI 用范围高亮而不是画线）。
7. 指标：见 5.7。

**生命周期**：移植原 Viewer 的线性反向扫描（`ShaderAnalyzer.cs:195-232`）：
- 对 `r#`、`x#` 做 def-use；
- 从最后一条指令向前，`lastRead` 保存每个寄存器的“最后一次读”指令索引；
- 遇到 write 且 `lastRead` 有值 → 区间 `[writeIndex, readIndex]`；
- 遇到 read 且 `lastRead` 无值 → 记录当前索引；
- 结束后仍留在 `lastRead` 的（只读、从未写）→ 区间 `[0, readIndex]`；
- 展开为每条指令的 alive 集合；`pressure.temp` = 当前存活 temp 数；
- `tempRegisterPeak` = 所有指令 pressure 的最大值；`tempRegisterMax` = `dcl_temps` 声明值或实际最大索引。
- 明确标注这是线性扫描估计，不处理循环回边（与 Unity Viewer 相同）。
- `branchCount` 统计包含结构标记在内的控制流行（`if`/`else`/`endif`/`loop`/`endloop`/`switch`/`case`/`default`/`endswitch`/`break*`/`continue*`/`call*`/`ret*`/`discard*`）；`loopCount` 只统计 `loop`。

### 5.6 SPIR-V 后端

**步骤 1：提取原始 SPIR-V**

- 输入：shaderc 产出的 bgfx shader binary（`-o` 文件）。
- 主算法：在文件中搜索 SPIR-V magic（小端字节 `03 02 23 07`），其前 4 字节应为 `shaderSize`（字节）；校验 `offset + 4 <= fileSize` 且 `shaderSize == (fileSize - offset - 4) - 尾部结构`（尾部有 `u8 0 + u8 numAttr + numAttr*2 + u16`；至少校验 `shaderSize` 不超过剩余长度且是 4 的倍数）。
- 兜底：按 `shaderc_spirv.cpp:355-400, 906-916` 解析完整头部（chunk magic + hash + RawBindings + uniform 表），再取 `shaderSize` 与 words。
- 单测必须用真实 `.bin` 验证偏移，不只手写假数据。

**步骤 2：反汇编与反射**

- 优先：链接 bgfx 已构建的 `spirv-opt`，调用 `spvBinaryToText`（`spirv-opt.cmake:148-160`，含 `disassemble.cpp`）；用 `spirv-cross`（`spirv-cross.cmake:47-60`）做资源反射。
- 回退：调用外部 `spirv-dis`（`--spirv-dis`），反射仍可用 spirv-cross；若两者都没有，`build` 报错并提示安装 Vulkan SDK 或启用 `HBG_BUILD_SHADER_TOOLS`。
- 反射字段：`get_shader_resources()` 的 `separate_images`、`separate_samplers`、`uniform_buffers`、`storage_buffers`、`push_constant_buffers`；`get_decoration(id, DecorationBinding/DescriptorSet)`；`get_declared_struct_size`。
- 指令解析：从反汇编文本提取 `%id = OpXxx ...`、`OpStore`、`OpBranch`、`OpLabel` 等；result id/type id/操作数；对 `OpVariable` 建立资源 id → 资源表映射。
- CBuffer 读取：识别 `OpAccessChain` 根为 uniform block 的指针链，结合 `OpLoad` 与反射的成员偏移；常量索引 → `offset`，寄存器/非常量索引 → `dynamic=true`；`dwords` 由结果类型宽度推断（vec4=4，scalar=1，依此类推）。
- 生命周期：SSA result id 的 `[defIndex, lastUseIndex]` 线性区间（与 Unity 的线性语义一致）；资源 `OpVariable` 从首次使用到最后使用；`maxLiveSsaValues` = 每指令活跃 id 数最大值。Liveness 区间 `class` 为 `ssa`/`resource`；`pressure.ssa` 只统计 SSA。
- 分支：`OpLabel`/`OpBranch`/`OpBranchConditional`/`OpSwitch`/`OpLoopMerge`/`OpSelectionMerge`/`OpReturn`/`OpKill`；`indent` 依据 `OpSelectionMerge`/`OpLoopMerge` 结构；`targetIndex` 指向 `OpLabel`。
- 明细 `instructions` 保留完整反汇编（含声明/debug 元数据）；`instructionCount`/`opcodeHistogram`/load/store/branch 指标只统计函数体，且排除 `NonSemantic.Shader.DebugInfo.100` 的 debug 指令。
- `--debug-info`：解析 `OpLine`/`OpSource`，填 `sourceLine`；报告中 `debugInfo=true`，并注明优化器已关闭（`shaderc_spirv.cpp:758-761`），不能与发布产物直接比较。

### 5.7 指标定义

**通用（所有后端）**：`instructionCount`、`opcodeHistogram`、`binaryBytes`、`compileMs`、`warningCount`、`errorCount`、`cacheHit`。

**DXBC**：
- `tempRegisterMax`、`tempRegisterPeak`
- `inputCount`、`outputCount`
- `cbufferSlotCount`、`cbufferDwordsRead`、`dynamicCBufferReadCount`
- `textureCount`、`samplerCount`、`uavCount`、`structuredBufferCount`
- `sampleCount`、`loadCount`、`storeCount`、`branchCount`、`loopCount`
- `microCodeSize`：用 `binaryBytes` 表达（原字段名保留映射说明）
- 明确 `null`：`SGPRCount`、`UserSGPRCount`、`LDSSize`、`threadGroupWaves`、`SIMDOccupancy*`、`CUOccupancy*`

**SPIR-V**：
- `ssaValueCount`、`maxLiveSsaValues`
- `imageCount`、`samplerCount`、`uniformBufferCount`、`storageBufferCount`、`pushConstantCount`
- `uniformBufferBytes`、`pushConstantBytes`
- `accessChainCount`、`loadCount`、`storeCount`、`branchCount`
- `sourceLineCount`、`debugInfo`
- 明确 `null`：DXBC 的 temp 压力/occupancy 等

> 所有指标都是**静态估算**；`tempRegisterPeak`/`maxLiveSsaValues` 来自线性扫描，不是编译器分配结果。

### 5.8 Diff 与 CSV

**Diff JSON**：
```json
{
  "schemaVersion": 1,
  "current": { "root": "...", "generatedAt": "..." },
  "reference": { "root": "...", "generatedAt": "..." },
  "entries": [
    {
      "backend": "dxbc",
      "source": "shaders/passes/forward.fs.sc",
      "stage": "fragment",
      "variantId": "...",
      "canonicalDefines": ["..."],
      "metricDeltas": { "instructionCount": 12, "tempRegisterPeak": 2, "binaryBytes": -128 },
      "status": "changed"
    }
  ],
  "addedVariants": [],
  "removedVariants": [],
  "sourceHashChanged": false,
  "toolchainChanged": false
}
```
- `status`: `same`/`changed`/`added`/`removed`/`incomparable`（source hash 或 toolchain 不同）；`entries` 只含两侧都存在的 variant，added/removed 单独列出。
- 只对两侧都 `ok` 的变体输出 metric delta；其余标记 `incomparable`。`compileMs`、`cacheHit`、`generatedAt` 不参与指标比较；`opcodeHistogram`/`debugInfo` 变化会使 status 变为 `changed`。
- `--ignore-timestamps` 省略（写 null）两侧 `current/reference.generatedAt`；`--fail-on-regression` 对 binaryBytes/instructionCount/tempRegisterMax/tempRegisterPeak/ssaValueCount/maxLiveSsaValues/cbufferDwordsRead 的正 delta 返回非零。

**CSV 列（v1）**：
`Source;Stage;Backend;VariantId;Defines;Status;CompileMs;BinaryBytes;InstructionCount;TempRegisterMax;TempRegisterPeak;SsaValueCount;MaxLiveSsaValues;TextureCount;SamplerCount;UavCount;UniformBufferCount;UniformBufferBytes;StorageBufferCount;CbufferDwordsRead;DynamicCBufferReadCount;WarningCount;ErrorCount`
- 方括号/引号规则按原 CSV：所有 header 和字符串字段用双引号包裹，内部 `"` 写成 `""`，每个字段以 `;` 结尾，行以 CRLF 结束；
- Defines 按 canonicalDefines 以 CRLF 连接；空指标留空；
- 宿主专用字段不输出（不伪造 0），CSV 为 UTF-8 无 BOM。

### 5.9 缓存、并发与确定性

- 缓存目录：`shader_analysis/cache/<backend>/<sourcePathWithoutExt>/<variantId>/`；SPIR-V debug 构建使用 `<variantId>-debug/` 隔离发布缓存；
  - `output.bin`、`output.bin.disasm`（DXBC）、`spirv.bin`（提取出的原始 SPIR-V）、`meta.json`；
- key = SHA-256(`shaderc 版本 + backend/profile/opt/debug + defines(有序) + 所有依赖文件内容`)；依赖由 `shaderc --depends` 获取；若失败，退回“源文件 + include 目录内所有文件内容”的保守 hash（开销大但正确）。
- `--force` 忽略缓存；`--no-cache` 不读也不写。
- 并发：`--jobs` 默认 8；进程级并发，不使用线程内编译。
- 超时：`--timeout` 默认 300s；超时 kill 并标记失败。
- 确定性：摘要 variants 按 `variantId` 排序；JSON 字段顺序固定；`generatedAt` 可被 `--deterministic` 省略。

### 5.10 C++ 测试

放进 `tools/shader_analysis/tests`，doctest；不链接 `hbg_app`，避免拖慢。测试文件与用例见第 7 节。

## 6. Web 编辑器设计

### 6.1 技术栈与目录

```
tools/shader_analysis/web/
  package.json          # react, react-dom, react-router-dom, @tanstack/react-virtual, prismjs, diff
  vite.config.ts        # proxy /shader_analysis /shaders -> localhost:8787
  index.html
  src/
    main.tsx
    App.tsx
    api.ts              # fetch + 类型
    types.ts            # 与 JSON schema 对齐
    pages/
      IndexPage.tsx
      ProgramPage.tsx
      VariantPage.tsx
      DiffPage.tsx
    components/
      AssemblyList.tsx
      RegisterTimeline.tsx   # Canvas
      CBufferList.tsx
      SourceView.tsx
      MetricsTable.tsx
      DiffTable.tsx
      FilterBar.tsx
    utils/
      canonical.ts
      diff.ts
      format.ts
```

- 不引入图表库；寄存器图用 Canvas。
- 语法高亮：`prismjs` + 自定义 HLSL/bgfx `.sc` 关键字；汇编用轻量分词器（不依赖 Monaco）。
- 虚拟滚动：`@tanstack/react-virtual`。
- 状态：React hooks + URL query；无需 Redux。

### 6.2 页面

1. `IndexPage`：读取 `reports/index.json`；表/卡片展示 backend、source、stage、variant 数、ok/failed、时间；搜索与 backend 过滤；点击进入 Program。
2. `ProgramPage`：读取摘要 JSON；变体表（defines、status、指令数、temp/SSA 峰值、资源数、编译耗时）；列排序/过滤；reference 选择；CSV 下载；点击进入 Variant。
3. `VariantPage`：读取明细 JSON；
   - Overview：指标卡片、defines、命令/日志、资源表；
   - Assembly：虚拟列表；opcode/操作数着色；branch 缩进；cbuffer 注释；源码行跳转（若有）；
   - Registers：Canvas 生命周期条带 + 读写条 + 压力条；点击依赖链高亮；Escape 清除；确定性颜色；
   - CBuffer：按 offset 顺序；乱序标红；动态 offset 蓝色；source-mapped 读取可跳源码行；
   - Source：源码高亮 + 行号；若 `sourceLine` 存在则联动。
4. `DiffPage`：加载当前/参考摘要 + diff JSON；指标 delta 表（回归红/优化绿）；新增/删除变体；单变体汇编 diff（`diff` 包）。
5. 全局：variant detail / report summary JSON 拖拽/选择上传（浏览器本地解析，index 暂不支持），深链接（`?backend=&source=&variant=`），复制路径，打开源码（`vscode://file/...` 或复制）。

### 6.3 从 Unity Viewer 对齐的交互

| Unity Viewer 行为 | Web 实现 |
|---|---|
| VGPR code/assembly 两个 Tab | Registers（DXBC temp / SPIR-V SSA）+ Assembly |
| SGPR code/assembly 两个 Tab | Resources/Constants（DXBC cbuffer / SPIR-V uniform）；不再有 SGPR 压力 |
| CBuffer read order Tab | CBuffer Tab，保留 offset 乱序检测 |
| 汇编行寄存器图（alive/read/write） | Assembly 行选中后 Registers 页聚焦该指令，显示游标、live lane 与读写计数 |
| 生命周期条带（颜色随 alive 时长） | Canvas 列条带，颜色按存活长度为蓝→洋红渐变 |
| 读/写条（蓝/红） | 同色语义 |
| 占用率色块 + tooltip | 压力色条 + tooltip（估算值）；RegisterTimeline 顶部压力曲线也可 hover 查看 pressure |
| 点击寄存器高亮依赖链 | 同语义：dim 全部，沿 read→write 链恢复 |
| Escape 清除高亮 | 同 |
| 跳转竖线 + hover 高亮区间 | Assembly 中按 `branch` 元数据绘制；颜色由固定 seed 生成 |
| 代码搜索高亮 | Assembly/Source 搜索框；Assembly 查询/过滤状态保留在 URL（`asmq`/`asmfilter`），切换 tab 不丢 |
| 固定随机种子 42 | 跳转颜色使用 seed 42 的确定性 hash |

**明确不复刻**：PS4/PS5 occupancy 表（`ShaderViewerWindow.cs:542-617`）、GCN 寄存器类型（`ShaderAnalyzer.cs:13-21`）、`ShaderUtil`/`AssetDatabase` 相关逻辑。

### 6.4 本地服务 `serve.mjs`

- 默认端口 8787；`--root .`、`--port 8787`。
- 路由：
  - `/web/*` → `tools/shader_analysis/web/dist/*`（若已构建）；未构建时返回提示；
  - `/shader_analysis/*` → 仓库内报告/缓存（只读）；
  - `/shaders/*` → 源码与 include（只读）；
  - `/` → 重定向到 `/web/`。
- 安全：解析路径后必须仍在 root 下；拒绝 `..`；只处理 GET/HEAD；设置正确 MIME（`.json`, `.js`, `.css`, `.sc`, `.bin`, `.disasm`, `.md`）。
- Vite dev：`proxy` 将 `/shader_analysis`、`/shaders` 转发到 8787。

### 6.5 本地命令

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug --target hbg_shader_analysis
.\build\vs2022\Debug\hbg_shader_analysis.exe build --backend all --pass "forward,deferred_lighting*" --jobs 8
node tools/shader_analysis/serve.mjs
# 或开发模式
cd tools/shader_analysis/web
npm install
npm run dev
```

## 7. 分阶段实施清单

> 勾选规则：每项完成后更新为 `[x]`，并在本文件末尾“实施记录”追加日期、命令、结果摘要。未通过验收不得进入下一阶段（P0 必须先完成）。

### 7.1 P0 共享变体清单

- [x] 修改 `tools/shader_build.py`：`CompileTask`、`_PLAN`、`--dump-plan`、`--plan-check`、`_PLAN_ONLY`。
- [x] 生成 `shaders/variants.json`（920 条，460/后端；待提交）。
- [x] 增加 CTest `shader_analysis_plan_check`（Python 可用时）。
- [x] 验证 4.5 全部验收项（完整 `hbg_shaders` 全量重编的例外见 4.5）。
- [x] 更新 `docs/m11.3-variant-strategy.md` 的旧数字注释。

**验收**：4.5。

### 7.2 P1 C++ 骨架与数据模型

- [x] `tools/shader_analysis/CMakeLists.txt`；顶层 CMake 选项/子目录。
- [x] `variant_manifest.h/.cpp`：加载/校验 variants.json；`{bgfxSrc}` 替换；重复输出/变体检测；`--dry-run` 命令打印。
- [x] `report.h/.cpp`：摘要/明细/索引结构；JSON 序列化/反序列化；schema 版本。
- [x] `filters.h/.cpp`：移植 `ShaderProgramFilter.Parse` 语义（2.5），并支持 `*`/`?`。
- [x] `hashing`：SHA-256 + 短 variantId；`canonicalDefines` 排序去重。
- [x] `main.cpp`：`build --dry-run`、`list`、`validate`、`--self-test`。
- [x] `tests/test_variant_manifest.cpp`、`test_filters.cpp`、`test_report_json.cpp`、`test_hashing.cpp`。
- [x] CMake 测试 target 并接入 `ctest`。

**验收**：
```powershell
.\build\vs2022\Debug\hbg_shader_analysis.exe build --dry-run --backend dxbc --pass forward
ctest --test-dir build/vs2022 -C Debug -R shader_analysis --output-on-failure
```
- dry-run 打印的命令与 `tools/shader_build.py` 对同一 task 的命令一致（逐字段比较）。
- JSON roundtrip 一致；过滤边界用例通过。

P1 验收结果（2026-11-23）：`hbg_shader_analysis --self-test` 输出 920 tasks；`build --dry-run --backend dxbc --pass forward` 输出 33 条命令；`forward.fs.0` 命令与 Python 逐字段比较一致；`list`/`validate` 对合成 report 目录通过；`ctest -R shader_analysis` 4/4 通过（22 cases / 1121 assertions）。

### 7.3 P2 编译调度、DXBC 反汇编、SPIR-V 提取

- [x] `process_runner`：CreateProcess（Windows）、stdout/stderr、超时、取消。
- [x] `compiler`：构造 shaderc 命令（对齐 `shader_build.py:74-93`）；`--depends`；缓存 key/meta；`--jobs` 并发。
- [x] DXBC：`--disasm`，读取 `<out>.disasm`，校验存在与大小。
- [x] SPIR-V：`spirv_blob` 提取原始 SPIR-V（magic 前 `shaderSize` 校验 + 完整头部兜底）。
- [x] 错误日志：按 shaderc stderr 解析为结构化 warnings/errors（保留原始文本）。
- [x] `tests/test_process_runner.cpp`（用假编译器脚本/小 exe）、`test_spirv_blob.cpp`（真实 fixture）。
- [x] 用 `--pass forward --backend all` 产出 `.bin`/`.disasm`/原始 SPIR-V。

**验收**：
- `build --pass forward --backend dxbc` 生成摘要中的 `binaryBytes`、`disasmPath`；
- `build --pass forward --backend spirv` 提取的 SPIR-V 以 `03 02 23 07` 开头，大小与 blob 中 `shaderSize` 一致；
- 缓存二次运行 `cacheHit=true` 且 metrics 一致。

P2 验收结果（2026-11-23）：`forward.vs` 单变体的 dxbc/spirv 编译、摘要、缓存均通过；`build --pass forward --backend all --jobs 8` 最终 66/66 成功（55 cache hit，4 个摘要），33 个 dxbc `.disasm`、32 个 forward.fs SPIR-V `spirv.bin` 均通过 magic/`shaderSize` 校验；缓存二次运行 `cacheHit=true`，`binaryBytes`/`microCodeSize` 一致。未优化的 dxbc 变体单次约 2–4 分钟，`--force` 全量 forward 可能超过 15 分钟，验收使用缓存完成；P3 应优先复用缓存并控制 `--jobs`。

### 7.4 P3 DXBC 分析

- [x] `dxbc_parser`：指令/操作数/资源声明/cbuffer/分支。
- [x] `liveness`：移植 Unity 线性反向扫描（2.8/5.5），输出区间与每指令压力。
- [x] 指标与明细 JSON 落盘。
- [x] `tests/test_dxbc_parser.cpp`、`test_dxbc_liveness.cpp`，fixtures 来自真实 `forward.fs.*.disasm` + 手写边界用例。
- [x] 与 Unity 原算法对照：用同样的合成序列（read-before-write、覆盖写、只读输入）比较区间结果。

**验收**：
- 对 `forward.fs.0` 报告 instructionCount > 0、tempRegisterPeak > 0；
- 合成用例的生命周期区间与 Unity 算法一致（单测断言具体区间）；
- 明细 JSON 通过 schema 校验。

P3 验收结果（2026-11-23）：`forward.fs.0`（`a4123cb86044`）instructionCount=26948、tempRegisterMax=58、tempRegisterPeak=48、inputCount=4、outputCount=1、textureCount=12、samplerCount=9、sampleCount=119、loadCount=264、cbufferDwordsRead=581、loopCount=127；detail 为 26948 instructions / 21086 liveness intervals / 22 resources / 302 cbufferReads，`validate --report ...a4123cb86044.json` 通过。`--pass forward.fs --backend dxbc --jobs 8` 的 32 个变体全部 ok 且命中缓存并生成 summary/detail；detail 体积约 17.8 MB/变体，Debug 全量生成耗时较高。`forward.vs` 真实 fixture 为 32 instructions、tempRegisterMax=3、tempRegisterPeak=3、cbufferDwordsRead=64。合成 read-before-write/覆盖写/只读输入/x 寄存器用例的区间与 Unity 反向扫描一致。`shader_analysis_tests` 44 cases / 1327 assertions，`ctest -R shader_analysis` 4/4。

### 7.5 P4 SPIR-V 分析

- [x] `spirv_parser`：接入外部 `spirv-dis`；spirv-cross `--reflect` 反射；`--spirv-dis`/`--spirv-cross` 可显式指定，默认搜索 PATH、`VULKAN_SDK` 与 `C:\VulkanSDK`。
- [x] SSA 生命周期、资源/cbuffer 读取（`OpAccessChain`+`OpLoad`）、分支/缩进。
- [x] `--debug-info`：解析 `OpLine` 与 glslang 的 `NonSemantic.Shader.DebugInfo.100` `DebugLine`，记录 `debugInfo=true` 与限制说明；debug 缓存目录使用 `<variantId>-debug`。
- [x] `tests/test_spirv_parser.cpp`（真实 `forward.vs` fixture + 合成反汇编文本 + 反射 JSON）。
- [x] 指标字段与 DXBC 对齐（通用字段同名）。

**验收**：
- `forward.fs.0` 的 SPIR-V 报告资源数量与 spirv-cross 反射一致；
- SSA 区间/最大活跃值可复现；
- `--debug-info` 报告含 `sourceLine`，且摘要标注 `debugInfo=true`。

P4 验收结果（2026-11-23）：`forward.fs.0`（`b3863e24fd91`）instructionCount=21387、ssaValueCount=16254、maxLiveSsaValues=123、imageCount=12、samplerCount=9、uniformBufferCount=1、storageBufferCount=1、uniformBufferBytes=864、accessChainCount=323、loadCount=354、storeCount=33、branchCount=2096、loopCount=127、cbufferDwordsRead=408；detail 为 21834 instructions / 16277 liveness / 23 resources / 147 cbufferReads，约 14.6 MB。32 个 forward.fs 变体的资源分类计数逐个与 `spirv-cross --reflect` 一致（0 mismatch），detail 总量约 467 MB。debug 验收 `forward.vs` 的 sourceLineCount=12、detail 中 10 条指令带 `sourceLine`（835–…），summary `debugInfo=true`；debug 缓存与明细使用 `<variantId>-debug` 与发布结果隔离。`shader_analysis_tests` 47 cases / 1397 assertions，`ctest -R shader_analysis` 4/4。注意 `spirv-dis` 缺失时该变体标记 analysis failed；`spirv-cross` 缺失时回退到反汇编资源声明。

P5 验收结果（2026-11-23）：`report_files` 内置整树索引重建；每次 build 写完全部摘要后扫描 `<reports>/<backend>/**/*.json`（跳过 12 位十六进制变体明细及 `-debug` 明细），按 `(backend, source, stage, summaryPath)` 排序写 `index.json`。`diff` 按 `source+stage+backend` 匹配 program、按 canonicalDefines 的 SHA-256 短 hash 匹配 variant；数值 metrics 与 `binaryBytes` 输出 delta，`compileMs`/`cacheHit`/时间戳不参与比较；source hash 或 shaderc/tool/profile/optimization/debug 指纹变化标记 `incomparable`，并输出 `addedVariants`/`removedVariants`。`--ignore-timestamps` 将两侧根 `generatedAt` 写为 null；`--fail-on-regression` 对 binaryBytes/instructionCount/tempRegisterMax/tempRegisterPeak/ssaValueCount/maxLiveSsaValues/cbufferDwordsRead 的正 delta 返回 1。`set-reference` 只复制摘要（清除本地 binary/disasm/detail 路径）并重建目标 index，拒绝源/目标重叠，目标目录会先清空。CSV 固定 23 列，字符串双引号包裹并转义内部双引号，字段以 `;` 结尾、行以 CRLF 结束，空值留空。CLI 已支持 `diff <current> <reference> --out <file>`、`export --report <dir|file> --format csv --out <file>`、`set-reference --from <dir> [--to <dir>]`。reference 基线已生成到 `shader_analysis/reference`（forward.fs dxbc/spirv 各 32 变体摘要）。真实报告验证：P4 forward.fs spirv 对 reference `32 matched / 32 same / 32 removed(dxbc)`；同目录 diff 为 all same；修改 instructionCount 后出现 delta 且 `--fail-on-regression` 返回 1。`shader_analysis_tests` 56 cases / 1508 assertions，`ctest -R shader_analysis` 4/4。

### 7.6 P5 报告索引、Diff、CSV、Reference

- [x] `index.json` 生成与增量更新（选择**整树重建**：每次 build 扫描 reports 下全部摘要并重写 index，保留未重建 program 的 entry）。
- [x] `diff`：匹配键、metric delta、added/removed、指纹警告；`--ignore-timestamps`。
- [x] `export --format csv`：5.8 列；分号/引号规则。
- [x] `set-reference`：复制摘要（默认不复制明细/原始汇编）；目录布局与 reports 相同。
- [x] `tests/test_diff.cpp`、`test_csv.cpp`、`test_reference.cpp`。
- [x] 生成一份 reference 基线（forward.fs 的 dxbc/spirv 摘要；提交摘要）。

**验收**：
- 相同报告 diff 输出无变化；改一个 defines/指标可看到 delta；
- CSV 用 Excel 打开列正确、无乱码；
- reference 可在 `diff` 和 Web 中使用。

### 7.7 P6 Web 基础

- [x] `web/` Vite + React + TS 初始化；`api.ts`/`types.ts`。
- [x] `IndexPage`、`ProgramPage`、`VariantPage` 的 Overview + Assembly。
- [x] `serve.mjs`（只读、路径安全、MIME、Vite proxy）。
- [x] 虚拟滚动、搜索、深链接、CSV 下载。
- [x] 用真实报告手工验收（D3D11 和 Vulkan 各一个 pass）。

**验收**：
- 启动 `serve.mjs` 后浏览器可浏览索引 → Program → Variant；
- Assembly 1 万行以上不卡顿（虚拟化生效）；
- 源码链接/路径复制可用。

P6 验收结果（2026-11-23）：`web/` 使用 React 18、Vite 6、TypeScript 和 `@tanstack/react-virtual`；实现 `IndexPage`（搜索/backend 过滤/统计）、`ProgramPage`（变体过滤/列排序/CSV 下载/源码链接）、`VariantPage`（Overview 指标卡片/opcode 直方图/资源/日志 + Assembly）。`serve.mjs` 默认 8787，仅 GET/HEAD，拒绝路径穿越和 POST，支持 `/web` SPA fallback、JSON/JS/CSS/SC/disasm MIME 和 Vite 代理。真实报告：以 `--variant-filter "ALPHA_CLIP&HAS_MASK_MAP&HAS_NORMAL_MAP&DOUBLE_SIDED"` 生成 dxbc/spirv forward.fs 各 2 个变体到 `shader_analysis/reports`；DXBC 变体 Assembly 27,042 条指令、21,186 个 live interval，DOM 仅保留可见虚拟行；CSV 下载与服务端 23 列格式一致。深链接 `?dataset=&summary=&variantId=&tab=assembly` 可用；源码 `/shaders/...` 打开与路径复制可用；Reference 数据集仅摘要可浏览。

### 7.8 P7 Web 寄存器/CBuffer/Source/Diff + 文档

- [x] `RegisterTimeline` Canvas：生命周期条带、读写条、压力条、压力 hover tooltip、依赖链高亮、Escape 清除。
- [x] Assembly：branch 跳转竖线与 hover 区间（DXBC 结构化边界 / SPIR-V target）。
- [x] `CBufferList`：offset 顺序与乱序标红、动态 offset 蓝色。
- [x] `SourceView`：源码高亮、行号、`sourceLine` 联动、行搜索/定位。
- [x] `DiffPage`：指标表、新增/删除、单变体汇编 diff。
- [x] `tools/shader_analysis/README.md`、`docs/shader-analysis.md`（使用说明、指标口径、限制）。
- [x] `.gitignore` 增补（见 8.3）。
- [x] 手工验收清单（第 9 节）全部通过。

**验收**：第 9 节。

P7 验收结果（2026-11-23）：`RegisterTimeline` 使用 Canvas 绘制压力条、生命周期条带和 read/write 标记；DXBC `r` 58 条 lane、SPIR-V `ssa` 按生命周期显示前 72 条，支持 class/寄存器搜索、点击选中依赖链、Escape 清除和双击跳转 Assembly。`CBufferList` 按 slot 检测 offset 顺序，递增绿色、乱序红色、动态蓝色；当前 forward 报告无动态读取，使用临时 detail fixture 验证蓝色路径。`SourceView` 使用 Prism HLSL 高亮和行号，支持源码行搜索/自动定位，Assembly 的 `sourceLine` 可跳转/双击跳转到 Source；当前 forward.vs debug 行号来自预处理源码（835+），超出原始文件时显示明确警告。`DiffPage` 支持指标 delta 红/绿、added/removed 和 `diff` 包逐行汇编对比；使用临时 previous 报告验证 changed + 汇编 diff，真实基线验证 added/removed。深链接、虚拟滚动、serve 安全和 debug 警告均已验证。

## 8. 仓库集成细节

### 8.1 CMake（拟新增，不改现有行为）

- 顶层 `CMakeLists.txt`：在 `HBG_BUILD_DAWNBAKE` 附近增加 `HBG_BUILD_SHADER_ANALYSIS` 与 `add_subdirectory(tools/shader_analysis)`。
- 不修改 `hbg_shaders` 的依赖关系；分析工具独立构建。
- 测试：`tools/shader_analysis/tests` 独立 target；`add_test` 名称统一 `shader_analysis_*`。
- 若 `TARGET shaderc` 存在，传递默认路径；否则允许 `--shaderc`/`HBG_ANALYSIS_SHADERC`。

### 8.2 .gitignore

```gitignore
/shader_analysis/cache/
/shader_analysis/reports/**/[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f].json
/shader_analysis/reports/**/[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-debug.json
/shader_analysis/reports/diff*.json
/shader_analysis/reports/*.csv
/shader_analysis/reports_previous/
/tools/shader_analysis/web/node_modules/
/tools/shader_analysis/web/dist/
```

说明：`reports/<backend>/<source>.json` 摘要可提交；仅 12 位 hash 的变体明细（含 `-debug`）按文件忽略，避免把 `reports/<backend>/<sourcePath>/` 目录下的摘要也误伤；本机 diff/CSV 与临时 previous 报告被忽略。`reference/` 不在忽略范围内（提交基线摘要）。

### 8.3 文档

- `docs/shader-analysis-plan.md`（本文件）：规划、对齐、阶段清单。
- `docs/shader-analysis.md`（P7）：使用说明、schema、指标口径、限制。
- `tools/shader_analysis/README.md`（P7）：本地构建/运行命令。
- [x] 在 `README.md` 的“工具”列表加入 Shader Analysis Viewer 链接（不改变现有结构）。

## 9. 手工验收清单（复刻原 Viewer）

- [x] 索引页显示 dxbc/spirv 两类报告，可按 backend/source 过滤。
- [x] Program 页变体表能按 defines 过滤（`A&B|C` 语义与 Unity 一致）。
- [x] Variant Overview 显示指令数、temp/SSA 峰值、资源、cbuffer 统计、sample/load/store、编译耗时、日志；页头显示 profile、optimization、shaderc 版本。
- [x] Assembly 页：指令顺序、着色、branch 缩进、跳转竖线与 hover 区间（tooltip 含起止指令文本，点击/Enter 选中目标）、cbuffer 注释、source line 文本/越界提示、搜索高亮/过滤（`asmq`/`asmfilter` 深链接），Enter/Shift+Enter/F3 与 Prev/Next 循环命中并显示 `k / n`（`asmhit` 深链接）、可选的 Source context（`asmcontext=1`，每条源码行首组指令显示源码文本）、源码行跳转/双击（有 debug 信息时）；单击行选中指令并在 Registers 页聚焦；每行右侧显示去重 read/write 计数、相对峰值缩放的 live pressure 条，以及可通过 `Lane class` 过滤的最多 16 条 live lane 色条（R/W 计数和寄存器菜单同类过滤）（来自 `liveness`，顶部红=write、底部蓝=read，tooltip 列生命周期区间与首写指令/源码行，hover 高亮该 lane 仍存活的可见 Assembly 行，点击跳 Registers 选中，URL `reg` 选中的 lane 保持色条描边和存活行高亮，依赖链 lane 加橙色描边、无关 lane 变暗并显示相关数，超出显示 `+N`，菜单列出隐藏 lane 并可 hover 高亮/选择）；点击 `R<n>/W<n>` 弹出具体规则寄存器列表，选择后跳 Registers 并选中对应 lane。
- [x] Registers 页：生命周期条带与读写条与数据一致；hover 显示最近写入指令、写入源码行文本与 pressure；顶部压力曲线 hover 显示 pressure/opcode/instruction；点击寄存器高亮依赖链并同步 `reg` 深链接；选中后显示首条写入指令/文本/源码行文本/Source；Escape/Clear 清除选择并移除 `reg`；focus 模式显示指令 opcode/文本、live/read/write 和 Source 行入口；class 下拉写入 `regclass`，sort 下拉可切换生命周期长度/寄存器编号排序（`regsort=index`），超过 72 条时分页（`regpage`），被选中的 lane 不在当前页时固定在首行。
- [x] CBuffer 页：Table/Read order 切换（`cbview=order` 持久化）；Read order 按 source line 分组并显示原风格 read 文案；offset 递增绿色、乱序红色、动态蓝色；source-mapped 读取显示 `L<n>` 并可跳 Source；Source 行显示 `CB<n>`，点击带 `cbline` 切回 CBuffer 并聚焦/高亮该行读取，`Show all` 清除。
- [x] Source 页：单击行写入 `line` 深链接并高亮，双击复制 `path:line`；可按 `Lane class` 过滤 `A<n>`/lane rail/R/W 计数；有 `sourceLine` 映射的行显示 `asm <n>`（单条直达、多条展开列表、Escape 收起）+ 峰值压力 `P<n>`（点击聚焦峰值指令）+ `R<n>/W<n>`（点击聚焦首条指令）+ `A<n>` 与按 class/index 数值排序的 live lane 彩色 rail（hover 高亮 lifetime 源码行，点击选中 Registers lane（超 72 条时固定首行），tooltip 列 lane + 区间，`+N` 菜单可查看/选中前 12 条之外的 lane）+ 行尾按全局最大值缩放的 pressure/alive 双色迷你条；Registers/Assembly 选中的 `reg` lane 在 rail 上加描边并高亮其 lifetime 覆盖的源码行 + `CB<n>`（点击聚焦 CBuffer）并跳 Assembly，hover 提示列出映射指令/分支目标/文件名:行号；映射 branch 在 40px gutter 按 indent 显示 source jump 竖线，hover 高亮源码范围，点击跳目标行；源码搜索 Enter/Shift+Enter/F3 与 Previous/Next 循环命中，提示显示 `k / n`，`qhit` 持久化当前命中序号。
- [x] Upload：任意页面拖入或选择 variant detail / report summary JSON；detail 查看 Assembly/Registers/CBuffer/Source，summary 显示变体指标表；detail 后追加同名 source 文件可供 Source/源码预览离线使用；index 明确拒绝；文件不上传服务器，刷新清空。
- [x] Diff 页：指标 delta 红/绿；新增/删除变体；单变体汇编 diff。
- [x] 深链接可复现页面状态；刷新后仍定位到同一 variant；`cbline` 可复现 CBuffer 源码行聚焦，`cbview=order` 可复现 Read order，`regclass=<class>` 可复现寄存器 class，`reg=<class:index>` 可复现 Registers lane 选中，`regsort=index` 可复现寄存器编号排序，`regpage=<n>` 可复现分页，`laneclass=<class>` 可复现 Assembly/Source lane class 过滤（含 R/W 计数），`asmhit=<n>` 可复现 Assembly 搜索命中位置，`qhit=<n>` 可复现 Source 搜索命中位置。
- [x] 1 万条指令的 Assembly 滚动流畅；寄存器 Canvas 不阻塞 UI；访问过的 Variant/Upload tab 切换后保留滚动位置与展开状态；tab 栏支持 `←`/`→`/`Home`/`End` 与 roving tabindex，面板有 `tabpanel`/`aria-labelledby`。
- [x] `serve.mjs` 拒绝 `..` 路径；只响应 GET/HEAD。
- [x] `--debug-info` 报告在 UI 顶部显示“调试构建、关闭优化”的警告。

说明：当前 forward 报告没有动态 cbuffer 读取，动态蓝色路径使用临时 detail fixture
验证；forward.vs debug 的 `sourceLine` 来自预处理源码，超出原始文件行数时 Source
页显示映射限制警告。两者均不改变真实报告格式。

## 10. 风险、限制与回退

| 风险 | 影响 | 对策 |
|---|---|---|
| `shader_build.py` 重构改变产物 | 构建回归 | P0 只做导出+校验，不改 manifest；`--plan-check` + 抽样哈希 |
| 线性生命周期不精确 | 指标与真实寄存器分配不同 | 沿用 Unity 算法并在 UI/schema 标注“估算”；不宣称硬件占用率 |
| DXBC 无源码行号 | 无法复刻源码↔汇编映射 | v1 不做；Source 与 Assembly 分开；将来可评估 fxc/dxc 调试路径 |
| SPIR-V `--debug` 关闭优化 | 报告与发布产物不可比 | 默认关闭；UI 警告；diff 标记 `debugInfo` 不同为 `incomparable` |
| `spirv-opt`/`spirv-cross` 链接不可用 | 无法内置反汇编 | 回退外部 `spirv-dis`；再失败则 build 报错并提示 |
| 全量 950 变体耗时长 | 本地等待 | 过滤、并行、依赖缓存、quick 预设（forward/deferredLighting/shadow） |
| 报告 JSON 体积大 | Web 加载慢 | 摘要/明细分离、懒加载、虚拟滚动；原始汇编不入 JSON |
| shaderc 版本/优化变化 | 指标漂移 | 报告记录 shaderc 版本/opt/debug；diff 标记 `toolchainChanged` |
| 工作区已有未提交改动 | 实现冲突 | 实现前先 `git status`/`git diff`；只改本文档列出的文件；不改用户已有修改 |
| `docs/m11.3` 与 manifest 数量不一致 | 文档误导 | P0 修正；以 variants.json 实际数量为准 |

## 11. 实施记录（追加）

> 每完成一个阶段，在此追加：日期、阶段、命令、结果、遗留问题。

- 2026-11-23：本文档创建（规划定稿，未开始实现）。
- 2026-11-23：P0 完成。命令：`py -3 tools/shader_build.py --dump-plan shaders/variants.json`、`--plan-check`、`ctest --test-dir build/vs2022 -C Debug -R shader_analysis_plan_check`。结果：920 条（dxbc 460 / spirv 460），`--plan-check` 通过；`shaders/manifest.json` SHA-256 保持 `86BD090D...`；`forward.fs.0`/`deferred_lighting_variant.fs.0` 的 dxbc/spirv 强制重编哈希一致。遗留：工作区 `uniforms.sc` 恢复后 mtime 使全量缓存失效，`hbg_shaders` 完整 target 未全量重编（与 P0 改动无关）；下一步 P1。
- 2026-11-23：P1 完成。新增 `tools/shader_analysis/`（`hbg_analysis_core` + `hbg_shader_analysis` + doctest）与顶层 `HBG_BUILD_SHADER_ANALYSIS`；实现 manifest 校验/选择、`{bgfxSrc}` 替换、`ShaderProgramFilter` 语义、SHA-256/variantId、摘要/明细/索引 JSON、`build --dry-run`/`list`/`validate`/`--self-test`。命令字段对齐 `tools/shader_build.py:120-174`；过滤语义对照 `ShaderProgramFilter.cs:55-78`，短 hash 语义对照 `ShaderBuildReport.cs:483-493`。验证：`--self-test` 920 tasks；`--pass forward --backend dxbc` 33 条；`forward.fs.0` 命令与 Python 逐字段一致；`list`/`validate` 对合成 report 目录通过；`shader_analysis_tests` 22 cases / 1121 assertions；`ctest -R shader_analysis` 4/4。下一步 P2 编译调度与反汇编。
- 2026-11-23：P2 完成。新增 `process_runner`（Windows CreateProcess 管道、stdout/stderr、超时、取消）、`compiler`（临时输出、`--depends`/`--disasm`、依赖/源文件内容缓存 key、meta、重试、fail-fast、进程级并发、摘要落盘）、`spirv_blob`（magic 前 `shaderSize` + 完整 bgfx 头部兜底）、shaderc 诊断解析；`build` 非 `--dry-run` 已可用，新增真实 `forward.vs.bin` fixture 与 fake compiler。命令/缓存对齐 `tools/shader_build.py:120-174`，DXBC 写出对照 `shaderc_hlsl.cpp:843-875`，SPIR-V 布局对照 `shaderc_spirv.cpp:355-400,906-930`。验证：`shader_analysis_tests` 34 cases / 1169 assertions；`ctest -R shader_analysis` 4/4；单变体 dxbc/spirv 首次编译 + 二次缓存命中通过；`--pass forward --backend all --jobs 8` 最终 66/66 ok、55 cache hit、4 摘要，dxbc 33 个 `.disasm`、spirv 32 个 `spirv.bin` 的 magic/`shaderSize` 校验通过。注意 DXBC 未优化变体单次约 2–4 分钟，`--force` 全量 forward 可能超过 15 分钟；`shader_analysis/cache/` 已加入 `.gitignore`。下一步 P3 DXBC 分析。
- 2026-11-23：P3 完成。新增 `dxbc_parser`（D3DDisassemble 头/声明过滤、资源/采样器/UAV/structured buffer/输入输出/temps 解析、操作数 mask、`x#[]`/`cb#[]` 索引、动态 cbuffer、`imul null`、store/atomic 无目标、谓词、结构化分支缩进与 `label_N` target）和 `liveness`（按 `ShaderAnalyzer.cs:195-232` 反向扫描；read-before-write、覆盖写、只读输入、x class；区间按 class/index/start 排序，pressure 按唯一寄存器计数）。`compiler` 对成功的 dxbc 变体解析 `.disasm`，写 `reports/<backend>/<sourceStem>/<variantId>.json` 并填充 summary metrics；缓存命中也会重新生成明细。新增 `test_dxbc_parser.cpp`、`test_dxbc_liveness.cpp`、真实 `forward.vs.disasm` / `forward.fs.sample.disasm` fixtures。命令：`build --pass forward.vs --backend dxbc --force` 和 `build --pass forward.fs --backend dxbc --jobs 8`；验证：44 cases / 1327 assertions，CTest 4/4；`forward.vs` 32 instructions / peak 3；`forward.fs.0` 26948 instructions / peak 48，detail 通过 `validate`；32 个 forward.fs 变体 summary/detail 全量生成。注意 detail 约 17.8 MB/未优化 fragment 变体，Debug 全量较慢；本次验收输出放在 `build/`，默认 `shader_analysis/reports` 的明细忽略策略留 P5/P7 处理。工作区并行新增 compute pass 后，已用 `--dump-plan` 把 `shaders/variants.json` 更新为当前 928 tasks（464/后端）；P0 记录的 920 是当时基线，`forward` 仍为 33 条。下一步 P4 SPIR-V 分析。
- 2026-11-23：P4 完成。新增 `spirv_parser`（外部 `spirv-dis` 反汇编 + `spirv-cross --reflect` 反射；`OpAccessChain`/`OpLoad` 的 UBO 成员偏移与动态索引、SSA `[def,lastUse]`、资源变量 `[firstUse,lastUse]`、`OpSelectionMerge`/`OpLoopMerge` 缩进、`OpBranch*`/`OpSwitch` target、`OpLine` 与 `NonSemantic.Shader.DebugInfo.100` `DebugLine`）和 `--spirv-dis`/`--spirv-cross` 解析；debug 缓存使用 `<variantId>-debug`。命令：`build --pass forward.fs --backend spirv --jobs 4 --out build/p4_forward_spirv`、`build --pass forward.vs --backend spirv --debug-info --force --out build/p4_debug`；`forward.fs.0` 资源 12 image/9 sampler/1 UBO/1 SSBO 与反射一致，instructionCount=21387、ssaValueCount=16254、maxLiveSsaValues=123、branchCount=2096、loopCount=127；32 个 forward.fs 变体资源分类逐项匹配反射，detail 总量约 467 MB；debug forward.vs sourceLineCount=12、10 条指令含 sourceLine，summary debugInfo=true。验证：47 cases / 1397 assertions，CTest 4/4；detail 通过 `validate`。注意 `spirv-dis` 缺失时变体 analysis failed，`spirv-cross` 缺失时回退反汇编资源声明。下一步 P5 报告索引、Diff、CSV、Reference。
- 2026-11-23：P5 完成。新增 `report_files`（递归摘要扫描、跳过变体明细、整树 `index.json` 重建）、`diff`（program/source+stage+backend、variant canonicalDefines hash；metric/binaryBytes delta；source/toolchain 指纹；added/removed；timestamps/regression 选项）、`export_csv`（23 列、分号、双引号转义、CRLF、空值）和 `reference`（只复制摘要、清除本地路径、清空目标、重建 index、拒绝重叠）。CLI 新增 `diff`/`export`/`set-reference`；`build` 结束后自动写 index。命令：`build --variants build/p4_vs_variants.json --backend spirv --out build/p5_verify`、`diff build/p5_verify build/p5_reference --out ...`、`export --report build/p5_verify --format csv --out ...`、`set-reference --from build/p5_reference_source --to shader_analysis/reference`。真实报告验证：P4 forward.fs spirv 对基线 32 matched/32 same/32 removed；同目录 diff 全 same；改 instructionCount 后 delta 正确且 `--fail-on-regression` 返回 1；reference 基线为 forward.fs dxbc/spirv 各 32 变体摘要。验证：56 cases / 1508 assertions，CTest 4/4。下一步 P6 Web 基础。
- 2026-11-23：P6 完成。新增 `tools/shader_analysis/web/`（Vite 6 + React 18 + TS + `@tanstack/react-virtual`）：`IndexPage` 索引表/搜索/backend 过滤；`ProgramPage` 变体过滤/列排序/CSV 下载/源码链接；`VariantPage` Overview（指标、opcode 直方图、资源、日志）与 Assembly；`serve.mjs` 只读服务（GET/HEAD、MIME、路径安全、SPA fallback、Vite proxy）；深链接使用 `dataset`/`summary`/`variantId`/`tab`。命令：`npm install`、`npm run build`；`build --backend all --pass forward --stage fragment --variant-filter "ALPHA_CLIP&HAS_MASK_MAP&HAS_NORMAL_MAP&DOUBLE_SIDED" --out shader_analysis/reports`；`node tools/shader_analysis/serve.mjs --root .`。真实报告：dxbc/spirv forward.fs 各 2 个变体；DXBC Assembly 27,042 条指令/21,186 个区间，虚拟列表只渲染可见行；源码 `/shaders/*`、参考摘要、深链接和 CSV 下载手工验证；`/web/../`、编码穿越返回 404，POST 返回 405。新增 `.gitignore`：12 位 hash 变体明细 JSON、web `node_modules`/`dist`。下一步 P7。
- 2026-11-23：P7 完成。新增 `RegisterTimeline`（Canvas 生命周期、读写、pressure 与 hover tooltip、class/搜索、点击依赖链、Escape、双击跳 Assembly）、`CBufferList`（按 slot 顺序、动态蓝色、instruction 跳转）、`SourceView`（Prism HLSL、行号、`sourceLine` 联动、源码行搜索/定位、Assembly 双击跳转和越界提示）、`DiffPage`（diff 包逐行汇编、指标 delta 红/绿、added/removed、query 深链）与 `filters` 的 `A&B|C` 语义。修复 debug detail 的 `<id>-debug.json` 路径解析，并支持 Diff 对 debug/detail 的回退查找。新增 `tools/shader_analysis/README.md`、`docs/shader-analysis.md`，更新 `.gitignore` 和手工验收清单。验证：`npm run build` 73 modules；真实 forward.fs dxbc/spirv + forward.vs debug；27,042 条虚拟 Assembly；Assembly 跳转竖线（DXBC 结构化 if/loop/switch 边界 1,227 条，SPIR-V branch target 3,014 条），hover 高亮 14/34 行；Registers DXBC 58 lanes / SPIR-V top 72 SSA；CBuffer 动态 fixture；Diff changed/added/removed 与 1,237 行汇编 diff；工作区 variants 已重新导出为 950 tasks（475/后端），CTest 4/4。所有改动未提交。
- 2026-11-24：最终 Web 回归。走查 Index/Reference/Program/Variant Overview/Assembly/Registers/CBuffer/Source/Diff 共 10 个路由，全部 ready；SPIR-V Assembly 滚动区域显示 9 条可见跳转线；无 console error、无 4xx 响应；Source 搜索 `ALPHA_CLIP` 命中 1 行；SPIR-V debug Assembly 双击 `sourceLine=835` 跳转 Source 并显示越界警告。补齐内联 favicon 消除 `/favicon.ico` 404；`GET/HEAD /web/` 200、`POST` 405、编码路径穿越 404、`/shaders/*` 200；根 README 工具列表已加入 Viewer 链接。修正 `.gitignore`：原 `reports/*/*/` 会误伤嵌套 source 目录摘要，改为仅忽略 12 位 hash 明细 JSON；`index.json` 与 3 个 source 摘要恢复为可提交。`npm run build` 73 modules、typecheck、CTest 4/4、plan-check 950 均通过；仍未提交。
- 2026-11-24：P0b 与交互/烟测优化完成。`variants.json` 加入 `passes` 槽位映射（950 tasks / 475 每后端）；新增 Python `--execute-plan` 校验 tasks/passes、按任务 optimization 编译并写 manifest；普通构建在计划存在时默认从 JSON 执行，CMake `hbg_shaders` 先 plan-check 再 execute，`--no-plan` 可回退；`--plan-check` 同时比较 `passes`。Web Assembly 搜索默认高亮并支持 `Filter matches` 切换；Source 搜索写入 `q` 深链接，支持 Enter/Shift+Enter/F3/Previous/Next；新增 `npm run smoke`/`smoke:browser`，临时启动 serve.mjs 并通过 headless Chrome 验证 Index、Program、Source 搜索、Assembly 控件。验证：`npm run build`、typecheck、smoke 通过；`--execute-plan --only build/dxbc/forward.vs.bin --no-manifest` 通过；plan-check 950 通过；另用 CDP 验证 Assembly 搜索高亮命中 3 条、切换过滤后 toolbar 显示 3,995 条，Source `float` Enter 从 1/2 导航到 2/2。仍未提交。
- 2026-11-24：CLI 收尾烟测。`list` 3 个摘要、`validate` 3 个摘要通过；`diff reports reference` 输出 4 matched/4 same/1 added/60 removed；`export` 输出 5 行 CSV；`set-reference` 复制 3 个摘要并重建 3 条 index，全部 exit 0。临时输出在 `build/p8_cli_smoke/`（未跟踪）。

---

### 附录 A：原 Viewer 功能对照

| 原功能 | 原位置 | 新位置 | 说明 |
|---|---|---|---|
| 寄存器类型 | `ShaderAnalyzer.cs:13-21` | `dxbc_parser.h`/`spirv_parser.h` | DXBC 寄存器类/SPIR-V SSA |
| 汇编解析 | `ShaderAnalyzer.cs:338-522` | `dxbc_parser.cpp`/`spirv_parser.cpp` | 解析真实后端文本 |
| 生命周期 | `ShaderAnalyzer.cs:195-232` | `liveness.cpp` | 线性反向扫描移植 |
| 每行 alive 集合 | `ShaderAnalyzer.cs:243-333` | `liveness.cpp` | 展开为指令级 |
| 跳转缩进 | `ShaderAnalyzer.cs:525-550` | `dxbc_parser.cpp`/`spirv_parser.cpp` | 结构化分支 |
| 代码行/汇编行视图 | `ShaderViewerWindow.cs:288-374` | `AssemblyList.tsx`/`RegisterTimeline.tsx`/`SourceView.tsx` | 无源码映射时仅汇编；有 `sourceLine` 时 Source 显示 `asm` 反向定位（多条可展开选择）、峰值压力 `P<n>`（点击进 Registers 峰值指令）、去重 `R<n>/W<n>`、live 数 `A<n>` + lane rail（hover 高亮 lifetime 行，点击进 Registers，`+N` 查看隐藏 lane，tooltip 含 lane/区间）+ 行尾 pressure/alive 双色条 + 选中 lane 的 lifetime 行高亮/色条描边 |
| 汇编行寄存器图/压力 | `ShaderViewerWindow.cs:288-374, 763-961` | `RegisterTimeline.tsx` / `AssemblyList.tsx` | Assembly 行选中后聚焦指令；focus note 显示 opcode/文本/live/读写与 Source 入口；每行显示去重 read/write 计数（点击弹出寄存器列表并跳 Registers 选中 lane）、按峰值缩放的 pressure 条，以及由 `liveness` 聚合的 live lane rail（最多 16 条，读写标记，tooltip 含生命周期区间与首写指令/源码行，hover 高亮该 lane 仍存活的可见行，点击跳 Registers，URL `reg` 选中的 lane 保持描边/存活行高亮，依赖链 lane 橙色描边、无关 lane 变暗，`+N` 菜单列出隐藏 lane） |
| 生命周期条带 | `ShaderViewerWindow.cs:763-896` | `RegisterTimeline.tsx` | Canvas；hover 含最近写入指令与写入源码行文本，选中显示首写指令/文本/源码文本/Source（原 tooltip 为 `v{i}` + write source line） |
| 读写条 | 同上 | 同上 | 蓝/红 |
| 依赖链高亮 | `ShaderViewerWindow.cs:963-1001` | 同上 | 点击 + Escape |
| 占用率表 | `ShaderViewerWindow.cs:542-617` | 不复刻 | 主机专用 |
| 跳转线 | `ShaderViewerWindow.cs:1009-1044` | `AssemblyList.tsx`/`SourceView.tsx` | seed 42；Assembly tooltip 含 branch label/from-to 指令/行范围，点击/Enter 选中目标；Source 按 sourceLine 绘制 jump 竖线，40px gutter 对齐原 k_JumpTableWidth，indent*7 分列，hover 高亮范围并点击跳目标行 |
| CBuffer 顺序/source 上下文 | `ShaderViewerWindow.cs:1046-1072` | `CBufferList.tsx` | 乱序/动态；source-mapped 读取跳 `L<n>`（原实现按 code line 组织，Web 用 Source 列）；Source 行显示 `CB<n>`，点击带 `cbline` 聚焦并高亮对应 CBuffer 读取 |
| code line tooltip | `ShaderViewerWindow.cs:505-514, 526-539` | `SourceView.tsx`/`AssemblyList.tsx` | Source 列出映射汇编、分支目标、文件:行号；Assembly 行 tooltip 附 source line 文本或越界提示 |
| 搜索高亮 | `ShaderViewerWindow.cs:376-394` | `AssemblyList.tsx`/`SourceView.tsx` | 同语义；Assembly/Source 均支持 Enter/Shift+Enter/F3 与 Prev/Next 命中导航，`asmhit`/`qhit` 可复现当前命中 |

### 附录 B：报告字段与 Unity 字段对照

| Unity 字段 | 新字段 | 说明 |
|---|---|---|
| `program.name` | `source.path` + `stage` | 程序身份改变（见 3.4） |
| `cu.multicompileIndex` | `variantId` + `canonicalDefines` | 用内容哈希而非索引 |
| `cu.defines` | `variant.defines` | 保持构建顺序 |
| `cu.multicompileDefines` | `variant.keywords`（或保留组合 defines） | 名称列表 |
| `cu.warnings/errors` | `variant.logs` | 结构化 |
| `p.microCodeSize` | `metrics.binaryBytes` | 产物字节数 |
| `p.VGPRCount` | `metrics.tempRegisterMax`（DXBC） / `null`（SPIR-V） | 语义近似 |
| `p.VGPRUsedCount` | `metrics.tempRegisterPeak`（DXBC） / `maxLiveSsaValues`（SPIR-V） | 估算 |
| `p.SGPRCount/UsedCount/UserSGPRCount` | `null` | 不适用 |
| `p.LDSSize` | `null` | 不适用 |
| `p.threadGroupWaves` | `null` | 不适用 |
| `p.SIMD/CU Occupancy*` | `null` | 不复刻主机查表 |
| `rawReport` | `detailPath` + 原始 `.disasm`/`.spv.asm.txt` | 不内嵌大文本 |

### 附录 C：命令速查

```powershell
# 变体清单
py -3 tools/shader_build.py --dump-plan shaders/variants.json
py -3 tools/shader_build.py --plan-check shaders/variants.json

# C++ 工具
cmake --preset vs2022
cmake --build --preset vs2022-debug --target hbg_shader_analysis
.\build\vs2022\Debug\hbg_shader_analysis.exe --self-test
.\build\vs2022\Debug\hbg_shader_analysis.exe build --dry-run --backend all --pass "forward,deferred_lighting*"
.\build\vs2022\Debug\hbg_shader_analysis.exe build --backend all --pass "forward,deferred_lighting*" --jobs 8
.\build\vs2022\Debug\hbg_shader_analysis.exe diff shader_analysis/reports shader_analysis/reference --out shader_analysis/reports/diff.json
.\build\vs2022\Debug\hbg_shader_analysis.exe export --report shader_analysis/reports --format csv --out shader_analysis/reports/report.csv

# 测试
ctest --test-dir build/vs2022 -C Debug -R shader_analysis --output-on-failure

# Web（本地）
cd tools/shader_analysis/web; npm install; npm run build
cd ../../..; node tools/shader_analysis/serve.mjs --root .
# 或开发模式
cd tools/shader_analysis/web; npm run dev
```
- 2026-11-24：原 Viewer 交互补齐：Assembly 单击行把 `instruction` 写入深链接并在 Registers 页以橙色虚线游标聚焦该指令；Registers 只强调该时刻 live lane，显示 live/read/write 计数，支持清除 focus，Assembly 工具栏增加 `Inspect registers`。行为对照原 `ShaderViewerWindow.cs` 的 assembly register graph（`ShaderViewerWindow.cs:288-374, 763-1001`，按行 alive/read/write 着色）；Web 仍采用独立的 Assembly/Registers 两页，不内嵌 Canvas。验证：`npm run build` 73 modules；CDP 点击第 0 行后 URL `instruction=0`、Assembly 1 个 focused 行、Inspect 按钮启用；Registers 显示 `Instruction 0 · 1 live r lane(s) · 0 read / 1 write`，清除后 URL 去掉 `instruction`。未提交。
- 2026-11-24：原 Viewer 汇编行压力条补齐。`AssemblyList` 按 backend 选择 `pressure.temp`（DXBC）或 `pressure.ssa`（SPIR-V），以全表峰值归一化在每行右侧显示 track/bar/value，tooltip 标注指令与 live 数量；对齐原 `ShaderViewerWindow.cs:288-374, 763-961` 的逐行 pressure/occupancy 视觉，但不移植 PS4/PS5 occupancy。验证：`npm run build` 73 modules；headless DOM 采样 Assembly 44 个 `.asm-pressure`，首个 tooltip 为 `1 live temp registers at instruction 0`；`tab=registers&instruction=3` 显示焦点游标/计数；`npm run smoke` 通过。未提交。
- 2026-11-24：原 Viewer 逐行 read/write 标识补齐。`AssemblyList` 对 `instruction.reads/writes` 按 `class:index` 去重，在行右侧显示蓝色 `R<n>`/红色 `W<n>`，tooltip 保留完整 refs；颜色与 Registers 页 read/write 语义一致。验证：`npm run build` 73 modules；headless DOM 采样 44 个 `.asm-rw`，首行 tooltip 为 `read: v5xyzx | write: r0xyz`；pressure 条仍为 44 个；`npm run smoke` 通过。未提交。
- 2026-11-24：Source 行交互补齐。`SourceView` 单击源码行把 `line` 写入深链接并高亮，双击复制 `path:line`，页头提供 `Copy path:line`；滚动改为仅当目标行不在可视区时居中，避免单击已可见行时跳动。验证：`npm run build` 73 modules；CDP 点击第 20 行后 URL `line=20`、唯一 active 行为 20、页头出现 `Copy path:line`；`npm run smoke` 通过。未提交。
- 2026-11-24：Source→Assembly 反向映射补齐。`SourceView` 汇总 `detail.instructions[].sourceLine`：仅在原始文件行范围内的映射显示 `asm <n>` 徽标，点击跳 Assembly 并聚焦首条指令；超出文件的预处理行只显示数量警告，不伪造行号。对齐原 `ShaderAnalyzer.cs` 的 code-line/assembly 关联语义，但保留 Web 的 Source/Assembly 分页。验证：`npm run build` 73 modules；用临时 report 将 3 条指令映射到 forward.fs.sc 行 20–22，DOM 显示 3 个 `asm 1`，点击后 URL `tab=assembly&instruction=0` 且行 00000 focused；真实 SPIR-V debug 报告显示 `10 instruction(s) reference preprocessed source lines outside this file` 且无徽标；`npm run smoke` 通过。临时 report 已清理；未提交。
- 2026-11-24：Registers focus 详情补齐。focus note 现在显示指令 opcode、完整文本、live/read/write 计数；若该指令有 `sourceLine`，提供 `Source L<n>` 按钮跳 Source。验证：`npm run build` 73 modules；真实 DXBC `instruction=3` 显示 `rsq · 1 live r lane(s) · 1 read / 1 write` 和指令文本；SPIR-V debug `instruction=1168` 显示 `OpCompositeExtract · 6 live ssa lane(s)` 及 `Source L835`；`npm run smoke` 通过。未提交。
- 2026-11-24：Assembly 搜索状态补齐。搜索词与 `Filter matches` 不再只是组件内 state，而是写入 `asmq`/`asmfilter=matches` 深链接；切换 tab、刷新或分享 URL 后仍保留查询与过滤。参照原 Viewer 单一 `CodeSearch` 在 tab 切换时保持搜索状态的行为（`ShaderViewerWindow.cs:207-211, 376-393`），Web 按 Assembly/Source 分栏各保留自己的查询。验证：`npm run build` 73 modules；CDP 在 Assembly 输入 `rsq` 后 URL 变为 `asmq=rsq`，勾选过滤后变为 `asmfilter=matches`，toolbar 显示 `180 of 27,042 instructions` 与 `180 matching · filtered`；直接打开 `asmq=rsq&asmfilter=matches` 渲染 180 条过滤结果；`npm run smoke` 增加 Assembly 深链接断言并通过。未提交。
- 2026-11-24：CBuffer source 上下文补齐。参考原 `ShaderViewerWindow.cs:1046-1072` 按 code line 展示 read 的方式，Web 在 CBuffer 表增加 `Source` 列：从对应 instruction 的 `sourceLine` 生成 `L<n>` 链接，点击跳 Source；未带 debug 映射时显示破折号，并在汇总区显示 mapped lines 数量。验证：`npm run build` 73 modules；用临时 dxbc detail 将一条 cbuffer read 映射到 forward.fs.sc:20，DOM 显示 1 个 `L20` 和 `1 mapped lines`，点击后 URL 变为 `tab=source&line=20`；临时 summary/detail 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source 行 tooltip 补齐。参考原 `ShaderViewerWindow.cs:505-514` 的 `GetCodeLineLabel` tooltip（汇编行列表 + 文件名:行号 + jump 信息），`SourceView` 现在在每行 title 中列出映射到该行的指令文本（最多 10 条，超出显示剩余数）、branch 目标及目标 source line、`文件名:行号` 和交互提示。验证：`npm run build` 73 modules；临时 dxbc detail 把 3 条指令映射到 forward.fs.sc 行 20/21，DOM title 含两条汇编、`if 1 → 2 (source L20)`、`forward.fs.sc:20`；未映射行仅显示位置/提示；临时 summary/detail 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Assembly 跳转线 tooltip 补齐。参考原 `ShaderViewerWindow.cs:1009-1044` 的 `CreateJumpLine` tooltip（Jumps from/to 与两端代码），Web jump wire 现在显示 branch label、`From <index>: <text>`、`To <index>: <text>` 和源行范围；hover 时仍高亮整个跳转区间。验证：`npm run build` 73 modules；SPIR-V `instruction=601` 滚动到分支区后 6 条可见 jump wire，首个 tooltip 为 `selectionMerge 601 → 634`、`From 601: OpSelectionMerge %11105 None`、`To 634: %11105 = OpLabel`、`rows 602–635`；`npm run smoke` 通过。未提交。
- 2026-11-24：Registers lane 写入来源补齐。参考原 `ShaderViewerWindow.cs:788, 899` 生命周期 tooltip 的 `v{i}` + `sourceWriteLine` 信息，RegisterTimeline 的 hover 现在显示最近写入指令（index/opcode），选中 lane 后显示首条写入指令按钮（跳 Assembly）、写入文本，以及可用时的 `Source L<n>`。验证：`npm run build` 73 modules；真实 DXBC 选中 r4 后显示 hover `write 3261 mov`、选中面板 `First write instruction 9 · mov` 与 `mov r4.xyz, v1.xyzx`；临时 detail 将 r0 首写映射到 forward.fs.sc:20，选中后出现 `Source L20`，点击 URL 变为 `tab=source&line=20`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Registers 压力曲线 hover 补齐。参考原 `ShaderViewerWindow.cs:645, 715` 压力块 tooltip，RegisterTimeline 顶部 pressure 图现在可 hover：按 x 映射 instruction，显示该处 pressure、opcode 和 instruction 索引（空点显示 `pressure —`），不影响 lane hover/选择。验证：`npm run build` 73 modules；CDP 在 DXBC `instruction=3` 的压力区 hover 显示 `pressure 20 · ld_indexable · instruction 7178`；lane hover 仍显示写指令与 pressure；`npm run smoke` 通过。未提交。
- 2026-11-24：Source 行峰值压力补齐。参考原 `ShaderViewerWindow.cs:619-656` code line 的压力条/数值，`SourceView` 对映射到同一源码行的指令取 backend 对应的 `pressure.temp`/`pressure.ssa` 峰值，显示 `P<n>` 徽标；tooltip 标注峰值类型与映射指令数。验证：`npm run build` 73 modules；临时 dxbc detail 在 forward.fs.sc:20 放两条 pressure 7/3 的指令，DOM 显示 `P7`，title 为 `Peak temp pressure across 2 mapped instruction(s) on line 20`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Assembly 行 source 预览补齐。参考原 `ShaderViewerWindow.cs:526-539` 的 assembly 行 tooltip（code line + file:line），`AssemblyList` 按 `sourcePath` 加载源码并在行 tooltip 中添加 `source <file>:<line> — <text>`；源码行超出文件时显示 `source L<n> (outside <file>)`，加载失败/未完成时只显示 `source L<n>`。验证：`npm run build` 73 modules；真实 SPIR-V debug `instruction=1168` 行 tooltip 显示 `source L835 (outside forward.vs.sc)`；`npm run smoke` 通过。未提交。
- 2026-11-24：Source 行 read/write 计数补齐。参考原 `ShaderViewerWindow.cs:619-688, 763-896` code line 的 registerReads/registerWrites 语义，`SourceView` 聚合映射到同一源码行的指令读写（按 `class:index` 去重），在 `asm`/压力徽标旁显示蓝色 `R<n>`、红色 `W<n>`，tooltip 列出完整 read/write refs。验证：`npm run build` 73 modules；临时 dxbc detail 在 forward.fs.sc:20 的指令读 r0/r1、写 r2/r3，DOM 显示 `R2 W2`，title 为 `read: r:0, r:1 | write: r:2, r:3`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source jump 竖线补齐。参考原 `ShaderViewerWindow.cs:1009-1044` 在 code view 绘制 jump line 的行为，`SourceView` 从映射指令的 `branch.targetIndex` + 目标指令 `sourceLine` 生成 source jump：左侧新增 gutter，按行实际几何位置绘制彩色竖线/端点；tooltip 显示 branch label、from/to 源码行和指令文本，hover 高亮起止源码范围。验证：`npm run build` 73 modules；临时 detail 将 if 20→21、endif 24→21 映射到 forward.fs.sc，DOM 显示 2 条 source jump 和范围提示；hover 后 1 条 active、line 20/21 均带 `jump-highlight`；竖线几何与行中心对齐（top/height 来自实测 rect）；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source 行 live 寄存器数补齐。参考原 `ShaderViewerWindow.cs:650-656` code line 的 `aliveRegisterCount`，`SourceView` 接收 detail liveness；对映射到同一源码行的每条指令统计覆盖它的区间数，取最大值显示绿色 `A<n>`，tooltip 标注跨映射指令的最大 live 数。验证：`npm run build` 73 modules；临时 dxbc detail 在 forward.fs.sc:20 放 2 条指令，liveness 使指令处最多 3 个区间存活，DOM 显示 `A3` 和 `Max live registers across mapped instructions on line 20`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source jump 缩进对齐原 Viewer。参考 `ShaderViewerWindow.cs:40` 的 `k_JumpTableWidth = 40` 与 `:1022` 的 `indentLevel * 7`，Source jump gutter/layer 从 1.2rem 扩到 2.5rem（40px），竖线按 branch indent 以 7px 分列并限制 5 层，hover 时提高 z-index 并在竖线后加浅色背景。验证：`npm run build` 73 modules；临时 nested detail 生成 line 20→24 indent 0、line 21→22 indent 1 两条 jump，Chrome/CDP 读取 gutter/layer 均 40px，竖线 left 分别为 4px/11px；hover 第二条后 active z-index=3，line 21/22 带 `jump-highlight`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source 多指令映射导航补齐。参考原 `ShaderViewerWindow.cs:505-514` code line tooltip/assembly line 列表，`SourceView` 的 `asm <n>` 在单条映射时仍直接跳 Assembly；多条映射时展开行内 instruction list，显示 index/opcode/text/branch target，点击任一条跳对应 Assembly 指令。展开后 source jump 高度随行位移重算。验证：`npm run build` 73 modules；临时 detail 在 forward.fs.sc:20 映射 3 条指令并带 20→24 jump，DOM 显示 `asm 3 ▾`，点击后 3 行列表展开，jump height 87.125→160.547、line24 top 同步下移 73.42，点击第二条后 URL 为 `tab=assembly&instruction=2`；点击外无卡死；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source cbuffer 读取提示补齐。参考原 `ShaderViewerWindow.cs:1046-1072` 按 code line 组织 cbuffer read 的行为，`SourceView` 从 instructions 的 `sourceLine` 聚合 `cbufferReads`，在有读取的源码行显示 `CB<n>`；tooltip 按 instruction 列出 slot/offset/dwords/dynamic，点击切到 CBuffer 页。未映射或越界的 read 不显示。验证：`npm run build` 73 modules；临时 detail 在 forward.fs.sc:20 放 2 条 cb0 read（offset 0/16）、line 24 放 1 条动态 cb1 read，DOM 显示 `CB2`（offsets 0–16）和 `CB1`（dynamic），点击 `CB2` 后 URL 变为 `tab=cbuffer`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：CBuffer 源码行聚焦补齐。参考原 `ShaderViewerWindow.cs:1046-1072` 的按 code line 读取顺序视图，`CBufferList` 接收 `cbline`：来自 Source `CB<n>` 的点击写入 `tab=cbuffer&cbline=<line>`，表格聚焦匹配 source line 的行并淡化其他行，顶部显示匹配数量与 `Show all`；通过 `scrollIntoView` 滚动到首条匹配，手工切换 CBuffer tab 会清除 `cbline`。验证：`npm run build` 73 modules；临时 detail 在 line 20 放 2 条读取、line 24 放 1 条，Source 点击 `CB2` 后 URL 带 `cbline=20`，banner 显示 `Source line 20 · 2 mapped read(s)`，2 行 `cb-focus`、1 行 `cb-dimmed`；点击 `Show all` 后 `cbline` 移除、焦点/banner 清除；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source live 寄存器入口补齐。参考原 `ShaderViewerWindow.cs:648-657` code line 的 alive register count/列，Source 的 `A<n>` 现在汇总映射指令覆盖到的 lane（class:index），tooltip 列出最多 16 个并提示首条映射指令；点击切到 `tab=registers&instruction=<first>`，由既有 RegisterTimeline focus 显示该指令游标与 live lanes。验证：`npm run build` 73 modules；临时 detail 在 forward.fs.sc:20 映射 2 条指令，liveness 构造 r0/r1/r2 三个 lane，DOM 显示 `A3`，tooltip 含 `r:0, r:1, r:2` 与 `Inspect registers at instruction 1`；点击后 URL 为 `tab=registers&instruction=1`，Registers 页渲染；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source pressure 峰值入口补齐。参考原 `ShaderViewerWindow.cs:637-646` code line pressure/occupancy 方块，Source 的 `P<n>` 现在记录该行峰值所在 instruction，tooltip 显示 peak pressure/instruction/映射指令数，点击切到 `tab=registers&instruction=<peak>`，而不是像 `A<n>` 固定首条映射。验证：`npm run build` 73 modules；临时 detail 在 forward.fs.sc:20 映射 pressure 3/5 两条指令，DOM 显示 `P5`，tooltip 为 `Peak temp pressure 5 at instruction 2 ...`，点击后 URL 为 `tab=registers&instruction=2`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：CBuffer Read order 视图补齐。参考原 `ShaderViewerWindow.cs:1046-1072` 按 code line 展示 `Read N DWORD at offset X.` / dynamic scalar offset 的布局，`CBufferList` 增加 Table/Read order 切换（`cbview=order` 持久化）；Read order 按 source line 分组（无映射按 instruction），保留 slot、instruction、DWORD、dynamic、状态色和 `L<n>`/instruction 跳转，并继续支持 `cbline` 聚焦。验证：`npm run build` 73 modules；临时 detail 在 line 20 放 2 条 read、line 24 放 1 条 dynamic，CBuffer 页切到 Read order 后 2 个 group，header 为 `Source L20 2 read(s) 8 DWORD` 与 `Source L24 1 read(s) 4 DWORD 1 dynamic`，read 文案精确匹配，cbline=20 时 1 组 focus/1 组 dim；切回 Table 正常；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：CBuffer 视图状态持久化。`CBufferList` 的 Table/Read order 由 `VariantPage` 的 `cbview` query 控制：`cbview=order` 加载 Read order，切回 Table 时删除参数；`cbline` 聚焦、Source `CB<n>` 跳转和 tab 切换均保留视图状态。验证：`npm run build` 73 modules；直接打开 `tab=cbuffer&cbview=order` 显示 Read order 且按钮 active；切 Table 后 URL 移除 `cbview`，再切 Read order 后写回；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source R/W 与列表键盘交互补齐。参考原 `ShaderViewerWindow.cs:768-770, 841-854` code line 的 registerRead/registerWrite 标记，`R<n>/W<n>` 现在为按钮，tooltip 列 read/write 寄存器并提示首条映射指令，点击切到 `tab=registers&instruction=<first>`；多条映射的 `asm <n>` 列表支持 Escape 收起。验证：`npm run build` 73 modules；临时 detail 在 forward.fs.sc:20 映射 2 条指令（R2/W2），DOM 显示 button R2/W2、title 含 r:0/r:1 与 `Inspect registers at instruction 1`，点击后 URL 为 `tab=registers&instruction=1`；重新打开 Source 展开 2 行列表后 Escape，列表行数 2→0、aria-expanded false；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Assembly 跳转线导航补齐。保留参考原 `ShaderViewerWindow.cs:1009-1044` 的 hover/from-to tooltip，新增点击及 Enter/Space 选中目标：`.asm-jump-group` 变为可操作 button（tabIndex/aria-label），tooltip 追加 `Click to select target instruction N`，CSS 提供 pointer/focus 样式；点击后既有 `selectInstruction` 写入 `instruction` 并滚动到目标行。验证：`npm run build` 73 modules；临时 4 指令 if/endif fixture 生成 3 条 jump，DOM role=button、tabindex=0、tooltip 含点击提示；点击第一条后 URL 为 `tab=assembly&instruction=2`，目标行显示 focused 且内容为 `00002 mul ...`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source 跳转线导航补齐。保留原 `ShaderViewerWindow.cs:1009-1044` 的 hover/范围高亮，Source jump 由装饰 span 改为可聚焦 button，tooltip 追加 `Click to jump to L<n>`，aria-label 描述 branch/target；点击写入 `line=<target>` 并复用 Source 滚动/高亮，focus 也触发范围高亮。验证：`npm run build` 73 modules；临时 nested detail 生成 20→24、21→22 两条 jump，DOM 两个 BUTTON、aria-label 正确；点击第二条后 URL 带 `line=22`，唯一 active 行为 22；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source live lane 区间补齐。参考原 `ShaderViewerWindow.cs:765-793` 的 alive register lifetime metadata，`SourceView` 在聚合 A<n> lane 时保留每个 `class:index` 的 liveness `[start,end]`（去重），tooltip 从裸 lane 列表升级为 `r:0 [0–2], r:1 [1–2]`，仍支持点击首条映射指令进 Registers。验证：`npm run build` 73 modules；临时 detail 在 forward.fs.sc:20 映射 2 条指令，liveness 构造三个区间，headless DOM 显示 `A3`，title 为 `Max 3 ...` + `r:0 [0–2], r:1 [1–2], r:2 [2–2]` + `Inspect registers at instruction 1`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source live lane 彩色 rail 补齐。参考原 `ShaderViewerWindow.cs:763-793` 的逐寄存器 lifetime 列，Source 在 `A<n>` 后显示最多 12 个 3px lane 色条（按 class:index 确定性着色，超出显示 `+N`，菜单列出隐藏 lane 并可 hover 高亮/选择），rail tooltip 列出每个 lane 的 liveness 区间；A 按钮仍可点击进 Registers。验证：`npm run build` 73 modules；临时 detail 在 forward.fs.sc:20 构造 r0/r1/r2 三个 lane，DOM 显示 1 个 rail、3 个色条且颜色互异（rgb(226,182,80) / rgb(155,226,80) / rgb(80,226,119)），rail title 含三个区间；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source lane rail 生命周期高亮。每个 live lane 色条可 hover，按该 lane 的 liveness interval 找到所有映射 instruction 对应的源码行并加 `lane-highlight`，tooltip 显示 lane/区间；离开清除。验证：`npm run build` 73 modules；临时 detail 映射 line 20/22/24，r:0 [0–3]、r:1 [1–2]；CDP hover r:0 高亮 20/22/24 三行，hover r:1 高亮 20/22 两行；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source lane 直达 Registers 选中。参考原 `ShaderViewerWindow.cs:795-819` 点击 lifetime 记录选中并高亮依赖，Source lane 色条改为 button：点击按该 lane liveness 起点写 `tab=registers&instruction=<start>&reg=<class:index>`；RegisterTimeline 新增 `focusRegister`，解析后切换 class 并选中 lane，同时保留首写/Source/依赖高亮能力。普通 `jumpToRegisters` 会删除 `reg`，避免旧选择误用。验证：`npm run build` 73 modules；临时 detail 映射 line 20/22、r0 0–2，Source 5 个 lane BUTTON；点击 r:0 后 URL 为 `tab=registers&instruction=0&reg=r%3A0`，Registers 显示 r0 首写面板（instruction 1 add / Source L20）；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source lane 选中后的 Registers 可见性修正。`RegisterTimeline` 原只渲染最长生命周期的 72 条 lane，Source rail 可点击第 79 条等隐藏 lane，导致 URL 已有 `reg=r:79` 但选中面板/依赖高亮不可见；现在若选中 lane 不在前 72 条，会将其固定在首行，并在说明中标注。Source lane 点击同时改为聚焦该源码行首条映射指令，而不是 lane 的全局 liveness 起点。验证：`npm run build` 73 modules；临时 detail 构造 80 条 r lane，仅 line 20 的 instruction 5 覆盖 r79；Source 仅显示 r79 色条，点击后 URL `instruction=5&reg=r%3A79`，Registers 显示 `Instruction 5 · add`、r79 选中面板，说明为 `Showing 73 of 80`，选中 lane 固定首行；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Assembly 行寄存器入口补齐。参考原 `ShaderViewerWindow.cs:898-961` 在每条 assembly line 绘制具体 read/write 寄存器条、`ShaderViewerWindow.cs:812-817` 点击 lifetime 后按依赖高亮的交互，Assembly 行的 `R<n>/W<n>` 从纯计数改为按钮；点击弹出该指令去重后的具体寄存器列表（`class:index` + 显示名），选择后写 `tab=registers&instruction=<n>&reg=<class:index>` 并选中 lane。Web 虚拟列表不内嵌寄存器图，因此用固定定位菜单暴露具体 lane；菜单支持外部点击、Escape、滚动关闭。验证：`npm run build` 73 modules；临时 detail 的 instruction 0 读 r0/r1、写 r1/r2，DOM 显示 R2/W2 按钮，点击 R2 弹出 `Reads · instruction 0` 与 r0/r1 两项，选择 r1 后 URL 为 `tab=registers&instruction=0&reg=r%3A1`，Registers 显示 r1 选中面板和首写；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Registers 写入源码上下文补齐。参考原 `ShaderViewerWindow.cs:788, 899` 的 lifetime tooltip（`v{i}` + `sourceWriteLine.highlightedCode`），RegisterTimeline 接收 `sourcePath` 并加载源码：hover lane 时在最近写入指令后追加 `source <file>:<line> — <text>`；选中 lane 的首写面板新增源码行文本，保留原有 `Source L<n>` 跳转，越界/未加载/空行均有明确显示。验证：`npm run build` 73 modules；临时 detail 将 instruction 0 的 r1 首写映射到 forward.fs.sc:20，直接打开 `tab=registers&instruction=0&reg=r%3A1` 后选中面板显示 `source forward.fs.sc:20`；Escape 清除选择后 canvas hover r1 显示 `write 0 mov · source forward.fs.sc:20 — (blank line)`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Registers 选择状态与深链接同步。参考原 `ShaderViewerWindow.cs:812-817` 点击 lifetime 选中依赖、`:249-252`/`:994-1001` Escape 清除选择，RegisterTimeline 的 canvas 点击、空白点击、Escape、Clear、class 切换均回调 VariantPage 同步 `reg`：选中写 `reg=<class:index>`，清除则删除；普通聚焦指令仍保留 `instruction`。刷新不再恢复已清除的旧选择，手工点击的 lane 也可通过 URL 复现。验证：`npm run build` 73 modules；临时 4 lane detail 在 canvas 点击 r2 后 URL 增加 `reg=r%3A2` 并显示选中面板，Escape 后 `reg` 移除且刷新无选中；再次点击后 Clear 同样移除 `reg`；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Registers class 深链接补齐。参考原 `ShaderViewerWindow.cs:134-152` 的 VGPR/SGPR tab 选择与 `:288-374` 分寄存器类型构建视图，Web 的 register class 下拉现在写入 `regclass`：刷新/分享后保持同一 class；手动切换 class 会清除旧 `reg`，避免选中项与 class 不一致；同时存在 `regclass` 与 `reg` 时以 `reg` 的 class 优先，防止两个 URL 参数互相拉扯。Source lane、Assembly R/W 菜单跳转时会同时写入对应 `regclass`。验证：`npm run build` 73 modules；临时 detail 同时含 r/ssa lane，直接打开 `regclass=ssa` 选中 ssa；`regclass=ssa&reg=r%3A2` 最终选中 r class 的 r2；从该状态手动切到 ssa 后 URL 移除 `reg`、保留 `regclass=ssa`，刷新仍为 ssa 且无选中；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source 隐藏 lane 菜单补齐。参考原 `ShaderViewerWindow.cs:763-896` 按 `maxVGPRUsed/maxSGPRUsed` 遍历并绘制全部寄存器 lifetime 的行为，Source lane rail 的 `+N` 从静态计数改为按钮：点击弹出被截断的具体 lane 列表，hover 仍高亮对应源码 lifetime 行，选择后跳 Registers 并选中该 lane（同时写 `regclass`）。菜单支持外部点击/Escape/滚动关闭。验证：`npm run build` 73 modules；临时 detail 在 line 20 放 15 条 r lane，rail 显示 12 条后 `+3`；点击后菜单 3 项为 r:7/r:8/r:9，hover 首项产生 1 条 `.lane-highlight`，选择 r:7 后 URL 为 `tab=registers&instruction=0&reg=r%3A7&regclass=r` 且选中面板正常；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：Source lane 顺序修正。原 rail 用字符串排序，出现 r:0、r:1、r:10…r:2 的顺序，与 `ShaderViewerWindow.cs:763-896` 按数值遍历寄存器的行为不一致。新增 `compareLaneLabels`：先按 class，再按数值 index；`A<n>` tooltip、rail、`+N` 菜单统一使用该顺序。验证：`npm run build` 73 modules；临时 15 条 r lane 的 rail 依次为 r:0…r:11，`+3` 菜单为 r:12/r:13/r:14，rail title 同为数值顺序；临时 report 已清理；`npm run smoke` 通过。未提交。
- 2026-11-24：离线 detail JSON 上传补齐。参考原 `ShaderViewerWindow.ShowReportFromFile`（`ShaderViewerWindow.cs:79-104`）读取外部报告并调用 `UpdateViewWithTextFile`（`:489-496`）打开 Viewer 的入口，Web 新增全局拖拽与 `Upload` 页：variant detail JSON 在浏览器本地解析（最大 64 MB），成功后在 `/web/upload` 复用 Assembly/Registers/CBuffer/Source 组件；Assembly R/W 菜单、Source lane、CBuffer 跳转等仍可联动，Source 仍从本地服务加载 detail.source。summary/index 及格式不符文件显示明确错误，不清除上一个有效 detail；`Clear upload` 返回拖放页。数据仅存在内存，刷新清空，符合“不向服务器上传”。验证：`npm run build` 75 modules；CDP 从 Index 拖入临时 detail（2 指令/3 区间/1 cbuffer read），拖拽时出现 overlay，drop 后路由 `/web/upload`、标题 uploadtest、Assembly 2 行；切换 Registers 显示 canvas、CBuffer 显示 1 行、Source 加载真实 `.sc`；再拖入 `{}` 显示错误且保留原 detail；`Clear upload` 回到 `Open a report detail`；`npm run smoke` 增加 `/web/upload` 断言并通过。未提交。
- 2026-11-24：Overview 指标补齐。Variant Overview 新增 DXBC `sampleCount`/`loadCount`/`storeCount`、SPIR-V `loadCount`/`storeCount`/`pushConstantBytes` 卡片；页头 eyebrow 显示 `profile` 与 `-O<optimization>`，variant meta 显示短 shaderc 版本（完整版本保留在 title）。这些字段此前已写入报告但未在 Web 暴露。验证：`npm run build` 75 modules；真实 DXBC forward.fs `7fc8f01bdc03` 页面 eyebrow 为 `DXBC · D3D11 · FRAGMENT · S_5_0 · -O3`，meta 显示 `shaderc 1.19.167.`，卡片 Samples=122、Loads=264、Stores=0；`npm run smoke` 通过。未提交。
- 2026-11-24：Upload 支持 summary JSON。上传器现在区分 detail 与 report summary：summary 显示来源/backend/stage/profile/opt、生成时间、短 shaderc 版本、ok/failed 数量，以及 Variant/Status/Defines/Instructions/Peak/Binary/Compile 表；不支持从 summary 直接打开 detail（需再拖入 detail 文件），index JSON 仍明确拒绝。`shadercVersion` 提取逻辑移到 `utils/format` 供 Variant 与 Upload 共用。验证：`npm run build` 75 modules；CDP 拖入真实 `dxbc/passes/forward.fs.json` 后 `/web/upload` 标题为源文件路径、meta `2 variants 2 ok … summary only`、表 2 行、无 variant tabs；再拖入临时 detail 后立即切换为 uploadtest 与四个 tabs、Assembly 1 行；`npm run smoke` 更新 Upload 页面文案断言并通过。未提交。
- 2026-11-25：Assembly 行内 live lane rail 补齐。参考原 `ShaderViewerWindow.cs:690-750, 898-961` 的 assembly register graph（逐行 alive/read/write 列），`AssemblyList` 接收 `liveness`，用区间事件扫描预计算每条指令的 live lane，并在压力条旁显示最多 16 个 `class:index` 确定性色条；write 标顶部红线、read 标底部蓝线，tooltip 列 lane 与读写，点击直接切 Registers 并写入 `reg`/`regclass`/`instruction`，超出显示 `+N`（点击进入首个隐藏 lane）。验证：`npm run build` 75 modules；真实 DXBC `forward.fs.15` 初始 43 个可见 Assembly 行/40 个 rail，首条 title `1 live register lane(s) · r:0 · write`；滚动到中段 73 行全部有 rail，最多 12 lanes；点击首条 lane 后 URL 为 `tab=registers&instruction=0&reg=r%3A0&regclass=r`，Registers 显示 selected r0；临时 20-lane detail 显示 16 个色条 + `+4`，点击 `+4` 后 Registers 显示 r16；`npm run smoke` 通过。未提交。
- 2026-11-25：Assembly lane rail hover 补齐。参考原 `ShaderViewerWindow.cs:898-961` 的每条 assembly 行 alive/read/write 列与 `:963-1001` 的寄存器生命周期联动，hover 任一行内 lane 色条时，所有该 lane 仍存活的可见 Assembly 行加绿色 `lane-highlight`；离开色条、rail、滚动或切换搜索/过滤即清除，focused 行保持橙色优先级。tooltip 增加 hover 提示。验证：`npm run build` 75 modules；真实 DXBC `forward.fs.15` hover 首个 `r:0` 色条高亮 11 个可见 Assembly 行，移出后归零；`npm run smoke` 通过。未提交。
- 2026-11-25：Assembly 隐藏 lane 菜单补齐。参考原 `ShaderViewerWindow.cs:763-896` 按 `maxVGPRUsed` 遍历并绘制全部寄存器 lifetime 列的行为，以及 Source lane rail 已有的 `+N` 菜单，Assembly 的 `+N` 不再直接跳到首个隐藏 lane，而是弹出固定菜单列出全部被截断 lane；菜单支持 hover 高亮行、选择后跳 Registers 选中对应 lane，并支持外部点击/Escape/滚动关闭。验证：`npm run build` 75 modules；临时 20-lane detail 显示 16 个色条 + `+4`，菜单 4 项为 r:16/r:17/r:18/r:19，header 为 `More live lanes · instruction 0`；Escape 关闭菜单；选择 r:19 后切到 Registers 且选中面板包含 r19；`npm run smoke` 通过。未提交。
- 2026-11-25：Assembly lane tooltip 生命周期/首写信息补齐。参考原 `ShaderViewerWindow.cs:788, 922` 的 lifetime tooltip（`v{i}` + `sourceWriteLine.highlightedCode`），Assembly lane 色条 tooltip 现在列出该 lane 的 liveness 区间（最多 3 段，超出省略）、当前指令处 read/write，以及首条写入指令的 index/opcode 和 `sourceLine` 源码文本；源码未加载或越界时沿用既有 `source L<n>` / `outside` 提示。验证：`npm run build` 75 modules；真实 DXBC 首条 title 含 `r:0 [0–1, 2–3, 3–4…]` 与 `write 0 mov`；临时 detail 映射 source line 21，title 显示 `write 0 mov · source forward.fs.sc:21 — const vec2 contactShadowData = hbgSampleContactShadow(...)`；`npm run smoke` 通过。未提交。
- 2026-11-25：Assembly 选中 lane 持久高亮补齐。参考原 `ShaderViewerWindow.cs:795-819, 963-1001` 点击 lifetime 后保留选中并高亮相关寄存器的行为，AssemblyList 接收 URL `reg`（Variant 为 `focusRegisterKey`，Upload 为 `focusRegister`），把对应 lane 的色条加描边、其存活行加蓝色 `lane-selected`；hover 的绿色 `lane-highlight` 优先，清除 `reg` 后全部恢复。验证：`npm run build` 75 modules；真实 DXBC `tab=assembly&reg=r%3A4&regclass=r` 有 4 条可见 selected 行/4 个 selected 色条，首条 tooltip 含 `selected`；移除 `reg` 后两者均为 0；`npm run smoke` 通过。未提交。
- 2026-11-25：Assembly 依赖链高亮补齐。参考原 `ShaderViewerWindow.cs:795-819, 963-1001` 选中 lifetime 后遍历相关节点并把无关条变暗的 `HighlightRelatedVGPRUsage` 行为，把 RegisterTimeline 内的依赖链 BFS 提取为共享 `utils/dependencies.ts`；Assembly 选中 lane 时，依赖链 lane 色条加橙色描边、无关 lane 降为 0.32 opacity，hover 恢复亮度，工具栏显示 `selected <lane> (<n> related)`。验证：`npm run build` 76 modules；真实 DXBC `reg=r:4` 显示 95 个 dependent 色条（99 个可见色条全部属于选择/依赖链），toolbar 为 `selected r:4 (121 related)`；Registers r0 仍显示 `121 dependent register(s)`，确认提取后语义不变；临时 r0→r1、r2 无关的 detail 在选中 r0 后显示 2 selected / 1 dependent / 1 unrelated；`npm run smoke` 通过。未提交。
- 2026-11-25：Registers lane 排序切换补齐。参考原 `ShaderViewerWindow.cs:763-896` 按 `for i < maxVGPRUsed` 的寄存器编号顺序遍历绘制，新增长度/编号两种排序；默认保持生命周期最长优先，`Register index` 改为数值升序并写入 `regsort=index`，选中 lane 仍固定首行。验证：`npm run build` 76 modules；真实 DXBC 默认排序点击 canvas 第 2 行得到 `reg=r%3A6`，`regsort=index` 点击第 2 行得到 `reg=r%3A1`，说明均为 `register index order`；页面 dropdown 与 Upload 本地状态均已接入；`npm run smoke` 通过。未提交。
- 2026-11-25：Registers lane 分页补齐。参考原 `ShaderViewerWindow.cs:763-896` 遍历全部 `maxVGPRUsed/maxSGPRUsed` 寄存器列的行为，Web 仍按 72 条一页渲染，但新增 Prev/Next 与页码、`regpage` 深链接；当前页无选中 lane 时把选中 lane 固定首行，span 配色按全部 lane 归一化，搜索/class/sort 变化回到第 1 页，越界页码自动钳制并回写。验证：`npm run build` 76 modules；临时 80-lane detail 第 1 页 72 条/`1/2`，Next 后 8 条/`2/2`；在第 2 页选中 lane 后回第 1 页显示 73 条（选中固定首行）；真实 58-lane 报告带 `regpage=1` 自动回到第 1 页、隐藏分页控件并移除 URL 参数；`npm run smoke` 通过。未提交。
- 2026-11-25：Assembly/Source lane class 过滤补齐。参考原 `ShaderViewerWindow.cs:134-152` 的 VGPR/SGPR tab 选择与 `:288-374` 分寄存器类型构建视图、`:763-896` 按 class 遍历寄存器列，新增 `laneclass` 深链接：Assembly 与 Source 的 Lane class 下拉可在 All/r/x/ssa/resource 间切换；Assembly 只过滤 live lane rail、range/首写与依赖高亮，Source 过滤 A 计数、lane rail 和 lifetime hover；选中 lane 不属于当前 class 时 Assembly 工具栏不显示 selected 提示。验证：`npm run build` 76 modules；混合 r/x/ssa/resource 临时 detail 默认 4 条 rail，切 `ssa` 后 Assembly 仅 1 条 ssa rail、Source 显示 `A1` 且 rail title 为 `ssa:0`；真实 DXBC `instruction=851&laneclass=x` 的可见 rail 全为 `x:*` 且下拉值为 x；`npm run smoke` 通过。未提交。
- 2026-11-26：Assembly 搜索命中导航补齐。参考原 `ShaderViewerWindow.cs:376-394` 的 CodeSearch 全量高亮语义，Web 在既有 `asmq`/`asmfilter` 高亮/过滤上增加命中游标：Enter/F3 下一个、Shift+Enter/Shift+F3 上一个，toolbar `Prev`/`Next` 循环切换，当前命中行用 `search-match-active` 加重并在 toolbar 显示 `k / n`；当前序号写入 `asmhit`，搜索词或过滤切换时清除，越界访问自动 clamp。匹配逻辑抽到 `utils/assemblySearch.ts`，Variant 与 Upload 两套 Assembly 共用。验证：`npm run build` 77 modules；真实 DXBC `asmq=rsq` 显示 180 条命中、首条 `00003`，Next 后 URL `asmhit=2`、Active `00089` 且滚动，Enter 到 `3 / 180`/`00110`，Shift+F3 回到 `2 / 180`，首条 Prev 回绕到 `180 / 180`/`26898`，直接打开 `asmhit=2` 恢复 `00089`；勾选过滤后移除 `asmhit`、显示 `180 of 27,042` 与 `1 / 180`；Upload 混合 fixture 的 2 条 `add` 命中可 Enter/Prev 循环；`npm run smoke` 通过。未提交。
- 2026-11-26：Source 搜索命中深链接补齐。参考原 `ShaderViewerWindow.cs:207-211, 376-394` 的 CodeSearch 状态在切换视图时保持、命中行高亮的语义，`SourceView` 的命中游标从组件内部 state 上移为 `matchPosition`/`onSelectMatch`；Variant 写入 `qhit`，搜索词变化时清除，Upload 使用本地 state 重置；Enter/Shift+Enter、F3/Shift+F3、Previous/Next 仍循环导航，命中提示显示 `k / n`，越界 clamp。验证：`npm run build` 77 modules；真实 DXBC `q=float` 为 2 条命中，首条 L17 `1 / 2`，Next 后 URL `qhit=2`、L34，直接打开 `qhit=2` 恢复 L34，`qhit=1` 下 Previous 回绕到 `2 / 2`；改为 `ALPHA_CLIP` 后 `qhit` 移除、显示 `1 / 1` L27，Enter 写入 `qhit=1`；Upload detail 的 Source 搜索同样从 `1 / 2` 导航到 `2 / 2` 且不写 URL；`npm run smoke` 通过。未提交。
- 2026-11-26：离线 source 文件上传补齐。参考原 `ShaderViewerWindow.cs:79-104` 的 `ShowReportFromFile` 与 `:489-496` 的 `UpdateViewWithTextFile`（外部报告打开后连同源码上下文展示），Upload 现在在 detail 加载后可再拖入/选择与 `detail.source` basename 同名的源码文件；`UploadProvider` 保存 `sourceText`/`sourceName`，SourceView、AssemblyList、RegisterTimeline 优先使用该文本，不再请求本地服务，Source 的首写/行 tooltip 与 Source 页因此完全离线。source 名不匹配、空文件、超过 64 MB 或未先加载 detail 均显示明确错误且保留旧内容；替换 detail/summary 或 Clear 时清除本地 source。验证：`npm run build` 77 modules；合成 detail `shaders/passes/local_only.fs.sc` 初始工具提示为 `source L2 (outside ...)`，Source 显示 0 行；全局拖入同名 `local_only.fs.sc` 后 meta 显示 `local source: local_only.fs.sc`、Source 5 行、Assembly tooltip 出现 `source local_only.fs.sc:2 — …`、搜索 `LOCAL_SOURCE_TOKEN` 为 `1 / 1`；拖入 `other.fs.sc` 显示名称不匹配错误且旧 source 保留；source 先于 detail 加载显示明确错误；通过 Upload 的 `Load source` 文件选择器同样加载成功；`npm run smoke` 通过。未提交。
- 2026-11-26：Source 行尾寄存器 profile 条补齐。参考原 `ShaderViewerWindow.cs:619-688` code line 的 pressure/alive 方块（宽度按计数缩放）与 `:763-896` 的 live register graph，Source 为每个有映射的源码行增加两条 4px 迷你条：橙色 pressure 与绿色 live lane 数，均按当前文件映射行的最大值缩放，tooltip 显示 `Peak temp/SSA pressure P / max` 与 `Live registers A / max`。两条仅在与 `P<n>`/`A<n>` 对应数据存在时显示，不改变搜索、lane class、跳转和离线 source 行为。验证：`npm run build` 77 modules；合成 detail 两行 pressure 5/10、alive 2/1，DOM 生成 2 个 profile，宽度分别为 20px/40px 与 40px/20px，tooltip 正确；真实 DXBC 无 sourceLine 不显示（预期），SPIR-V debug forward.vs 的映射均超出原始 25 行（仅显示既有警告）；`npm run smoke` 通过。未提交。
- 2026-11-26：Source 选中 lane 持久高亮补齐。参考原 `ShaderViewerWindow.cs:795-819` 点击 lifetime 选中、`:963-1001` 依赖链高亮/清除的语义，Source 接收 `selectedLane`（Variant 的 `reg`、Upload 的 `focusRegister`）：在当前 class 的 `visibleLiveness` 中找该 lane 的区间，把区间覆盖的映射源码行加 `.lane-selected`，并在 live lane rail 的对应色条加 `selected` 描边/tooltip；hover lane 的绿色高亮与 Source 行选中/搜索高亮优先，class 过滤后不匹配的选中 lane 自动隐藏。验证：`npm run build` 77 modules；合成 detail 的 r0 0–2、r1 0–0/2–2 映射 L2–L4，点击 Assembly r0 后切 Source，L2/L3/L4 均 `.lane-selected` 且三个 rail 色条带 selected/tooltip `Selected lane`；改选 r1 后只剩 L2/L4；本地 source 与离线预览不受影响；`npm run smoke` 通过。未提交。
- 2026-11-26：类过滤扩展到 read/write 计数。参考原 `ShaderViewerWindow.cs:134-152, 288-374` 的 VGPR/SGPR 分 tab 构建及 `:763-896` 按 class 遍历寄存器，Assembly/Source 的 `Lane class` 过滤现在不仅作用于 rail/A，也按 class 统计 `R<n>/W<n>`、行 tooltip 和 Assembly R/W 弹出菜单；Source 的读写字串过滤为 `class:index` 前缀，菜单只列当前 class。验证：`npm run build` 77 modules；混合 r/x fixture 默认 Assembly/Source 均为 R2/W2，切到 x 后均为 R1/W1，Assembly 菜单只剩 `x0`，行 tooltip 只剩 `read: x0x / write: x1x`；切回 All 恢复 R2/W2；`npm run smoke` 通过。未提交。
- 2026-11-26：tab 切换保留滚动位置。参考原 `ShaderViewerWindow.cs:243-247` 为 VGPR/SGPR Assembly/CBuffer 分别持有 ScrollView、`:288-374` 仅在 `childCount == 0` 时首次构建且切换不重建，Variant/Upload 现在记录访问过的 tab，首次进入才挂载，之后用 `hidden` 保留组件；切回 Assembly/Source/Registers/CBuffer 时滚动位置、展开状态以及虚拟列表/Canvas 实例不变。验证：`npm run build` 77 modules；真实 DXBC Assembly `scrollTop=1800` 经 Registers 往返后仍为 1800，Source 670 经 CBuffer 往返后仍 670，CBuffer table 200 往返后仍 200；返回 Registers 时 canvas width 661、非活动 tab 处于 hidden；上传 160 条指令临时 detail 的 Assembly 800 与 Source 500 同样保留；临时上传数据已清理；`npm run smoke` 通过。未提交。
- 2026-11-26：tab 栏键盘与 ARIA 补齐。参考原 `ShaderViewerWindow.cs:118-145` 每个 Tab 的 `selected` 回调统一更新 `currentlySelectedTab` 并调用 `UpdateSelectedTabView`，Web 的 Variant/Upload tab 现在保持单一活动项、活动项为唯一 tab stop，支持 `←`/`→`/`Home`/`End` 循环切换，切换后同步 URL（Variant）并聚焦新 tab；每个 tab 增加 `aria-controls`，懒挂载后的面板增加 `role=tabpanel`/`aria-labelledby`，隐藏面板保持 `hidden`。验证：`npm run build` 77 modules；真实 DXBC 上 Assembly→ArrowRight 后 active/URL 为 registers、焦点 `variant-tab-registers`，End 到 source、Home 到 overview、overview ArrowLeft 回绕到 source；活动 tab 的 tabIndex 为 0、其余为 -1，面板 role/label/hidden 正确；上传临时 detail 上 Assembly→ArrowRight→ArrowLeft 同步切换并聚焦；`npm run smoke` 通过。未提交。
- 2026-11-26：Assembly 源码上下文补齐。参考原 `ShaderViewerWindow.cs:526-539` 的 code line label 与 `:690-756` 在 assembly 行前输出 code line 文本的布局，Web Assembly 新增可选 `Source context`（Variant 写 `asmcontext=1`，Upload 为本地开关）：按当前过滤后的指令序列，在每条 `sourceLine` 的首条映射指令后显示单行源码文本，重复映射行不重复显示，无源码/越界行不显示；文本可省略并带 `文件名:行号 — 源码` tooltip，不改变行高/虚拟滚动。验证：`npm run build` 77 modules；上传 6 条指令（sourceLine 2,2,3,null,4,4）勾选后恰好 3 个 context，依次为 line2/line3/line4，第二行和第六行不重复，关闭后为 0；Variant 上开关写入/移除 `asmcontext=1`；`npm run smoke` 通过。未提交。
