# V2 编辑器边界（占位）

> 状态：仅规划边界，不实现。V1 完成后再启动。
> 原则：编辑器只消费 `docs/vfx/asset-format.md` 的 JSON/`.vfxb`；不修改运行时 API。

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

## 3. 依赖

- V1.3 的图校验/错误定位。
- `vfx/include/vfx/compiler.h` 的稳定 C++ API。
- hdrp-bgfx 的 DebugUI/ImGui（或独立 Qt 编辑器，二选一，后续决定）。

## 4. 待决策

- 编辑器 UI 技术：ImGui node editor vs 自绘 Qt。
- 是否支持 Unity `.vfx` 导入/导出（V2+，需要单独解析器）。
- 是否支持 effect GPU instancing / 子图上下文（V2+）。
- 是否把 VFX 输出并入透明 DrawList 做统一排序。
