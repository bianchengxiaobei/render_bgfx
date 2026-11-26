# V2 编辑器边界

> 状态：V2.0（文档模型/布局）、V2.1（ImGui 节点视图/Inspector）、V2.2（结构 authoring/undo/文本输入/Curve-Gradient）、V2.3（save→compile→error focus→hot reload）、V2.4（playback/preview/diagnostics）、V2.2b（clipboard/subgraph/expression/optional/recovery/close-confirm）与 V2.5（28 资产 round-trip/双后端截图/no-VFX 0-diff/文档）已完成，见 [v2-checklist.md](v2-checklist.md)。
> 原则：编辑器只消费 `docs/vfx/asset-format.md` 的 JSON/`.vfxb`；不修改既有 runtime API 语义、`.vfxb` binary 或 shader pack 契约。

---

## 1. 目标

- 节点图：System/Context（Spawn/Init/Update/Output）、Block/Operator、子图。
- 属性面板：暴露属性、Curve/Gradient 编辑、纹理槽位。
- 实时预览：复用 hdrp-bgfx 场景；播放/暂停/单步/重播。
- 烘焙：保存时调用 `vfx::compileGraphFile`（链接 `vfx_compiler`，不依赖 CLI）。
- 资产浏览器/错误面板：JSON 路径 + shaderc 错误定位。

## 2. 不改变的东西

- `.vfxb` binary 版本与运行时 API。
- Shader pack 契约（Lit/Unlit stage 表）。
- 运行时线程/生命周期规则（主线程、手动 update/encode）。
- `.vfx.json` 顶层 schema；编辑器布局写入独立 sidecar。

## 3. 已落地依赖/边界

- V1.3 的图校验/错误定位：编辑器每次修改后调用 `parseGraphJson()`，失败回滚并保留 `ParseError`。
- 图 JSON：`nlohmann::ordered_json` 保留字段顺序；不复制 Unity ScriptableObject 序列化。
- UI：复用 hdrp-bgfx 的 Dear ImGui/DebugUI context；自绘节点画布，不引入 Qt。
- 布局：`<asset>.editor.json` sidecar，保存 context 位置/折叠/pan/zoom 与 `graphHash`；不写进 `.vfx.json`/`.vfxb`。
- 编译/预览：V2.3 在 Save/Compile 后调用 `compileGraphFile`，写 `.vfxb` 并对匹配 scene VFX instance 调 `Runtime::reloadEffect`；V2.4 增加未保存图 → 临时 `.vfxb` 的 `Preview`，以及 play/pause/step/replay/time scale。

## 4. 决策结果

- 编辑器 UI 技术：**Dear ImGui 自绘节点画布**；不采用独立 Qt。
- Unity `.vfx` 导入/导出：**不在 V2**；需要单独解析器，后续评估。
- effect GPU instancing / 子图上下文：**V2+**，V2 不改 runtime 资源模型。
- VFX 输出并入透明 DrawList：**保持 V1 D8 结论**，V2 不重排透明对象。

## 5. 下一步

- 节点/Block/属性增删、block 移动、undo/redo、文本输入、Curve/Gradient 编辑已在 V2.2 主体完成；复制粘贴、子图 invocation、表达式文本编辑、自动 recovery、optional block 参数和关闭确认已补 V2.2b。
- V2.3 已接 Save/Compile → `.vfxb` → `Runtime::reloadEffect`，Compile 面板可按 `ParseError` 聚焦；同步编译阻塞、raw shaderc 错误未结构化仍为限制。
- V2.4 已接 Playback（play/pause/step/replay/time scale）、scene instance effect 替换、Runtime 诊断，以及未保存图到临时 `.vfxb` 的 `Preview`；bounds 目前为数值显示，3D 线框留独立 preview viewport。
- V2.5 已完成 28 个 checked-in 资产 round-trip（保存后 `vfxbake --check`）、D3D11/Vulkan editor 截图、`--no-vfx` 0-diff 与文档回填。后续只剩 V2 之外的新能力。
- V3.0 已完成粒子 motion vectors v1：透明/不透明 quad output 在 motion-vector pass 写入相机 + 粒子速度近似 MV，TAA/motion blur 可重投影；`.vfx.json`/`.vfxb` 不变。详见 `v3-checklist.md`。
- V3.1 已完成粒子阴影 v1：output 可选 `castShadows`，Inspector 可切换，粒子在 shadow atlas 中写 depth；透明输出 alpha clip，shadow cache 自动抑制。
- V3.2 已完成 per-output motion-vector gate：Inspector `Generate motion vectors` 可关闭固定 MV pass；JSON 缺省 true，旧 `.vfxb` byte 22=0 兼容。
