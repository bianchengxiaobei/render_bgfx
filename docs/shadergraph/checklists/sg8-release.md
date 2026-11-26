# SG-8 收尾

> 目标：把功能、性能、文档、打包与回归收敛到可交付状态。
> 前置：SG-7。
> 参考：`AGENTS.md` 的构建/测试/代码约定；`docs/vfx/*` 的验收风格。

---

## 1. 任务

| ID | 任务 | 产物 | 参考 | 验收 |
|---|---|---|---|---|
| SG8-1 | 性能：bake/预览/编辑基线 | 基准脚本 + 记录 | `web-editor.md` §11/§15 | 60 节点 preview <1s；512² ≥10 fps；300 节点可编辑 |
| SG8-2 | 编译优化：并行/缓存/去重 | `shaderc.cpp`、缓存目录策略 | `tools/shader_build.py`、`vfx/src/compiler/compiler.cpp` | 重复编译命中缓存；多 pass 并行 |
| SG8-3 | 资源生命周期审计：program/uniform/texture/RT | 泄漏测试 | `AGENTS.md`、`vfx` 资源测试 | 反复打开/热重载/关闭无泄漏 |
| SG8-4 | 文档全量更新 | 本目录全部文档 | - | 与实现一致；Known limits 完整 |
| SG8-5 | 协议/前端文档与版本 | `editor-protocol.md`/`web-editor.md` | - | protocolVersion/兼容说明 |
| SG8-6 | 打包：web 资源嵌入/单文件 | CMake 生成/安装 | `web-editor.md` §13 | 无 Node 环境可运行 |
| SG8-7 | 安全复核：token/路径/请求/日志 | 审计清单 | `editor-protocol.md` §14 | 无未授权文件访问 |
| SG8-8 | 全量回归：CTest/IDL/gfx-test/双后端 | 记录 | `AGENTS.md` §构建与测试 | 全绿 |
| SG8-9 | 迁移/版本策略演练 | 旧 JSON/旧 `.sgb` fixture | `asset-format.md` §13 | 明确拒绝/迁移行为 |
| SG8-10 | 发布说明/已知限制 | `README.md` 更新 | - | 与 checklist 一致 |

## 2. 性能预算（目标，最终以实测为准）

| 项 | 目标 | 测量方式 |
|---|---|---|
| 60 节点图 preview 编译 | <1s | 宿主日志/`sgbake` stats |
| 完整 bake（Lit 5 pass × 2 后端） | <10s | `sgbake` stats |
| 编辑到预览刷新 | <500ms（防抖后） | 前端埋点/日志 |
| 512² 预览流 | ≥10 fps | MJPEG 帧计数 |
| 300 节点图拖拽 | ≥30 fps | 浏览器性能面板 |
| 内存/资源 | 无持续增长 | 泄漏测试 |

## 3. 回归矩阵

| 类别 | 内容 |
|---|---|
| 核心 | 图/类型/校验/序列化/子图单测 |
| 编译 | fixture 双后端；`.sgb` 确定性；变体/预算错误 |
| 渲染 | Lit/Unlit forward+deferred；Fullscreen/Canvas/CRT；D3D11/Vulkan |
| 旧功能 | 现有场景/材质 golden 0 diff；stage 契约回归 |
| 宿主 | 命令/undo/布局/并发/编译/预览/SSE |
| Web | 端到端流程、错误定位、性能 |
| 节点 | inventory 覆盖 100%；代表性节点 golden |
| 安全 | token/路径/大小/Origin |
| 迁移 | 旧 schema/typeVersion/未知节点 |

## 4. 验收

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --test-dir build/vs2022 -C Debug --output-on-failure
py -3 tools/idl_gen.py --check

.\build\vs2022\Debug\hbg.exe --backend=d3d11 --gfx-test
.\build\vs2022\Debug\hbg.exe --backend=vulkan --gfx-test

.\build\vs2022\Debug\sgbake.exe --check tests\shadergraph\assets
cd web; npm ci; npm run build; npm run test
```

通过标准：

- CTest/IDL/gfx-test/协议/前端测试全绿。
- 性能预算达标或记录偏差与原因。
- 文档与实现一致；Known limits 与不支持目标完整。
- 无 Node/npm 环境也能运行打包后的编辑器（静态资源嵌入）。
- 安全审计无高危项。

## 5. Known limits（汇总入口）

- 目标：Terrain/UITK/VFX 不支持；Fullscreen/Canvas/CRT 为最小实现。
- 渲染：图材质 v1 不参与 instancing；透明 distortion/backface 不支持；GBuffer 高级通道不写入。
- 节点：部分平台节点在对应 Target 落地前仅保证编译/禁用行为。
- 编辑器：单文档；无跨文档复制；节点预览异步。
- 资产：不兼容 Unity `.shadergraph`。
- 性能：大图/多变体场景需按实测调参。

## 6. 对齐参考

- 构建/测试/代码约定：`AGENTS.md:3/18/54`。
- VFX 验收风格：`docs/vfx/README.md`、`docs/vfx/v1-checklist.md`、`docs/vfx/v2-checklist.md`。
- 协议安全：`editor-protocol.md` §14。
- 资产版本：`asset-format.md` §13。
- 打包：`web-editor.md` §13。
