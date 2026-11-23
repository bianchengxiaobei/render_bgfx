# VFX 编辑器 V2 清单

> 状态：V2.0（文档模型/布局）、V2.1（ImGui 节点视图/Inspector）、V2.2（结构 authoring/undo/文本输入/Curve-Gradient）、V2.3（save→compile→error focus→hot reload）与 V2.4（playback/preview/diagnostics）已完成；V2.2b 收尾、V2.5 待做。
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
| 未保存处理 | 关闭窗口时日志告警；显式 `Save` 写 graph + layout；默认 `On Save` 编译，也可手动 `Compile`；V2.2 已补 undo/redo | Unity 用 `EditorUtility.IsDirty`/保存对话框（`VFXViewWindow.cs:395-443`）；保存确认/自动恢复仍未接 |
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

- `hbg_vfx_tests`：**80/80, 38056 assertions**（含 13 个 editor case / 299 assertions）。
- `hbg_vfx_editor_d3d11` / `hbg_vfx_editor_vulkan` smoke：加载图、画 UI、3 帧退出。
- 全量 CTest **114/114（1396.72s）**，日志 `build\vs2022\v143_v24_final_ctest.log`；`hbg_vfx_tests` **80/80, 38056**；`hbg_tests` **271/271, 32794**；`hbg_vfx_compiler_tests` **22/22, 471**；`hbg_vfx_runtime_tests` **19/19, 494**；`hbg_vfx_adapter_tests` **7/7, 328**；IDL check 通过。
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
- 无复制粘贴、子图 invocation 插入、表达式文本编辑（V2.2b）。
- V2.3 已有 Compile/On Save/hot reload；play/pause/step/replay 与独立 preview 已在 V2.4 补齐。
- 不支持窗口 docking/多 graph tab；窗口位置使用 first-use 默认。
- 节点行宽固定 270 world px；长 block 名会被固定文本缓冲截断（显示层限制，不影响数据）。

---

## 3. V2.2 结构 authoring、undo、文本输入与 Curve/Gradient（主体完成）

- [x] `Document` 结构 API：`addSystem`/`removeSystem`/`renameSystem`、`addBlock`/`removeBlock`/`moveBlock`、`addOutput`/`removeOutput`、`addProperty`/`removeProperty`。
- [x] 默认参数来自共享 V1 block catalog：新增 `vfx/src/core/block_catalog.{h,cpp}`，parser 的 field 表与 editor 的默认 JSON 共用同一份 `detail::BlockSpec`；不在 editor 手写第二份 block 名/字段语义。
- [x] Block 参数编辑：number/bool/string；数值数组逐 component；custom HLSL `code` 多行输入；整数 JSON 字段保持整数；失败经 `parseGraphJson()` 回滚并显示错误。
- [x] Output `texture` 改为 texture property 下拉；property texture default path 可编辑；output add/remove。
- [x] Undo/redo：snapshot `root + dirty + layoutStale`，最多 128 条；同 edit key 在 500ms 内合并（drag 不会每帧压栈）；restore 后重建 graph/layout contexts；非法编辑不产生 history。
- [x] 文本输入：GLFW `key callback` + `char callback` → `WindowInput`；`DebugUI` 映射 GLFW key 到 `ImGuiKey` 并调用 `AddInputCharacter`；system name/new property/block string/texture path 可用键盘编辑。
- [x] 快捷键：`Ctrl+Z` undo，`Ctrl+Y`/`Ctrl+Shift+Z` redo；toolbar 增加 Undo/Redo/Add System。
- [x] Curve/Gradient：key add/set/remove；新增 key 自动选 `[0,1]` 内未占用时间；texture slot/错误面板沿用 Inspector。
- [x] Node/Block 删除：block 上/下移动；block 内 enabled/参数编辑；system/output/property 删除前由 parser 校验引用，引用未清除时回滚。
- [x] `subgraph` block 类型仍列出，但插入返回明确错误（需要先有可引用子图；V2.2b）；`trigger_event` 需 system emit event，over-life 需对应 gradient/curve property，否则返回错误。
- [ ] copy/paste/duplicate；subgraph invocation 插入；表达式文本编辑；可选 block 参数（如 sphere `surface`、velocity `direction/coneAngle`）的显式添加；保存前 dirty 确认/自动保存/崩溃恢复（V2.2b）。
- [x] 新增 editor test：非法结构回滚、system/block/output/property authoring、block catalog 默认值、undo/redo、Curve/Gradient、texture slot。

参考：

- `Editor/GraphView/Elements/VFXContextUI.cs:751-775`（按鼠标位置计算 block 插入 index）、`:786-833`（`OnCreateBlock`/block provider）。
- `Editor/GraphView/Views/VFXView.cs:575-611`（node/variant 创建）、`Editor/GraphView/Views/Controller/VFXViewController.cs:1274-1304`（node controller 同步）、`Tests/Editor/VisualEffectGraphEditorTests.cs:17-31`（open/add/undo/save 生命周期 sanity）。
- `Editor/Models/Blocks/Implementations/HLSL/CustomHLSL.cs:268-286`（错误注册）；本项目以 `ParseError` + HLSL 行列复用 V1.3 校验。

验证：

- `hbg_vfx_tests`：**80/80, 38056 assertions**（含 13 个 editor case / 299 assertions）。
- `hbg_vfx_editor_d3d11` / `hbg_vfx_editor_vulkan` smoke：加载图、画 UI、3 帧退出；本地截图 `build\vs2022\vfx_editor_v22.png`。
- 全量 CTest **114/114（1396.72s）**，日志 `build\vs2022\v143_v24_final_ctest.log`；`hbg_vfx_tests` **80/80, 38056**；`hbg_tests` **271/271, 32794**；`hbg_vfx_compiler_tests` **22/22, 471**；`hbg_vfx_runtime_tests` **19/19, 494**；`hbg_vfx_adapter_tests` **7/7, 328**；IDL check 通过。
- 未产生任何 checked-in `*.editor.json`；只有显式 Save 才写 sidecar。

Known limits：

- Text input 使用 ImGui 默认字体，非 ASCII 可输入但可能无字形；没有 IME 候选窗。
- `subgraph` 插入、表达式文本编辑、copy/paste、保存确认/自动恢复尚未接（V2.2b）。
- 文本字段在焦点离开且控件仍绘制时提交；切换选中节点/关闭窗口可能丢弃未提交文本。
- 新增 block 的默认参数表是编辑器默认值，不改变 compiler/runtime 的缺省语义；compiler 仍按 V1 规则验证。

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

- `hbg_vfx_tests`：**80/80, 38056 assertions**（含 13 个 editor case / 299 assertions）。
- `hbg_vfx_editor_compile` CTest：复制 `minimal_unlit.vfx.json` 到 build 目录，执行 `--vfx-editor-compile` 与 `--vfx-editor-preview`，校验非空 canonical `.vfxb` 且 preview 不修改 graph/canonical binary，**31.06s（全量 CTest，依赖 shader cache）** 通过。
- D3D11/Vulkan editor smoke 继续通过；`vfx_editor_v23_b.png` 显示 Compile/On Save 面板。
- 临时 `editor_scene.gltf` + `.hdrp.json` 指向复制的 `minimal_unlit.vfxb`；`Ctrl+S` 后日志：`VFX editor compile: ... compiled 36204 bytes in 6158 ms; hot reloaded`，同时生成 `.editor.json`。
- 全量 CTest **114/114（1396.72s）**；`hbg_vfx_editor_compile` 31.06s，日志 `build\vs2022\v143_v24_final_ctest.log`。

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
- `hbg_vfx_editor_compile` 31.06s：canonical compile + preview 均通过，preview 后源 graph 与 canonical `.vfxb` SHA256 不变。
- 全量 CTest **114/114（1396.72s）**，日志 `build\vs2022\v143_v24_final_ctest.log`。

## 6. V2.5 验收（待做）

- [ ] 编辑器 round-trip：加载 28 个 checked-in VFX 资产，保存后 `vfxbake --check` 通过且 `graphHash` 语义不变。
- [ ] D3D11/Vulkan 编辑器 smoke + render 截图。
- [ ] 无新增 runtime pass/validation error；`--no-vfx` 0-diff 保持。
- [ ] 文档、CLI usage、README 回填。

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
py -3 tools/idl_gen.py --check
```

V2.0–V2.4 关键文件：

- `vfx/include/vfx/editor.h` / `vfx/src/editor/document.cpp`
- `vfx/include/vfx/runtime.h` / `vfx/src/runtime/runtime.cpp`（`advanceOneFrame`）
- `vfx/tests/test_vfx_editor.cpp` / `vfx/tests/test_vfx_runtime.cpp`
- `src/app/vfx_editor_ui.h` / `src/app/vfx_editor_ui.cpp`
- `src/app/application.cpp` / `src/app/backend.cpp` / `src/app/backend.h`
- `src/render/passes/forward_renderer.h` / `.cpp`（playback/preview/hot reload）
- `src/app/debug_ui.cpp`（ASCII glyph preload）
- `vfx/tests/run_vfx_editor_compile.cmake`
- `vfx/CMakeLists.txt` / root `CMakeLists.txt`
