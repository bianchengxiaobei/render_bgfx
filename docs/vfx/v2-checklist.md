# VFX 编辑器 V2 清单

> 状态：V2.0（文档模型/布局）、V2.1（ImGui 节点视图/Inspector）、V2.2（结构 authoring/undo/文本输入/Curve-Gradient）、V2.3（save→compile→error focus→hot reload）、V2.4（playback/preview/diagnostics）、V2.2b 收尾（clipboard/subgraph/expression/optional/recovery/close-confirm）与 V2.5（28 资产 round-trip/双后端截图/no-VFX 0-diff/文档）已完成。
> 原则：编辑器只消费 `.vfx.json`，保存后仍由 `vfxbake` 产出 `.vfxb`；`.vfxb` binary、既有 runtime API 语义、shader pack stage 契约不变；V2.4 仅新增 additive `Effect::advanceOneFrame()`。
> 参考：先读 Unity VFX Graph 编辑器源码，再实现；本清单记录实际 `file:line`。

---

## 0. 决策

| 项目 | 决策 | 依据 |
|---|---|---|
| UI 技术 | 复用 hdrp-bgfx 的 Dear ImGui，自绘节点画布；不引入 Qt | V1 已依赖 `hbg_imgui`，DebugUI 已有 ImGui 注入/提交；避免新增桌面框架 |
| 图数据 | 编辑原始 `.vfx.json`（`ordered_json` 保留字段顺序），每次编辑后用 `parseGraphJson()` 验证；失败回滚 | 不复制 Unity ScriptableObject 序列化；V1 JSON 已是唯一图契约 |
| 编辑器布局 | 独立 sidecar：`<asset>.editor.json`（如 `spark.vfx.editor.json`），保存 context 位置、折叠、pan/zoom、graph hash | Unity 把 `VFXUI` 存在图资产内（`Editor/Models/VFXGraph.cs:529-544`、`Editor/Models/VFXUI.cs:33-106`）；本项目为保持 `.vfx.json`/`.vfxb` 不变，使用 sidecar，并记录 asset hash 检测陈旧布局 |
| 节点身份 | `systems/<i>/spawner|init|update` 与 `systems/<i>/outputs/<j>` | 与 V1 JSON 路径一致，不新增运行时 ID |
| 未保存处理 | 关闭窗口时若有 graph/layout 修改，先拦截 close 并显示 Save/Cancel/Discard 模态框；Save 写 graph + layout，Discard 清 recovery 后退出，Cancel 保持窗口；默认 `On Save` 编译，也可手动 `Compile`；V2.2 已补 undo/redo | Unity `VFXView.cs:188-207` 用 `DisplayDialogComplex("Save","Cancel","Discard Changes")`，Cancel 不关闭；本项目按同样三分支，并保留独立 recovery snapshot |
| 不支持 | `.vfx` 导入/导出、GPU instancing、子图上下文、透明 DrawList 统一排序 | 与 `v2-editor-outline.md` 边界一致；V1 D8 结论不变 |

---

## 1. V2.0 文档模型与布局（完成）

- [x] `vfx/include/vfx/editor.h`：`ContextKind`、`ContextRef`、`NodeLayout`、`ViewLayout`、`Document` 稳定 API；不暴露 nlohmann/ImGui/bgfx 类型。
- [x] `vfx/src/editor/document.cpp`：
  - 加载 `.vfx.json`，同时用 `vfx::parseGraphJson()` 做完整 V1 校验；
  - 使用 `nlohmann::ordered_json` 保留字段顺序；
  - `setSystemCapacity/Space/Sort/Bounds`、`setSpawnerRate`、`setBlockEnabled`、output lit/blend/soft/metallic/roughness、property 数值编辑；
  - 每次修改先备份 JSON，重新 parse；失败恢复旧 JSON 并保留结构化 `ParseError`；
  - `setSystemSort` 同步 output `sort` 字段，保持 V1 parser 的 “system 排序开启时 output 必须声明 sort” 不变量；
  - layout sidecar 使用 `assetHash`（`graphHash()`）检测陈旧；缺失/损坏 sidecar 不阻塞图加载；
  - 默认布局按 system 列排布 Spawn/Init/Update/Output。
- [x] `vfx/tests/test_vfx_editor.cpp`：context path、加载、非法编辑回滚、typed 编辑、layout round-trip、损坏 layout、default layout。
- [x] 未修改 `.vfx.json` schema、`.vfxb` binary version、runtime API 或 shader pack。

参考：

- `Editor/Models/VFXGraph.cs:529-544`（`m_UIInfos` 作为图序列化字段）、`Editor/Models/VFXGraph.cs:936-939`（重新启用时 `UIInfos.Sanitize`）。
- `Editor/Models/VFXUI.cs:33-106`（`UIInfo.position`、system context 关联、group/sticky note）、`:108-132`（sanitize 失效 UI 节点）。
- `Editor/GraphView/Views/Controller/VFXGraphValidation.cs:25-46`（按 children 校验 model，再校验 slot link）；本项目用 `parseGraphJson()` + `loadGraphFile()` 保留 JSON/HLSL 行列。
- `Editor/Models/VFXErrorManager.cs:20-49`（`RefreshCompilationReport`/`GenerateErrors`）、`:94-130`（按 model 注册、去重、dirty 标记）；本项目映射为 `ParseError` 路径 + 行列。

验证：

- `hbg_vfx_tests`：**85/85, 38172 assertions**（含 18 个 editor case / 415 assertions）。
- `hbg_vfx_editor_d3d11` / `hbg_vfx_editor_vulkan` smoke：加载图、画 UI、3 帧退出。
- V2.2b/editor 目标测试 **5/5**；全量 CTest 受工作区既有 config/pipeline.json/renderer 状态影响，未作为本切片门禁（历史日志 `build\vs2022\v143_v22b_final_ctest6.log`）；`hbg_vfx_tests` **85/85, 38172**；`hbg_tests` **CTest passed（direct 279 cases 有 1 个非 VFX 既有失败）**；`hbg_vfx_compiler_tests` **22/22, 347**；`hbg_vfx_runtime_tests` **19/19, 494**；`hbg_vfx_adapter_tests` **7/7, 328**；IDL check 通过。
- 未产生任何 checked-in `*.editor.json`；只有显式 Save 才写 sidecar。

Known limits：

- sidecar 按 JSON 路径索引；外部重排 blocks 后位置会跟随索引，hash 会提示 stale。
- V2.0 初始版本无 add/remove/undo/text input；这些已由 V2.2 补齐，见 §3。
- graph 修改后 layout hash 会 stale，保存时一并刷新。

---

## 2. V2.1 ImGui 节点视图与 Inspector（完成）

- [x] `--vfx-editor=<effect.vfx.json>` CLI；必须配 `--scene=<scene.gltf>`，复用现有 renderer 背景。
- [x] `src/app/vfx_editor_ui.{h,cpp}`：自绘 context 卡、Spawn→Init→Update→Output 连线、选中高亮、双击折叠、拖动节点、MMB/右键平移、滚轮缩放。
- [x] Inspector：
  - Blackboard 属性列表；float/int/bool/vector/color 数值编辑；
  - system capacity/space/sort/bounds/spawn rate；
  - init/update block 启用/禁用；参数只读 JSON（V2.2 改为结构化编辑）；
  - output lit/blend/soft/softDistance/metallic/roughness；
  - 错误面板显示 `ParseError` 路径/行列与 sidecar 错误。
- [x] `Save` 同时写 `.vfx.json` 与 `.vfx.editor.json`；`Reset Layout` 恢复默认布局。
- [x] `DebugUI::setPanelVisible(false)` 在编辑器模式隐藏调试面板；F1 仍可临时打开。
- [x] Editor UI 使用 DebugUI 已初始化的 ImGui context，不新增 render pass、不新增 sampler。
- [x] `HBG_BUILD_VFX=OFF` 时使用 `vfx_editor_ui_stub.cpp`，`--vfx-editor` 返回明确错误，不破坏原构建。

参考：

- `Editor/GraphView/Views/VFXView.cs:575-611`（`AddNode` 选择 operator/context/parameter 并走 controller）、`Editor/GraphView/Views/Controller/VFXViewController.cs:1274-1304`（节点 controller 同步）。
- `Editor/GraphView/Elements/VFXContextUI.cs:751-775`（根据鼠标位置计算 block 插入 index）；V2.1 只选择/折叠，插入留 V2.2。
- `Editor/GraphView/Views/VFXView.cs:1977-2004`（`CompileAndUpdateAsset`/`OnSave`）；V2.3 已接 Save/Compile。
- `Editor/GraphView/VFXViewWindow.cs:395-443`（dirty → auto compile → refresh errors）；V2.3 已接编译/error focus。

验证：

- D3D11/Vulkan 1280×720 截图：`build\vs2022\vfx_editor_smoke_e.png`、`vfx_editor_smoke_vk.png`（本地验证产物，不提交）。
- `hbg_vfx_editor_d3d11` / `hbg_vfx_editor_vulkan` CTest 通过。
- 未新增 render graph pass；`render performance`/VFX 指标与 V1 相同。

Known limits：

- V2.1 初始版本无 add/remove、undo、text input；这些已由 V2.2 补齐。
- V2.1 初始版本 block 参数/Curve/Gradient 只读；V2.2 已补。
- V2.2b 已补 block/property clipboard、subgraph invocation、expression JSON、optional block 参数、recovery 与关闭确认。
- V2.3 已有 Compile/On Save/hot reload；play/pause/step/replay 与独立 preview 已在 V2.4 补齐。
- 不支持窗口 docking/多 graph tab；窗口位置使用 first-use 默认。
- 节点行宽固定 270 world px；长 block 名会被固定文本缓冲截断（显示层限制，不影响数据）。

---

## 3. V2.2 结构 authoring、undo、文本输入与 Curve/Gradient（完成；V2.2b 完成）

- [x] `Document` 结构 API：`addSystem`/`removeSystem`/`renameSystem`、`addBlock`/`removeBlock`/`moveBlock`、`addOutput`/`removeOutput`、`addProperty`/`removeProperty`。
- [x] 默认参数来自共享 V1 block catalog：新增 `vfx/src/core/block_catalog.{h,cpp}`，parser 的 field 表与 editor 的默认 JSON 共用同一份 `detail::BlockSpec`；不在 editor 手写第二份 block 名/字段语义。
- [x] Block 参数编辑：number/bool/string；数值数组逐 component；custom HLSL `code` 多行输入；整数 JSON 字段保持整数；失败经 `parseGraphJson()` 回滚并显示错误。
- [x] Output `texture` 改为 texture property 下拉；property texture default path 可编辑；output add/remove。
- [x] Undo/redo：snapshot `root + dirty + layoutStale`，最多 128 条；同 edit key 在 500ms 内合并（drag 不会每帧压栈）；restore 后重建 graph/layout contexts；非法编辑不产生 history。
- [x] 文本输入：GLFW `key callback` + `char callback` → `WindowInput`；`DebugUI` 映射 GLFW key 到 `ImGuiKey` 并调用 `AddInputCharacter`；system name/new property/block string/texture path 可用键盘编辑。
- [x] 快捷键：`Ctrl+Z` undo，`Ctrl+Y`/`Ctrl+Shift+Z` redo；toolbar 增加 Undo/Redo/Add System。
- [x] Curve/Gradient：key add/set/remove；新增 key 自动选 `[0,1]` 内未占用时间；texture slot/错误面板沿用 Inspector。
- [x] Node/Block 删除：block 上/下移动；block 内 enabled/参数编辑；system/output/property 删除前由 parser 校验引用，引用未清除时回滚。
- [x] `subgraph` block 插入：`Document::addSubgraphBlock()` 从 raw 子图定义的 input `default` 生成 `args`，校验 context 兼容性；UI 提供兼容子图下拉。`trigger_event` 需 system emit event，over-life 需对应 gradient/curve property，否则返回错误。
- [x] copy/paste/duplicate：block/property/system/output；block 粘贴到选中 block 之后，或 init/update context 末尾；property 自动生成 identifier 唯一名；原始 JSON 作为 clipboard，可跨文档。支持 toolbar、Inspector 按钮与 `Ctrl+C`/`Ctrl+V`/`Ctrl+D`。
- [x] 子图 invocation 保留：新增 `GraphParseMode::PreserveSubgraphs` 供编辑器使用，`Document` 保留 raw `subgraph` block 与定义；compiler/runtime 仍按默认 flatten 模式，不改变 binary 语义。
- [x] 表达式文本编辑：`Document::blockParameterJson()` / `setBlockParameterJson()`；Inspector 对 `Expression`、`Object`、非数值/长数组显示 raw JSON 多行编辑，非法 JSON/表达式经 parser 回滚并保留 undo history。
- [x] 自动保存/崩溃恢复：`Document::defaultRecoveryPath()` 使用 temp 下稳定 FNV 路径；每个 graph dirty 后在 UI 中 debounce 2s 写 recovery；关闭时 app 再写一次；下次打开弹出 Recover/Discard，Recover 载入后保持 dirty，`Save` 或 Discard 清除。只恢复 graph JSON，不恢复 layout sidecar。
- [x] 可选 block 参数：`Document::optionalBlockParameters()` 列出 block catalog 中缺失且带默认 JSON 的 optional field；`addOptionalBlockParameter()` 经 `parseGraphJson()` 校验后插入并参与 undo。UI 在 Inspector 参数末尾显示 “Optional parameters” 下拉，支持 sphere `surface`、velocity `direction/coneAngle` 等。
- [x] 关闭前阻塞式 dirty 确认：`Window` 暴露 `shouldClose/setShouldClose`，`Application` 在 close request 时不直接退出，而是调用 `VfxEditorUI::requestClose()`；ImGui modal 提供 Save/Cancel/Discard Changes，Cancel 保持窗口，Discard 清 recovery，Save 写 graph + layout 后退出；参考 Unity `VFXView.cs:188-207`。
- [x] 新增 editor test：非法结构回滚、system/block/output/property authoring、block catalog 默认值、undo/redo、Curve/Gradient、texture slot、block/property clipboard、system/output duplicate、subgraph 插入、expression JSON 编辑、optional block 参数。

参考：

- `Editor/GraphView/Elements/VFXContextUI.cs:751-775`（按鼠标位置计算 block 插入 index）、`:786-833`（`OnCreateBlock`/block provider）。
- `Editor/GraphView/Views/VFXView.cs:575-611`（node/variant/子图创建）、`:2546-2560`（`canCopySelection`）、`:2705-2735`（`DuplicateSelectionWithEdges`）、`:3181-3202`（Duplicate/Paste context menu）；`Editor/VisualEffectGraphShortcuts.cs:79-85`（duplicate with link shortcut）。
- `Editor/GraphView/Views/VFXCopy.cs:27-47`（`Copy`/`CopyBlocks`）、`:311-390`（node flags/input slots/block copy）；`Editor/GraphView/Views/VFXPaste.cs:145-234`（block paste target：选中 block 后插入，或选中 context 追加）。
- `Editor/GraphView/Views/Controller/VFXViewController.cs:1274-1304`（node controller 同步）、`Tests/Editor/VisualEffectGraphEditorTests.cs:17-31`（open/add/undo/save 生命周期 sanity）。
- `Editor/Models/Blocks/Implementations/HLSL/CustomHLSL.cs:268-286`（错误注册）；本项目以 `ParseError` + HLSL 行列复用 V1.3 校验。
- `Editor/Models/VFXSubgraphBlock.cs:124-183`、`Editor/Models/VFXSubgraphUtility.cs`（子图资源/属性同步）；本项目只保留 raw invocation，运行时展开语义不变。

验证：

- `hbg_vfx_tests`：**85/85, 38172 assertions**（含 18 个 editor case / 415 assertions）。
- `hbg_vfx_editor_d3d11` / `hbg_vfx_editor_vulkan` smoke：加载图、画 UI、3 帧退出；本地截图 `build\vs2022\vfx_editor_v22.png`。
- V2.2b/editor 目标测试 **5/5**；全量 CTest 受工作区既有 config/pipeline.json/renderer 状态影响，未作为本切片门禁（历史日志 `build\vs2022\v143_v22b_final_ctest6.log`）；`hbg_vfx_tests` **85/85, 38172**；`hbg_tests` **CTest passed（direct 279 cases 有 1 个非 VFX 既有失败）**；`hbg_vfx_compiler_tests` **22/22, 347**；`hbg_vfx_runtime_tests` **19/19, 494**；`hbg_vfx_adapter_tests` **7/7, 328**；IDL check 通过。
- 未产生任何 checked-in `*.editor.json`；只有显式 Save 才写 sidecar。

Known limits：

- Text input 使用 ImGui 默认字体，非 ASCII 可输入但可能无字形；没有 IME 候选窗。
- Clipboard 仅支持单 selection（block/property），不支持多选/context 整卡复制；block 跨 context 粘贴由 parser 校验类型，失败回滚。
- Subgraph 插入只列出目标 stage 有 block 的定义；expression-only 子图仍需作为 expression JSON 手动引用；args 默认值来自 raw `inputs[].default`，缺省时报错。
- 表达式编辑是 raw JSON，不是 Unity slot/operator graph；错误路径仍以 JSON path 为主。
- 可选 block 参数只列出 catalog 中声明了默认 JSON 的 optional field；`reap.condition`、over-life 的 `gradient/curve` 等需要上下文/表达式的字段仍通过 raw JSON 编辑。默认值只是编辑器补齐值，不改变 compiler/runtime 缺省语义。
- 关闭确认的 Save 只写 graph + layout，不触发 On Save 编译；Discard 直接退出并清 recovery；Close 拦截依赖 ImGui/DebugUI 可用。
- 文本字段在焦点离开且控件仍绘制时提交；切换选中节点/关闭窗口可能丢弃未提交文本。
- 新增 block 的默认参数表是编辑器默认值，不改变 compiler/runtime 的缺省语义；compiler 仍按 V1 规则验证。

### V2.2b 收尾（完成）

- `vfx/include/vfx/editor.h` / `vfx/src/editor/document.cpp`：
  - `copyBlock`/`pasteBlock`/`duplicateBlock`：block JSON 原样复制，粘贴 index 由 UI 决定；`pasteBlock` 走完整 `parseGraphJson(..., PreserveSubgraphs)`，非法类型/字段回滚。
  - `copyProperty`/`pasteProperty`/`duplicateProperty`：property JSON 原样复制，新名字按 identifier 规则加数字后缀（如 `Tint1`），避免空格非法。
  - `duplicateSystem`/`duplicateOutput`：复制整个 JSON 对象；system 使用唯一名，output 追加。
  - `addSubgraphBlock`：从 raw `subgraphs[]` 解析目标 stage 和 inputs 默认值，生成 `{"type":"subgraph","name":...,"args":...}`；`subgraphNames(context)` 提供给 UI。
  - `blockParameterJson`/`setBlockParameterJson`：raw JSON 参数编辑；`type` 保留字段不可改，parser 校验失败回滚。
  - `optionalBlockParameters`/`addOptionalBlockParameter`：共享 `detail::BlockSpec::FieldSpec::defaultJson`，给缺失 optional 字段补默认值；默认值同样经 parser 校验并支持 undo。
- 编辑器 raw 图：`GraphParseMode::PreserveSubgraphs` 保留 subgraph 定义与系统里的 invocation，避免 flatten 后 block 索引与 JSON 不一致；compiler 仍使用默认 `FlattenSubgraphs`，`.vfxb` 语义不变。
- UI：toolbar/Inspector 的 Copy/Paste/Duplicate；`Ctrl+C/V/D`；`drawJsonParameter()` 多行 raw JSON；subgraph context 下拉 + `+ Block` 插入；property/system/output duplicate 按钮；Inspector 末尾 “Optional parameters” 下拉。
- 自动保存/崩溃恢复：`Document::defaultRecoveryPath()` 使用 temp + 稳定 FNV 路径；`saveRecovery`/`loadRecovery`/`clearRecovery`；UI dirty 后 2s debounce 写入，启动检测并显示 Recover/Discard modal；app 关闭时再写一次。`Save`/Discard 清除 recovery。布局 sidecar 不进入 recovery。
- 关闭确认：`Window::shouldClose/setShouldClose` 允许 app 拦截 close；`VfxEditorUI::requestClose/closeApproved` + ImGui modal 实现 Save/Cancel/Discard Changes，Cancel 不关窗，Discard 清 recovery，Save 写 graph+layout 后退出。
- 测试：`hbg_vfx_tests` 新增 clipboard/duplicate、subgraph、expression、recovery、optional 五个 editor case；当前 **85/85, 38172**（18 editor / 415）。
- CTest：新增 `hbg_vfx_editor_subgraph_d3d11` / `_vulkan` smoke；V2.2b/editor 目标测试 **5/5**。
- D3D11 screenshot `build\vs2022\vfx_editor_v22b.png`：toolbar 显示 Copy/Paste/Duplicate，Init 节点显示 raw `subgraph` block，Inspector 显示 `Duplicate System` 与 subgraph/block 控件；关闭确认 modal 已本地 smoke 显示（Save/Cancel/Discard Changes 三分支）。

## 4. V2.3 编译/错误/hot reload（完成）

- [x] `Save` 默认先写 graph + layout，再调用 `vfx::compileGraphFile()`（直接链接 `hbg_vfx_compiler`，不依赖 CLI）；显式 `Compile` 在有未保存修改时先保存。输出固定为与 `.vfx.json` 同目录的 `.vfxb`（`vfx::editor::defaultBinaryPath`），先写临时文件再替换，编译失败不覆盖旧 binary。
- [x] Compile options：`--vfx-shaderc` / `--vfx-bgfx-src` / `--vfx-shader-pack`，对应 env `HBG_SHADERC` / `HBG_BGFX_SOURCE_DIR` / `HBG_VFX_SHADER_PACK`；CMake 生成默认值（`HBG_VFX_SHADERC_DEFAULT` 等）。缺少 shaderc/bgfx source 时在 Compile 面板明确报错。
- [x] `CompileResult.errors` 的 `ParseError` 显示 source/line/column/path；点击错误调用 `errorFocus()` 映射到 system/init/update/output/block/property，并自动平移画布居中该 context；raw shaderc stderr 在无结构化 path 时原样显示。
- [x] 写 `.vfxb` 成功后通过 `ForwardRenderer::reloadVfxAsset()` 按路径匹配 scene VFX instance，调用 `Runtime::reloadEffect()`，重新应用 instance properties；失败保留旧 effect。无匹配 scene instance 时编译仍成功，状态显示 hot reload skipped。
- [x] compile/error/reload 状态和 `On Save` 开关在 Inspector 的 `Compile` 面板；`Ctrl+S` 触发 Save（含默认编译）。新增 `--vfx-editor-compile` headless smoke，供 CI 校验编译路径。
- 参考：`Editor/GraphView/Views/VFXView.cs:1977-2004`（`CompileAndUpdateAsset`/`OnSave`）、`Editor/Models/VFXGraph.cs:1664-1674`（`CompileAndUpdateAsset`）、`Editor/GraphView/VFXViewWindow.cs:395-443`（dirty → compile → refresh errors）、V1 `Runtime::reloadEffect`。

Known limits：

- 编译在 UI 线程同步执行；最小图约 6s，复杂图会阻塞窗口，尚未做后台任务/超时取消。
- 只有 `ParseError` 能点击定位；shaderc 原始日志没有 JSON path/HLSL 行映射，显示为 raw text。
- 没有 scene VFX instance 时，V2.3 只验证编译和 `.vfxb` 写入；V2.4 `Preview` 可创建独立 editor-only effect。
- 保存写入仍非严格原子替换（临时文件 + remove/rename）；编译失败不会触碰旧 binary。
- hot reload 只覆盖 scene 已有 VFX instance；headless compile CTest 不启动 runtime，因此状态为 skipped。

验证：

- `hbg_vfx_tests`：**85/85, 38172 assertions**（含 18 个 editor case / 415 assertions）。
- `hbg_vfx_editor_compile` CTest：复制 `minimal_unlit.vfx.json` 到 build 目录，执行 `--vfx-editor-compile` 与 `--vfx-editor-preview`，校验非空 canonical `.vfxb` 且 preview 不修改 graph/canonical binary，**31.72s（本次依赖 shader cache）** 通过。
- D3D11/Vulkan editor smoke 继续通过；`vfx_editor_v23_b.png` 显示 Compile/On Save 面板。
- 临时 `editor_scene.gltf` + `.hdrp.json` 指向复制的 `minimal_unlit.vfxb`；`Ctrl+S` 后日志：`VFX editor compile: ... compiled 36204 bytes in 6158 ms; hot reloaded`，同时生成 `.editor.json`。
- V2.2b/editor 目标测试 **5/5**；`hbg_vfx_editor_compile` 31.72s。

## 5. V2.4 实时预览（完成）

- [x] `Effect::advanceOneFrame()`：暂停时以固定 `1/60s` 仿真一步；`Runtime` 保留 pending/frame 标记，step 帧仍 encode simulation/output，之后继续暂停。
- [x] Playback 面板：Play/Pause、Step、Replay、Time scale（0–4x）；scene seed 固定显示；多实例聚合 status（bounds 为并集）。
- [x] `ForwardRenderer` playback API：`toggleVfxPaused/stepVfx/restartVfx/setVfxPlayRate/vfxPlaybackStatus`，按 `.vfxb` 路径匹配 scene 实例；hot reload 保留 rate/paused/playing/seed 状态。
- [x] Runtime 诊断面板：effects/systems/particles/draws/dispatches/sorts/events + update/sim/output/total ms，消费现有 `FrameDiagnostics`。
- [x] `Preview`：`Document::serialize()` 未保存图 → `compileGraph(json)` → 临时目录 `hbg_vfx_preview_<hash>.vfxb` → `ForwardRenderer::previewVfxAsset()`；不写 graph/layout/canonical `.vfxb`；失败保留旧 effect；无 scene 实例时创建 editor-only effect。
- [x] `--vfx-editor-preview` headless smoke；`hbg_vfx_editor_compile` CTest 同时验证 preview 不修改 graph/canonical binary。
- [x] Editor 模式未显式 `--no-vfx` 时自动启用 VFX runtime；`--no-vfx` 仍可关闭。
- 参考：`Editor/Inspector/VisualEffectEditor.cs:21-44`（`ControlStop/ControlPlayPause/ControlStep/ControlRestart`）、`:507-531`（Scene View 控件）、`:546-564`（play rate 百分比与预设）；`Editor/GraphView/VFXComponentBoard.cs:149-160,390-445`（组件面板接线/按钮/暂停状态）；`Editor/Inspector/VFXAssetEditor.cs:283-303`（preview play/pause + Reinit）；`Editor/GraphView/Profiling/VFXProfilingBoard.cs:13`（采样板）；V1.4.4 `FrameDiagnostics`。

Known limits：

- Bounds 目前在 Inspector 显示 world AABB 数值，尚未画 3D 线框（graph 窗口覆盖 scene，需独立 preview viewport）。
- Preview 编译仍在 UI 线程同步执行；最小图约 6–12s。
- 多个匹配实例共享同一 play/pause/rate/step 命令，不提供逐实例选择。
- Preview-only effect 使用 identity transform/seed 0；scene 实例的 transform/properties 在 preview 时保留。
- Dear ImGui 1.92 动态 font atlas 需在 `DebugUI` 初始化时显式 preload ASCII glyphs；否则部分数字首次绘制会缺字。

验证：

- `hbg_vfx_runtime_tests` **19/19, 494**（新增 pause→step→output 断言）。
- D3D11 editor screenshot `build\vs2022\vfx_editor_v24_final.png` 显示 Playback（Pause/Step/Replay、1.00x）、seed 7、bounds min/max、Runtime 面板。
- `hbg_vfx_editor_compile` 31.72s：canonical compile + preview 均通过，preview 后源 graph 与 canonical `.vfxb` SHA256 不变。
- V2.2b/editor 目标测试 **5/5**。

## 6. V2.5 验收（完成）

- [x] 编辑器 round-trip：新增 `vfx/tests/editor_roundtrip.cpp`（`hbg_vfx_editor_roundtrip`）和 `vfx/tests/run_vfx_editor_roundtrip.cmake`。把 28 个 checked-in `.vfx.json`/`.vfxb` 复制到 build 工作目录，用 `Document::load(..., PreserveSubgraphs)` 加载，执行 `save()` + `saveLayout()`，再重新加载；逐资产比较 `graphHash()` 与序列化 JSON，随后对每个保存后的资产执行 `vfxbake --input ... --output ... --check`（dxbc+spirv），确认编辑器保存不改变语义或 checked-in binary。
- [x] D3D11/Vulkan editor smoke + render 截图：新增 `run_vfx_editor_screenshot.cmake`，加载 `subgraph_visual.vfx.json`，1280×720 渲染 3 帧并校验 PNG 签名/尺寸；D3D11/Vulkan 各自通过。既有 editor/subgraph smoke 继续覆盖 640×360。
- [x] 无新增 runtime pass/validation error；`--no-vfx` 0-diff 保持：`hbg_vfx_empty_{d3d11,vulkan}` 与 `hbg_vfx_order_off_{d3d11,vulkan}` 通过；D3D11 `--bgfx-debug` 编辑器运行 exit 0，日志只有 RenderDoc/AGS/Aftermath 动态库缺失 WARN，无 VFX validation error。
- [x] 文档、CLI usage、README 回填：`docs/vfx/{v2-checklist,v2-editor-outline,README,architecture,source-alignment}.md` 与根 `README.md` 更新；V2.5 验证命令写入 `docs/vfx/README.md`。

参考：

- `Editor/Models/VFXGraph.cs:156-206`（`OnCompileResource` 在 importer 阶段执行 `PrepareGraph(true)` 并生成 asset desc）、`:347-365`（`OnWillSaveAssets` 写回 sub-assets）、`:1664-1674`（`CompileAndUpdateAsset` 刷新编译结果）。
- `Editor/GraphView/Views/VFXView.cs:1977-2004`（`CompileAndUpdateAsset`/`OnSave`）；`Editor/GraphView/VFXViewWindow.cs:395-443`（dirty/import 后重编译/刷新错误）。
- `Editor/Models/VFXUI.cs:108-132`（invalid UI info 的 sanitize）；本项目布局仍在 `.vfx.editor.json` sidecar，不写入 graph。

验证：

- `hbg_vfx_editor_roundtrip` CTest **passed**：**28** 个资产，**647.84s**；每资产 `vfxbake --check` 通过（dxbc+spirv）。本次 `sort_visual` 遇到一次 shaderc 失败（本机同时有并发 CTest/shader 编译负载），测试脚本对单资产重试一次后通过；不修改 production compiler。
- `hbg_vfx_editor_screenshot_{d3d11,vulkan}` **2/2**，**5.46s**；截图 `build\vs2022\vfx\vfx_editor_screenshot_d3d11.png` / `_vulkan.png`，均为 **1280×720**。
- editor/subgraph smoke 与 `--no-vfx` 0-diff：**8/8**，**37.44s**。
- `hbg_vfx_tests` **85/85, 38,172 assertions**（18 editor cases / 415）；D3D11 `--bgfx-debug` 编辑器日志 `build\vs2022\v25_editor_validation_d3d11.log` 无 validation error。
- 未在源资产目录产生 `*.editor.json`；所有 round-trip 产物在 `build\vs2022\vfx\editor_roundtrip_test`；`git diff --check` 通过。

Known limits：

- round-trip 的 `vfxbake --check` 在测试 harness 中按资产执行，单资产 shaderc 在并发负载下可能失败，harness 重试一次；production `vfxbake` 不重试。28 个资产全量重烘焙约 11 分钟。
- round-trip 比较 `graphHash` + 序列化 JSON，并验证 layout sidecar 可重新加载；不逐字段 parse 所有未知 JSON 扩展。
- screenshot 测试只验证 PNG 签名/尺寸和非空，UI 文本/计时状态不参与 golden 像素比较。
- Vulkan `--bgfx-debug` 在当前环境仍受 validation layer 初始化限制，沿用既有 V0/V1 记录；Vulkan 以默认路径 smoke/golden 验证。
- 28 是当前 checked-in 资产数量；新增/删除资产需同步 `HBG_EXPECTED_ASSETS` 与文档。

---

## 7. 验证命令

```powershell
ctest --test-dir build\vs2022 -C Debug -R "hbg_vfx_tests|hbg_vfx_editor" --output-on-failure
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=vfx\tests\assets\vfx_scene.gltf `
  --vfx-editor=vfx\tests\assets\minimal_unlit.vfx.json --frames=3 --no-vsync
# 可选：覆盖编辑器编译工具链
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=<scene.gltf> `
  --vfx-editor=<effect.vfx.json> --vfx-shaderc=<shaderc.exe> --vfx-bgfx-src=<bgfx-root>
# preview smoke：未保存图编译到临时 .vfxb，不写 canonical binary
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --scene=<scene.gltf> `
  --vfx-editor=<effect.vfx.json> --vfx-editor-preview --no-vsync
# V2.5：28 资产 save->reload->vfxbake --check（约 11 分钟）
ctest --test-dir build\vs2022 -C Debug -R "hbg_vfx_editor_roundtrip" --output-on-failure
# V2.5：D3D11/Vulkan editor screenshot
ctest --test-dir build\vs2022 -C Debug -R "hbg_vfx_editor_screenshot" --output-on-failure
py -3 tools/idl_gen.py --check
```

V2.0–V2.5 关键文件：

- `vfx/include/vfx/editor.h` / `vfx/src/editor/document.cpp`
- `vfx/include/vfx/runtime.h` / `vfx/src/runtime/runtime.cpp`（`advanceOneFrame`）
- `vfx/tests/test_vfx_editor.cpp` / `vfx/tests/test_vfx_runtime.cpp`
- `src/app/vfx_editor_ui.h` / `src/app/vfx_editor_ui.cpp`
- `src/app/application.cpp` / `src/app/backend.cpp` / `src/app/backend.h`
- `src/render/passes/forward_renderer.h` / `.cpp`（playback/preview/hot reload）
- `src/app/debug_ui.cpp`（ASCII glyph preload）
- `vfx/tests/run_vfx_editor_compile.cmake`
- `vfx/tests/editor_roundtrip.cpp` / `vfx/tests/run_vfx_editor_roundtrip.cmake`
- `vfx/tests/run_vfx_editor_screenshot.cmake`
- `vfx/CMakeLists.txt` / root `CMakeLists.txt`
