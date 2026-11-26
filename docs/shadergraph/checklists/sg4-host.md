# SG-4 编辑宿主

> 目标：提供 C++ 权威文档、命令/undo/layout、本地 HTTP/SSE/MJPEG、编译与预览队列，供 Web 与 CLI 共用。
> 前置：SG-1（文档/校验）、SG-2（编译接口）。
> 参考：VFX 的 `Document` 模式；hdrp-bgfx 的窗口/帧循环/CLI；协议见 `editor-protocol.md`。

---

## 1. 范围

- `shadergraph/src/editor/`：`document.{h,cpp}`、`commands.{h,cpp}`、`layout.{h,cpp}`、`history.{h,cpp}`。
- `src/app/sg_editor_host.{h,cpp}`：HTTP/SSE/MJPEG、token、路径沙箱、任务队列。
- `src/app/backend.cpp`/`application.cpp`：`--sg-editor`/`--sg-port`/`--sg-root`/`--sg-web-root` 选项与启动流程。
- 新增依赖：`cpp-httplib`（header-only，MIT）或等价轻量 HTTP 库；不引入 WebSocket（先用 SSE + POST）。
- 不实现 Web 前端（SG-5）。

## 2. 任务

| ID | 任务 | 产物 | 参考 | 验收 |
|---|---|---|---|---|
| SG4-1 | Document：加载/保存/脏标记/打开子图 | `document.cpp` | `vfx/include/vfx/editor.h:60`、`vfx/src/editor/document.cpp:795` | 打开/保存/另存 round-trip |
| SG4-2 | 命令总线：原子应用、校验、revision | `commands.cpp` | `editor-protocol.md` §5 | 所有 v1 命令 fixture |
| SG4-3 | Undo/Redo：结构历史、合并拖拽 | `history.cpp` | `vfx/src/editor/document.cpp:2061` | 撤销/重做后文档等价 |
| SG4-4 | Layout sidecar：读写/防抖/失败不阻塞保存 | `layout.cpp` | `vfx/src/editor/document.cpp:2221` | 布局损坏不影响图 |
| SG4-5 | HTTP 服务：bind/token/origin/路径沙箱/大小限制 | `sg_editor_host.cpp` | `editor-protocol.md` §1/§14 | 非本机绑定失败；逃逸被拒 |
| SG4-6 | 文档/命令/undo/redo/save API | `sg_editor_host.cpp` | `editor-protocol.md` §3-§6 | 响应/错误码 fixture |
| SG4-7 | 编译队列：preview/bake/cancel/stale | `sg_editor_host.cpp` | `vfx/tools/vfxbake.cpp:240`、`vfx/src/compiler/compiler.cpp:314` | 60 节点 preview <1s；可取消 |
| SG4-8 | 预览队列：FBO/readback/JPEG/MJPEG | `sg_editor_host.cpp` | `src/app/application.cpp:636/693`、`sg0-spike.md` | ≥10 fps；断开停止 |
| SG4-9 | SSE：revision/compile/file/diagnostics | `sg_editor_host.cpp` | `editor-protocol.md` §10 | 事件顺序正确；心跳 |
| SG4-10 | Catalog：nodes/targets/assets | `sg_editor_host.cpp` | `editor-protocol.md` §9 | 搜索稳定；目标能力正确 |
| SG4-11 | CLI/启动：`--sg-editor/--sg-port/--sg-root/--sg-web-root` | `backend.cpp`/`application.cpp` | `src/app/backend.cpp:634`、`src/app/application.cpp:636` | 启动/关闭/端口释放 |
| SG4-12 | 协议测试：fixture/并发/错误码/安全 | `tests/host/` | `editor-protocol.md` §15 | 全绿；无泄漏 |

## 3. 线程与队列

```
HTTP 线程：
  request → 鉴权/解析 → 任务封装 → 投递主线程（promise/future）→ 响应
                │
                ├─ 编译任务 → 编译 worker（快照 JSON）→ 完成回调 → 主线程发布
                └─ 预览任务 → 主线程渲染队列（长度 1，丢弃旧请求）→ 帧槽 → MJPEG 线程

主线程：
  每帧处理命令队列/编译结果 → 更新 Document/运行时 → 渲染 → 预览 readback
```

规则：

- 禁止 HTTP 线程触碰 bgfx/Document。
- 命令超时 2s；编译可配 120s；预览按帧预算。
- 帧槽双缓冲/序号；MJPEG 线程只读最近帧；慢客户端不阻塞渲染。
- 编译取消：设置 cancel 标志 + 终止 shaderc 子进程树；结果丢弃并返回 `cancelled`。
- 关闭：停止接受新请求 → 取消任务 → 停止渲染线程 → 释放端口。

## 4. 路径与安全

- `--sg-root` 默认当前工作目录；所有 `open/save/assets` 路径规范化后必须位于 root 内。
- 只允许 `.sgraph.json` / `.ssubgraph.json` / `.sgb` / 纹理扩展名（png/jpg/hdr/exr/tga）。
- 拒绝 `..`、绝对路径、符号链接逃逸；不跟随重解析点。
- token 32 hex，启动时打印到控制台/URL；不写入日志文件。
- 请求体上限 8 MiB；命令数上限 512；预览参数白名单。

## 5. 编译与预览集成

- 编译快照：主线程序列化当前图（canonical JSON）→ worker；worker 不回调 Document。
- 预览 shader：SG-2 的 `preview` target；只编译一个 pass/变体。
- 完整 bake：写 `.sgb` + manifest；成功后主线程热重载（SG-3）。
- 预览场景：球/平面/立方体/方片；相机/背景/通道参数；固定两盏灯 + 可选 sky/IBL（配置来自 sidecar）。
- 节点预览：临时图 → preview target → 128×128；按需/缓存/丢弃过期。

## 6. 验收

```powershell
# 启动宿主
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-editor=tests\shadergraph\assets\smoke_lit.sgraph.json --sg-port=8777 --sg-root=tests\shadergraph

# 协议冒烟（实现时提供脚本）
py -3 tools/sg_protocol_smoke.py --port 8777 --token <token>
```

通过标准：

- 所有 v1 命令有请求/响应 fixture；错误码覆盖 §12。
- 两客户端并发编辑 → 409 + 可恢复；刷新后一致。
- 预览 512² ≥10 fps；断开后 GPU 占用下降；编译可取消。
- 路径逃逸/无 token/超大请求被拒绝；关闭后端口释放。
- 无资源泄漏（多次打开/编译/关闭）。

## 7. Known limits（初始）

- 单文档单窗口；不支持多标签。
- 不支持 WebSocket；SSE 只做通知。
- 编译与预览共享同一主线程，重负载时预览会降频。
- 不提供通用文件浏览/导入；只列白名单扩展名。

## 8. 对齐参考

- VFX 文档/命令/undo/layout：`vfx/include/vfx/editor.h:60`、`vfx/src/editor/document.cpp:563/795/2061/2221`。
- VFX 编译/CLI：`vfx/src/compiler/compiler.cpp:314`、`vfx/tools/vfxbake.cpp:240`。
- hdrp-bgfx CLI/帧循环：`src/app/backend.cpp:634`、`src/app/application.cpp:636`、`src/app/window.cpp:24/77`。
- 预览/截图：`src/app/application.cpp:693`。
- 线程/资源约定：`AGENTS.md`（代码约定、RenderGraph、资源句柄化）。

## 9. SG-4 core v1 + 主预览 + 节点预览完成状态（2026-11-24）

已落地：

- `shadergraph/include/sg/editor.h`、`src/editor/`：`Document` 加载/保存/脏标记、v1 命令总线、原子回滚、undo/redo（深度 200）、layout sidecar 读写。
- `src/app/sg_editor_host.{h,cpp}`：`cpp-httplib` 本地 HTTP；`/api/session`、`/api/document`、`/api/commands`、undo/redo、open/save/save-as、diagnostics、compile jobs、catalog、assets、SSE 事件。
- 安全：只绑定 `127.0.0.1`；32 hex token；Origin 校验；8 MiB body；512 commands；路径规范化/符号链接逃逸检查；扩展名白名单。
- `src/app/backend.cpp`/`application.cpp`：`--sg-editor`/`--sg-root`/`--sg-web-root`/`--sg-port`/`--sg-token`；主线程 `poll()`；关闭时停止 HTTP 与 worker。
- 编译队列：主线程快照 `.sgraph.json`，worker 调 SG-2；`preview/bake`、backend/variant/HLSL/maxVariants/timeout；job 状态 `queued/running/succeeded/failed/cancelled/stale`；编辑后 stale。
- preview 编译：`mode=preview` 克隆图并切到 SG-2 `preview` target（单 forward pass/单变体）；backend 由 `Application` 按当前 bgfx 渲染器注入（D3D11/D3D12=dxbc，Vulkan=spirv）。
- `src/app/sg_preview_renderer.{h,cpp}`：离屏 FBO + readback + JPEG/PNG；sphere/plane/cube/quad；yaw/pitch/distance；checker/black/gray/sky 背景；rgb/r/g/b/a 通道；尺寸 128..1024。
- `GET /api/preview.jpg`：按 revision+参数缓存 1 帧；`GET /api/preview/stream`：`multipart/x-mixed-replace; boundary=sgframe`，静态帧按 fps 复用，512² 实测约 9.7 fps；客户端断开后停止 tick。
- 节点预览：`/api/preview/node/{id}.png` 从选中节点上游闭包构建临时图（加 `BlockBaseColor`，切 `preview` target），worker 异步编译；首请求 202+`Retry-After`，完成后返回 128×128 PNG；2D 节点用 quad、几何节点用 sphere；按 generation/revision/node/slot 缓存编译结果、按渲染参数缓存 PNG，并异步限流。`capabilities.nodePreview=true`。
- catalog：节点搜索稳定排序、Target 能力矩阵、沙箱纹理资产列表。
- 协议测试：`tests/host/test_sg_editor_host.cpp` 覆盖 session/document/command/stale/401/403/path escape/save-as/catalog/preview/node preview（异步 202→200、404、PNG）；`tools/sg_protocol_smoke.py --node-preview` 可做外部冒烟。

验证：

- `hbg_tests`：293 cases / 33208 assertions（含 host 节点预览/catalog slots/dev Origin）；`hbg_sg_tests`：36 cases / 1419 assertions（含无 layout fixture 位置 fallback）。
- `hbg` 启动 `--sg-editor` 后 HTTP health/session/document 可用；主线程命令与保存已验证；关闭后端口释放。
- D3D11 实测：`mode=preview` 单 pass/2 shader 成功；预览 128²~512² 返回有效 JPEG；MJPEG 512² 3s 收到 29 帧。
- D3D11 节点预览：首请求 202+`Retry-After`，随后 128² PNG；UV 节点 2D quad 1030 B，Position 节点 3D sphere 9418 B；Block/未知节点 404，旧 revision 409，重复请求 `X-SG-Node-Preview-Cache: hit`。
- Vulkan 实测：默认 preview backend 自动选 spirv；冷编译约 3.3s，主预览 JPEG 与节点预览 PNG 均成功。
- SG 相关 `ctest`：3/3 通过（hbg_sg_core/hbg_sg_compiler/hbg_tests）；全量 ctest 在既有 `dawnbake_agent_transfer` 用例阻塞，非本次 SG 改动。

当前缺口（SG-4 core 明确不宣称完成）：

- 预览（含节点预览）目前走 `preview` target 的单 unlit pass，未应用材质纹理路径；纹理属性按 `defaultType` 使用对应 1x1 fallback（默认 white）；未实现完整 SRP 灯光预览；`Time` 节点使用固定 0 时间。
- 预览编译冷启动仍受 shaderc/Debug 影响（Vulkan 约 3.3s），60 节点 <1s 仅在缓存命中时成立。
- `createSubgraph`/`createCustomFunction` 命令仍返回 `SG_TARGET_UNSUPPORTED`；Sub-graph 与 Custom Function 节点编译 v1 已支持，编辑器资产创建命令待接入。
- 取消任务目前只设置状态/丢弃结果，尚未终止 shaderc 子进程树；preview/bake job 还没有进度回调。
- Web 前端 v1 已由 SG-5 接入；`--sg-web-root` 可托管 `web/dist`（Vite dev 使用 `/api` proxy）；CMake 嵌入 dist 尚未实现。
- `assets` 仅列白名单纹理，未解析图像尺寸；`open/save` 仅单文档。
