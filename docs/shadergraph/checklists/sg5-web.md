# SG-5 Web 编辑器

> 目标：用 React + React Flow 复刻 Shader Graph 编辑器核心交互，并以 SG-4 宿主为唯一后端。
> 前置：SG-3（运行时预览/编译）、SG-4（协议稳定）。
> 参考：`web-editor.md`、`editor-protocol.md`；UX 对齐 Unity 的 GraphView/Blackboard/Inspector/Preview。

---

## 1. 范围

- `web/`：Vite + React + TS + React Flow + Zustand + CodeMirror 6（SG-6 用）。
- 不实现新的图语义/校验/编译；全部调用宿主。
- 不实现多标签、多人协同、从 OS 拖入文件。

## 2. 任务

| ID | 任务 | 产物 | 参考 | 验收 |
|---|---|---|---|---|
| SG5-1 | 工程脚手架：Vite/TS/主题/i18n/dev proxy | `web/` | `web-editor.md` §1/§13 | `npm run dev` 可连宿主 |
| SG5-2 | 协议客户端：token/fetch/错误码/超时/重试 | `api/client.ts` | `editor-protocol.md` §2/§12 | 错误码映射齐全 |
| SG5-3 | 文档 store：revision/命令队列/脏标记/SSE | `state/document.ts` | `editor-protocol.md` §5/§9/§10 | 409 可恢复；刷新一致 |
| SG5-4 | 画布：自定义节点/端口/连线/选择/缩放/框选 | `graph/*` | `Editor/Drawing/Views/MaterialGraphView.cs:26`、`MaterialNodeView.cs:18` | 60 节点图流畅 |
| SG5-5 | 端口校验/类型颜色/动态槽 | `ports/*` | `codegen.md` §2 | 非法连接有提示 |
| SG5-6 | 内联控件（float/vector/color/bool/enum/texture） | `widgets/*` | `Editor/Drawing/Views/Slots/*` | 未连接输入可编辑 |
| SG5-7 | Blackboard：属性/关键字/分类/拖拽创建 | `panels/Blackboard.tsx` | `Editor/Drawing/Blackboard/SGBlackboard.cs:34` | 增删改/拖拽可用 |
| SG5-8 | Inspector：节点/属性/keyword/Target | `panels/Inspector.tsx` | `Editor/Drawing/Inspector/InspectorView.cs:13` | 设置即时生效 |
| SG5-9 | 节点搜索（Space）/分类/同义词 | `panels/SearchPalette.tsx` | `Editor/Drawing/SearchWindowProvider.cs:26` | 键盘操作；Target 过滤 |
| SG5-10 | 主预览（MJPEG/单帧/降级/控制） | `panels/Preview.tsx` | `Editor/Drawing/Inspector/MasterPreviewView.cs:19`、`PreviewManager.cs:24` | ≥10 fps；断开暂停 |
| SG5-11 | 诊断/编译日志/点击定位 | `panels/Diagnostics.tsx` | `editor-protocol.md` §7/§11 | 能定位 node/slot |
| SG5-12 | 组/便签/复制粘贴/删除/快捷键 | `graph/GroupNode.tsx`、`NoteNode.tsx` | `Editor/Drawing/Views/ShaderGroup.cs`、`StickyNote.cs` | 交互验收 |
| SG5-13 | 保存/打开/undo/redo/工具栏 | `app/Toolbar.tsx` | `Editor/Drawing/MaterialGraphEditWindow.cs:26` | 端到端一致 |
| SG5-14 | 构建/嵌入（dist/gzip/静态服务） | CMake/`sg_editor_host` | `web-editor.md` §13 | 单文件运行 |
| SG5-15 | 测试：Vitest/Playwright/性能 | `web/tests/` | `web-editor.md` §14 | 全绿；性能达标 |

## 3. 交互细节

### 3.1 命令流

- 结构命令：本地立即反映（可选乐观），串行发送；失败回滚并 toast。
- 409：拉取最新文档，保留本地选择/视口；提示"文档已更新"。
- 布局：拖动只改本地；200ms 后发送 `setLayout`。
- Undo/Redo：调用服务端；响应后按受影响节点更新或整图重拉。
- 编译：`Ctrl+B` → job → SSE 完成 → 刷新预览/诊断。

### 3.2 画布

- React Flow 自定义 `nodeTypes` 必须稳定（避免重建）。
- 节点内容按 `nodeId` memo；只有 revision/设置变化才重渲染。
- 端口使用 `Handle`，`id` 为 slot id；动态槽显示当前 concreteType。
- 连线校验：本地快速规则 + 服务端最终结果；错误边红色虚线。
- 大图模式：关闭节点预览、降低动画、限制框选高亮。

### 3.3 Blackboard / Inspector

- 属性行支持双击重命名、右键菜单、拖拽到画布。
- 关键字枚举 entry 可增删；删除前提示影响变体数。
- Inspector 的数字/颜色输入防抖 100ms；非法值本地拒绝。
- Target 设置修改会触发重新编译；提示变体/预算影响。

### 3.4 预览

- MJPEG `<img>`；`onerror` 降级轮询；标签页隐藏暂停。
- 控件变化 150ms 防抖后更新 URL（revision + 参数）。
- 预览显示 revision/编译状态；stale 时覆盖提示。
- 节点预览 IntersectionObserver + 缓存；性能模式可关闭。

### 3.5 诊断定位

- 点击诊断：`setCenter(node.position)` + 选中 + 打开 Inspector。
- shaderc 错误无 nodeId 时聚焦 pass/属性面板，并显示原始错误。
- 变体/预算错误提供修复建议（减少关键字/贴图/属性）。

## 4. 验收

```powershell
# 宿主 + 前端 dev
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-editor=tests\shadergraph\assets\lit_test.sgraph.json --sg-port=8777
cd web; npm install; npm run dev

# 生产构建
cd web; npm run build
```

端到端脚本（Playwright/手工）：

1. 新建 Lit 图。
2. 添加 `_BaseColor`（color）、`_MainTex`（texture2d）、`_Smoothness`（float）。
3. 放置 UV → TilingAndOffset → SampleTexture2D → Multiply → BaseColor。
4. 编译（preview）→ 预览出现纹理材质。
5. 断开连接 → 错误提示定位到 BaseColor；重新连接。
6. 保存 → 关闭 → 重开 → 图/布局/属性一致。
7. Undo/Redo 三次 → 状态正确。

通过标准：

- 上述流程无阻塞错误；刷新后 revision/内容一致。
- 错误路径（类型/缺输入/变体超限/shaderc）能定位。
- 60 节点图编辑到预览刷新 <500ms；512² 流 ≥10 fps。
- 300 节点图拖拽/缩放可用（记录实测帧率）。

## 5. Known limits（初始）

- 单文档；无多标签/多窗口。
- 无 OS 文件拖入；纹理通过资产选择器。
- 节点预览异步，首次可能占位。
- 无热力图/颜色模式/节点折叠（可后续）。
- 无跨文档复制粘贴。

## 6. 对齐参考

- 画布/节点/连线：`Editor/Drawing/Views/MaterialGraphView.cs:26`、`Editor/Drawing/Views/MaterialNodeView.cs:18`、`Editor/Drawing/Views/GraphEditorView.cs:47`。
- 搜索：`Editor/Drawing/SearchWindowProvider.cs:26`。
- Blackboard：`Editor/Drawing/Blackboard/SGBlackboard.cs:34`。
- Inspector/预览：`Editor/Drawing/Inspector/InspectorView.cs:13`、`Editor/Drawing/Inspector/MasterPreviewView.cs:19`、`Editor/Drawing/PreviewManager.cs:24`。
- 编辑窗口/保存：`Editor/Drawing/MaterialGraphEditWindow.cs:26`。
- 组/便签：`Editor/Drawing/Views/ShaderGroup.cs`、`Editor/Drawing/Views/StickyNote.cs`。
- 槽位控件：`Editor/Drawing/Views/Slots/*`。


---

## 7. v1 实现状态（2026-11-24）

已落地的 `web/` 主流程：

- SG5-1/2/3：Vite + React + TS + React Flow + Zustand；`api/client.ts` 处理 token、超时、重试、错误码；`state/editor.ts` 串行命令队列、revision/409 恢复、SSE、编译轮询。
- SG5-4/5/6：`graph/GraphCanvas.tsx`、`GraphNode.tsx` 自定义节点/端口/连线/框选/缩放；类型颜色、服务端最终校验、动态槽具体类型与未连接输入内联编辑。
- SG5-7/8/9：Blackboard 属性/关键字增删改与拖拽；Inspector 节点/属性/关键字/Target；Space 搜索与 Target 过滤。
- SG5-10/11：主预览 MJPEG + 单帧降级 + mesh/size/背景/通道/旋转；节点预览 202 轮询；诊断列表点击定位。
- SG5-13/14：打开/保存/另存为/undo/redo/编译工具栏；`--sg-web-root` 静态托管与 Vite `/api` dev proxy。
- 宿主协议补充：`/api/catalog/nodes` 返回 slot 描述、默认值、settings 与 block/preview 元数据；无 layout 的 fixture 使用确定性拓扑 fallback 位置。

验证：

- `npm run build` 通过（Vite 6.4.6）。
- `npm test`：6/6 Vitest 通过。
- `SG_EDITOR_URL=... npm run test:e2e`：节点渲染、编译成功、512² 预览、节点选择通过。
- 静态 `--sg-web-root=web/dist` + D3D11：编译后 512² MJPEG、节点缩略图、边删除/undo、搜索创建节点/undo、属性添加/undo、另存为/打开通过。
- `hbg_sg_tests`：36 cases / 1419 assertions；既有 `hbg_tests`：293 cases / 33208 assertions；本轮全量 `hbg` 重建受工作区未完成的非 SG 改动影响，最终全量门禁待复跑。

仍缺口（SG5-12 及后续）：

- 组/便签/复制粘贴/对齐尚未实现；当前只覆盖节点/边删除和快捷键。
- i18n 仅中文；Blackboard 分类 CRUD、枚举 entry 编辑、完整资产选择器/图像尺寸未接。
- Target 切换、帧率/300 节点性能基准、预览标签页隐藏后的服务端流关闭验收未完成。
- CMake 嵌入 `dist`/单文件发布尚未接；当前使用 `--sg-web-root` 或 Vite dev server。
- 预览仍是 preview target 单 unlit pass；纹理属性默认值/完整 SRP 灯光仍待 SG-6。
