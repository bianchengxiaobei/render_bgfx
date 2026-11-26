# Shader Analysis 使用说明

本文说明 hdrp-bgfx 的 C++ Shader 分析工具和本地 Web 浏览器。规划、原 Unity
映射与阶段记录见 [`shader-analysis-plan.md`](shader-analysis-plan.md)。

## 1. 目标与差异

工具用 bgfx `shaderc` 编译 `shaders/variants.json` 中的 DXBC / SPIR-V 变体，输出：

- 程序/变体摘要 JSON；
- 按变体懒加载的汇编、生命周期、资源、cbuffer 明细；
- 可整树重建的 `index.json`；
- diff JSON 与 23 列 CSV；
- 可供 Web 浏览的静态报告树。

与 Unity 原工具的关键差异：

| Unity | hdrp-bgfx |
| --- | --- |
| PS4/PS5 GCN `VGPR`/`SGPR` | DXBC temp `r#` / SPIR-V SSA value |
| 以 pass 为 program | 以源文件 + stage + backend 为 program |
| `Library/ShaderPerformanceReports` | `shader_analysis/reports` |
| EditorPrefs reference | `shader_analysis/reference` 目录 |
| 编辑器窗口 | 本地 Vite + React 页面 |
| 原始寄存器/占用模型 | 线性静态估算，不模拟 GPU occupancy |

因此 UI 中所有寄存器压力、峰值和 live interval 都是“估算值”。`n/a` 表示该后端
不适用，绝不用 0 冒充。

## 2. 构建与编译

```powershell
cmake --preset vs2022 -DHBG_BUILD_SHADER_ANALYSIS=ON -DHBG_BUILD_SHADER_TOOLS=OFF
cmake --build --preset vs2022-debug --target hbg_shader_analysis
```

最小示例：

```powershell
.\build\vs2022\Debug\hbg_shader_analysis.exe build `
  --root . `
  --variants shaders\variants.json `
  --backend all `
  --pass forward `
  --jobs 8 `
  --out shader_analysis\reports
```

常用过滤：

```powershell
--pass forward                 # include；也支持 +forward,-forward 和 * 通配
--stage fragment               # vertex|fragment|compute
--variant-filter "A&B|C"       # 任一组全部关键字命中即选中
--define ALPHA_CLIP=1          # 精确 define 过滤，可重复
--debug-info                   # SPIR-V 加 debug，使用独立缓存并关闭优化
--progress json                # 机器可读进度
```

`build` 结束后自动扫描报告树并重建 `index.json`。12 位 hash 的变体明细 JSON
默认被 `.gitignore` 忽略；reference 基线只保留摘要。

`shaders/variants.json` 现在还包含 `passes` 槽位映射；CMake `hbg_shaders` 使用
`--execute-plan` 从 JSON 编译并写 `shaders/manifest.json`；普通 Python 构建在计划文件存在时默认使用该模式，`--no-plan` 回退旧调度：

```powershell
py -3 tools/shader_build.py --execute-plan shaders/variants.json `
  --shaderc <shaderc> --bgfx-src <bgfx-src>
```

修改 shader/变体后必须先重新导出并校验计划：

```powershell
py -3 tools/shader_build.py --dump-plan shaders/variants.json
py -3 tools/shader_build.py --plan-check shaders/variants.json
```

## 3. 报告结构

```text
shader_analysis/
  reports/
    index.json
    dxbc/passes/forward.fs.json
    dxbc/passes/forward.fs/<variantId>.json
    spirv/passes/forward.fs/<variantId>-debug.json
  reference/
    index.json
    dxbc/passes/forward.fs.json
    spirv/passes/forward.fs.json
```

摘要包含源文件哈希、backend/profile/optimization/debug、shaderc 版本、变体列表和
metrics。明细按需读取，包含：

- `instructions`：opcode、文本、read/write/resource refs、cbuffer read、pressure、
  branch 缩进/目标、`sourceLine`；
- `liveness`：`class`/`index` 的 `[start,end]` 区间；
- `resources`：kind/name/binding/size；
- `cbufferReads`：instruction/slot/offset/dwords/dynamic。

完整字段示例见规划文档 5.4。

## 4. 指标口径

### DXBC

- `instructionCount`：反汇编指令数；
- `tempRegisterMax`：出现的 temp `r#` 最大值；
- `tempRegisterPeak`：线性扫描的峰值存活 temp 数；
- `cbufferDwordsRead`：常量缓冲读取 DWORD 总数；
- `dynamicCBufferReadCount`：含动态索引的读取次数；
- `textureCount`/`samplerCount`/`uavCount`/`structuredBufferCount`：资源声明统计；
- `branchCount`/`loopCount`：结构化分支/循环计数。
- `ssaValueCount`、`maxLiveSsaValues`：DXBC 不适用，输出 `null`。

### SPIR-V

- `instructionCount`：反汇编 `Op*` 指令数（含 debug 指令时可能与优化版本不同）；
- `ssaValueCount`：SSA result id 数；
- `maxLiveSsaValues`：线性 SSA 存活区间峰值；
- `imageCount`/`samplerCount`/`uniformBufferCount`/`storageBufferCount`；
- `uniformBufferBytes`/`pushConstantBytes`；
- `accessChainCount`/`loadCount`/`storeCount`/`branchCount`/`loopCount`；
- `sourceLineCount`：带 `sourceLine` 的指令数；
- `tempRegisterMax`/`tempRegisterPeak`：SPIR-V 不适用，输出 `null`。

所有指标来自报告；不要和 D3D 编译器实际寄存器分配或 GPU occupancy 混用。

## 5. Web 浏览

```powershell
cd tools\shader_analysis\web
npm install
npm run build
cd ..\..\..
node tools\shader_analysis\serve.mjs --root .
```

打开 `http://127.0.0.1:8787/web/`。

页面：

- **Index**：backend/source 搜索、backend 过滤、变体/失败统计；
- **Program**：变体表、defines 过滤（`A&B|C`）、列排序、CSV 下载、源码链接；
- **Variant**：
  - **Overview**：指标（含 DXBC sample/load/store 与 SPIR-V load/store/push constant bytes）、opcode 直方图、资源、日志；页头显示 profile、optimization 和 shaderc 版本；
  - **Assembly**：虚拟化指令列表、opcode 高亮、分支缩进、跳转竖线/hover 区间、cbuffer 注释；跳转线 tooltip 显示 branch label、起止指令文本和源行范围，点击或 Enter/Space 选中目标指令并滚动；行 tooltip 在 `sourceLine` 可映射时附带源码文本，越界时显示 `outside <file>`；搜索默认高亮匹配行，可勾选 `Filter matches` 只显示命中行，搜索词与过滤状态写入 `asmq`/`asmfilter` 深链接；搜索框支持 Enter/Shift+Enter/F3 与 `Prev`/`Next` 在同组命中中循环，当前命中行加粗并在 toolbar 显示 `k / n`，`asmhit` 持久化当前命中序号；可勾选 `Source context`（`asmcontext=1`）在每条源码行的首组映射指令后显示该行源码文本，重复映射行只显示一次；单击行选中并在 Registers 页聚焦该指令，双击跳源码行；每行右侧显示去重后的 read/write 计数与相对峰值缩放的 live pressure 条（DXBC temp / SPIR-V SSA），并从 `liveness` 聚合该指令的 live lane，通过 `Lane class` 下拉可按 `r`/`x`/`ssa`/`resource` 过滤 rail，R/W 计数和寄存器菜单也限制到该类，显示最多 16 条按 `class:index` 着色的色条（顶部红=write、底部蓝=read；hover 列 lane/读写、生命周期区间与首写指令/源码行，并高亮该 lane 仍存活的可见 Assembly 行，点击跳 Registers 选中；URL `reg` 选中的 lane 在 Assembly 中保持色条描边和存活行高亮，依赖链 lane 加橙色描边、无关 lane 变暗，工具栏显示相关 lane 数；超出显示 `+N`，菜单列出隐藏 lane 并可 hover 高亮/选择）；点击 `R<n>/W<n>` 弹出具体寄存器列表，选择后跳 Registers 并选中对应 lane；
  - **Registers**：Canvas 生命周期/读写/压力条，hover 显示当前指令、pressure 和最近写入指令；顶部压力曲线 hover 显示该处 pressure/opcode/instruction；点击
    lane 高亮依赖链，选择同步写入 `reg`，Escape/Clear 清除选择并从 URL 移除 `reg`；class 下拉写入 `regclass`，同时选择 class 会清除旧 `reg`，sort 下拉可切换 `longest lifetime` / `register index`（后者写入 `regsort=index`），lane 超过 72 条时分页（`regpage`）并双击跳到 Assembly；hover lane 显示最近写入指令与写入源码行文本；选中 lane 后显示首条写入指令（可跳 Assembly）、写入文本、源码行文本和 `Source L<n>` 入口；从 Assembly 单击选中指令后，
    页面显示橙色指令游标、该时刻 live lane、read/write 计数、opcode/文本，并在有 `sourceLine` 时提供 `Source L<n>` 按钮；
  - **CBuffer**：Table/Read order 两种视图（Read order 写入 `cbview=order`）；Read order 按 source line 分组（无映射按 instruction），显示原风格 `Read N DWORD at offset X.` / `Read N DWORD scalar offset.`，并保留 offset 顺序色；按 slot 的 offset 顺序；递增绿色、乱序红色、动态蓝色；每条读取显示
    所属指令的 source line，点击 `L<n>` 跳 Source；来自 Source 的 `CB<n>` 会带 `cbline` 聚焦该行读取并高亮，可用 `Show all` 清除；没有 debug 映射时显示破折号；
  - **Source**：Prism 语法高亮、行号、`sourceLine` 联动、源码行搜索；单击行写入
    `line` 深链接并高亮，双击复制 `path:line`；有 `sourceLine` 映射的行显示 `asm <n>`：单条点击直达 Assembly，多条点击展开该行的指令列表再选择，Escape 收起列表；同时显示该行映射指令的峰值 `P<n>` 压力（DXBC temp / SPIR-V SSA；点击在 Registers 页聚焦峰值指令），并在行尾用双色迷你条显示相对全局最大值的压力（橙）与 live lane 数（绿）；与去重后的 `R<n>/W<n>` 读写寄存器数（tooltip 列寄存器，点击在 Registers 页聚焦首条映射指令）、可按 `Lane class` 过滤 `A<n>`、lane rail 与 R/W 计数，显示映射指令处最大 live 寄存器数 `A<n>` 与彩色 live lane rail（按 class + 数值 index 排序，最多 12 条，超出部分通过 `+N` 菜单列出并可 hover/选中；tooltip 列出 lane 及各自 instruction 区间；hover lane 高亮该生命周期覆盖的源码行，点击色条在 Registers 页选中该 lane 并聚焦该源码行首条映射指令；Registers/Assembly 已选中的 lane（`reg`）在 Source 中保持色条描边，并高亮其 lifetime 覆盖的源码行，hover 绿色高亮优先；点击 `A<n>` 聚焦该行首条映射指令）；有 cbuffer 读取的行显示 `CB<n>`，hover 列出 slot/offset/dwords，点击切到 CBuffer 页并聚焦该源码行的读取；hover 提示列出该行映射的汇编指令（最多 10 条）、分支目标和 `文件名:行号`；映射出的源码行 branch 会按缩进层在 40px jump gutter 中绘制 source jump 竖线，hover 时高亮起止源码范围并把该竖线提到最前，点击跳转到目标行并写入 `line`；搜索词写入
    `q` 深链接，Enter/Shift+Enter、F3 或 Previous/Next 在匹配行间导航，当前命中序号写入 `qhit`，命中提示显示 `k / n`；
- **Upload**：任意页面拖入或通过顶部 `Upload` 打开 variant detail 或 report summary JSON；detail 可查看 Assembly/Registers/CBuffer/Source，summary 显示来源、构建信息和变体指标表；detail 加载后还可拖入/选择与 `detail.source` 同名的源码文件（如 `.sc`/`.hlsl`），Source 与 Assembly/Registers 的源码预览直接使用本地文本，不再依赖本地服务；index JSON 暂不支持；文件不会上传到服务器，刷新后需重新拖入。
- **Tab 切换**：访问过的 Variant/Upload tab 首次进入后保持挂载（同原 Viewer 的独立 ScrollView），切回时保留 Assembly/Source/Registers/CBuffer 的滚动位置与展开状态；活动 tab 是唯一的 tab stop，支持 `←`/`→`/`Home`/`End` 切换并同步 URL，面板使用 `role=tabpanel`/`aria-labelledby`，隐藏 tab 不进入可访问性树。
- **Diff**：指标 delta、新增/删除变体、单变体汇编 diff；reference 需有明细才能做
  Assembly diff。

### 深链接

- Program：`/web/program?dataset=reports&summary=<summaryPath>&source=<source>`
- Variant：`/web/variant?dataset=reports&summary=<summaryPath>&source=<source>&variantId=<id>&tab=assembly`
- 变体 tab：`overview`、`assembly`、`registers`、`cbuffer`、`source`
- Registers class：`&regclass=<class>`（手动切换下拉写入；同一 URL 带 `reg` 时以 `reg` 的 class 为准）
- Registers lane 选中：`&reg=<class:index>`（Source lane 色条、Assembly R/W 菜单或 canvas 点击写入）
- Registers 排序：`&regsort=index`（寄存器编号升序；缺省按生命周期长度降序）
- Registers 分页：`&regpage=2`（从 1 开始；每页 72 条，缺省第 1 页）
- Lane class 过滤：`&laneclass=x`（Assembly rail/R/W 计数与 Source A/rail/R/W 计数；缺省显示全部 class）
- Assembly 搜索命中：`&asmhit=2`（1 开始；与 `asmq` 配合，越界自动 clamp）
- Assembly 行选中/Registers 聚焦：`&instruction=<index>`（跨 tab 保留；Registers 页可直接打开 `&tab=registers&instruction=<index>`）
- Assembly 搜索：`&tab=assembly&asmq=<text>`；过滤命中行时加 `&asmfilter=matches`；源码上下文加 `&asmcontext=1`
- Source 行：`&tab=source&line=<line>`
- Source 搜索：`&tab=source&q=<text>`（可同时带 `line`）
- Source 搜索命中：`&qhit=2`（1 开始；与 `q` 配合，越界自动 clamp）
- CBuffer 源码行聚焦：`&tab=cbuffer&cbline=<line>`（Source 的 `CB<n>` 写入）
- CBuffer 视图：`&tab=cbuffer&cbview=order`（Read order；Table 时省略）
- Upload：`/web/upload`（页面本身可打开；detail 数据保存在当前页面内存中，刷新清空）
- Diff：`/web/diff?diff=<path>&current=<root>&reference=<root>&compare=<variantId>`

刷新后仍由 URL query 还原页面状态。

## 6. Diff、CSV 与 Reference

```powershell
.\build\vs2022\Debug\hbg_shader_analysis.exe diff `
  shader_analysis\reports shader_analysis\reference `
  --out shader_analysis\reports\diff.json

.\build\vs2022\Debug\hbg_shader_analysis.exe export `
  --report shader_analysis\reports --format csv `
  --out shader_analysis\reports\report.csv

.\build\vs2022\Debug\hbg_shader_analysis.exe set-reference `
  --from shader_analysis\reports --to shader_analysis\reference
```

- program 匹配 `source + stage + backend`；
- variant 匹配 canonicalDefines 的 SHA-256 短 hash；
- `compileMs`、`cacheHit`、`generatedAt` 不参与比较；
- `sourceHashChanged` / `toolchainChanged` 将匹配项标为 `incomparable`；
- `--fail-on-regression` 对 `binaryBytes`、`instructionCount`、
  `tempRegisterMax`、`tempRegisterPeak`、`ssaValueCount`、
  `maxLiveSsaValues`、`cbufferDwordsRead` 的正 delta 返回 1；
- CSV 23 列，字段以 `;` 分隔、双引号包裹、CRLF 行；Defines 以 CRLF 连接。

`set-reference` 会清空目标、只复制摘要并清除本地 binary/disasm/detail 路径，因此
reference 不包含汇编明细。做单变体汇编 diff 时，把 `current` / `reference` 指向
两个包含明细的报告目录。

## 7. Debug 与源码行

SPIR-V `--debug-info` 产物会：

- 使用 `<variantId>-debug` 缓存目录；
- 可能关闭优化，指标不可与 release 直接比较；
- 在 Web Variant 顶部显示 Debug 警告；
- 部分 `sourceLine` 可能指向预处理后的行号，超出原始 `.sc` 文件行数时 Source
  页会显示警告而不是错误高亮。

DXBC v1 没有源码行映射，Assembly 与 Source 分开查看。

## 8. 本地服务安全

`serve.mjs`：

- 仅接受 GET/HEAD；
- `/web/*` 只暴露 `web/dist`；
- `/shader_analysis/*`、`/shaders/*` 限制在 `--root` 内；
- 拒绝 `..`、编码穿越和根目录逃逸；
- 设置 JSON/JS/CSS/SC/bin/disasm MIME 与 `nosniff`；
- 不提供写接口，不执行客户端请求中的命令。

不要把该服务暴露到公网。

## 9. 测试与限制

```powershell
ctest --test-dir build/vs2022 -C Debug -R shader_analysis --output-on-failure
cd tools\shader_analysis\web
npm run typecheck
npm run build
npm run smoke
```

`npm run smoke` 会启动临时 `serve.mjs`，检查报告/源码/SPA/路径穿越/POST，并在找到
Chrome/Edge 时用 headless dump-dom 验证 Index、Program、Source 搜索和 Assembly
页面；`npm run smoke:browser` 强制要求浏览器。`CHROME_PATH` 可覆盖浏览器路径。

已知限制：

- 生命周期是线性反向扫描，不是 CFG 精确分析；
- SPIR-V SSA lane 数量可能很大，Registers 页每页显示 72 条，支持分页（`regpage`）、
  class 切换和寄存器搜索；被选中的 lane 若超出上限会固定在首行，保证 Source
  色条跳转后仍能看到选中项；
- 优化 fragment 的 detail 可达十几 MB，Variant 首次打开需要等待；
- 报告 schema 和指标以 C++ `report.h` / 规划文档为准；
- 上传的 detail JSON 和可选源码文件只保存在当前浏览器内存中，刷新/关闭页面后清空；source 文件名必须与 `detail.source` 的文件名一致；
- Web 仅本地使用；已提供 `npm run smoke`，尚未接入仓库 CI 门禁。
