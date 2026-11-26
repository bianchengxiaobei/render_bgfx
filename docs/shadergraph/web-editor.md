# Web 编辑器设计

> 关联：[editor-protocol.md](editor-protocol.md)、[architecture.md](architecture.md)、[targets.md](targets.md)、[nodes.md](nodes.md)。
> 技术栈：React 18 + TypeScript + Vite + React Flow（xyflow）+ Zustand + CodeMirror 6。
> 原则：Web 只是视图；节点类型、端口合法性、类型推断、诊断、编译全部由 C++ 提供。

---

## 1. 目录结构

```
web/
├─ package.json
├─ vite.config.ts            # dev proxy /api → 127.0.0.1:8777
├─ index.html
├─ src/
│  ├─ main.tsx
│  ├─ app/
│  │  ├─ App.tsx             # 布局与面板编排
│  │  ├─ Toolbar.tsx         # 打开/保存/编译/undo/redo/target
│  │  └─ Layout.tsx          # 可拖拽分栏
│  ├─ api/
│  │  ├─ client.ts           # fetch 封装、token、错误码
│  │  ├─ types.ts            # 由 protocol schema 生成
│  │  ├─ sse.ts              # EventSource 封装
│  │  └─ preview.ts          # MJPEG/单帧/节点预览
│  ├─ state/
│  │  ├─ document.ts         # 服务端文档镜像、revision
│  │  ├─ commands.ts         # 命令队列、baseRevision、回滚
│  │  ├─ selection.ts        # 选择/多选/框选
│  │  └─ ui.ts               # 面板可见性、缩放、主题、i18n
│  ├─ graph/
│  │  ├─ GraphCanvas.tsx     # React Flow 容器、键盘/拖放
│  │  ├─ nodes/GraphNode.tsx # 自定义节点
│  │  ├─ edges/GraphEdge.tsx # 连线样式/校验态
│  │  ├─ ports/Port.tsx      # Handle + 类型颜色 + 精度
│  │  ├─ GroupNode.tsx       # 节点组/框
│  │  └─ NoteNode.tsx        # Sticky Note
│  ├─ panels/
│  │  ├─ Blackboard.tsx      # 属性/关键字/分类
│  │  ├─ Inspector.tsx       # 节点/Target/属性详情
│  │  ├─ Preview.tsx         # 主预览 + 控制
│  │  ├─ Diagnostics.tsx     # 编译/校验日志
│  │  ├─ SearchPalette.tsx   # 节点搜索
│  │  └─ Breadcrumb.tsx      # 主图/子图导航
│  ├─ widgets/               # Float/Vector/Color/Gradient/Curve/Texture/Enum
│  ├─ theme/                 # Unity 风格深色主题（CSS 变量）
│  └─ i18n/                  # zh-CN/en-US
└─ tests/
```

## 2. 页面布局

```
┌──────────────────────────────────────────────────────────────┐
│ Toolbar: 文件 编辑 视图 Target 保存 编译 预览状态            │
├──────────────┬──────────────────────────────┬───────────────┤
│ Blackboard   │ Graph Canvas                 │ Inspector     │
│ 属性/关键字  │ 节点/连线/组/便签            │ 节点/Target   │
│ 分类/搜索    │                              │ 属性详情      │
│              │                              ├───────────────┤
│              │                              │ Master Preview│
├──────────────┴──────────────────────────────┴───────────────┤
│ Diagnostics / Compile Log（可折叠）                          │
└──────────────────────────────────────────────────────────────┘
```

- 分栏宽度可拖拽并持久化到 localStorage。
- Blackboard/Inspector/Preview 可折叠；窄屏时 Inspector 变为抽屉。
- 底部 Diagnostics 支持"错误 → 选中节点并居中"。

## 3. 图画布（React Flow）

### 3.1 节点

- 自定义节点类型 `GraphNode`：
  - 标题栏：显示名 + 分类颜色；可选图标。
  - 端口：左侧输入/右侧输出；`Handle` 的 `id` 为 slot id。
  - 未连接输入：按类型渲染内联控件（float/vector/color/bool/enum/texture）；连接后隐藏控件。
  - 徽标：错误/警告、预览缩略图、精度（可选）。
  - 折叠：仅显示标题与端口（可选）。
- 端口颜色：按 `ValueType` 映射（float/vector/matrix/texture/bool/dynamic）；动态槽使用中性色。
- 节点尺寸：由内容决定，固定最小宽度；保存位置不含尺寸（可计算）。

### 3.2 连线

- 仅允许 output→input；连接前本地做快速类型检查，最终以服务端 `connect` 响应为准。
- 非法连接：红色虚线 + toast，携带服务端错误码。
- 删除：选中边 Delete；或拖动端口断开。
- 高亮：悬停/选中时高亮上游/下游（可选）。

### 3.3 组、便签、多选

- 组（Group）：React Flow parent node；移动组带动子节点；组内节点仍可单独选择。
- 便签（Sticky Note）：独立节点，可编辑 Markdown/纯文本。
- 多选：Shift 框选；批量移动/删除/复制；对齐工具（客户端本地计算，结果通过 `moveNodes` 提交）。
- 复制/粘贴：内部剪贴板（存 JSON 片段）；跨文档暂不支持（v1）。

### 3.4 交互与快捷键

| 操作 | 快捷键 |
|---|---|
| 节点搜索 | `Space` / `Ctrl+Space` |
| 保存 | `Ctrl+S` |
| 编译 | `Ctrl+B` |
| Undo/Redo | `Ctrl+Z` / `Ctrl+Shift+Z` |
| 删除 | `Delete` |
| 复制/粘贴/重复 | `Ctrl+C/V/D` |
| 全选/框选 | `Ctrl+A` / 拖拽 |
| 居中选中 | `F` |
| 缩放 | 滚轮 / `Ctrl+0` 重置 |
| 折叠面板 | `Ctrl+1/2/3` |

快捷键可在设置中查看；输入框聚焦时不拦截。

## 4. Blackboard

- 属性行：类型图标、显示名、默认值预览、exposed 开关；单击选中，双击重命名。
- 关键字行：布尔/枚举；枚举展开 entry 列表。
- 分类：可折叠、拖拽排序；"+" 创建属性/关键字/分类。
- 拖拽属性到画布 → 创建 PropertyNode；拖拽关键字 → KeywordNode。
- 删除属性时提示受影响的 PropertyNode 数量；确认后由服务端级联删除引用。
- 搜索框过滤属性/关键字；大量属性时虚拟滚动。

## 5. Inspector

根据选择显示：

| 选择 | 内容 |
|---|---|
| 节点 | 节点设置（精度、模式、枚举）、槽位值、节点级错误、预览 |
| 属性 | 类型、默认值、精度、exposed、分类、属性特性（Range 等） |
| 关键字 | 类型、definition、entries、默认值 |
| 空 | Target 设置（surfaceType/alphaClip/doubleSided/renderQueue/precision） |
| 组/便签 | 标题、颜色、文本 |

- 输入控件即时提交（防抖 100ms）为 `setNodeSetting`/`setProperty`/`setSlotValue` 命令。
- `Gradient` 节点设置以 JSON keys 编辑，Inspector 同时渲染渐变预览条；未设置时使用 Unity 默认白→黑渐变。
- 非法输入本地拦截（数字范围/颜色格式/渐变结构），服务端仍做最终校验。

## 6. 主预览与节点预览

### 6.1 主预览

- `<img src="/api/preview/stream?...">` 显示 MJPEG；`onerror` 时降级为每 500ms 轮询 `preview.jpg`。
- 控件：mesh（sphere/plane/cube/quad）、size（256/512/1024）、背景（checker/black/gray/sky）、通道（rgb/r/alpha）、重置视角。
- 旋转/缩放：客户端将 yaw/pitch/distance 通过 query 节流 150ms 发给服务端（服务端渲染）。
- 标签页隐藏时暂停流（`visibilitychange`），减少 GPU 占用。
- 预览区显示当前 revision 与编译状态；`stale` 时覆盖提示。

### 6.2 节点预览

- 仅对视口内节点请求（IntersectionObserver）；首次显示占位图。
- 请求 `/api/preview/node/{id}.png?rev=...`；202 时轮询（最多 3 次）。
- 缓存 `Map<nodeId, {rev, url}>`；revision 变化时清理可见节点缓存。
- 用户可在设置中关闭节点预览（性能模式）。

## 7. 诊断与编译日志

- 服务端诊断：`severity/code/nodeId/slotId/pass/variant/message`。
- 列表按 severity + nodeId 排序；点击 → 选中节点、展开 Inspector、居中画布。
- 编译日志：pass/variant/backend/耗时；失败时显示 shaderc 原始错误（可折叠）。
- 变体超限/预算超限给出修复建议（减少关键字/贴图/属性）。

## 8. 节点搜索

- 触发：Space；在鼠标位置创建，或替换选中节点（可选）。
- 数据源：`/api/catalog/nodes`；本地缓存 + 服务端搜索。
- 排序：精确标题 > 前缀 > 同义词 > 分类；支持按 Target 过滤。
- 键盘：上下选择、Enter 创建、Esc 关闭；显示分类路径与简要说明。
- 目标专属节点在不可用 Target 下置灰并显示原因。

## 9. 状态与命令流

```ts
type DocumentState = {
  revision: number;
  graph: GraphJson;
  layout: LayoutJson;
  dirty: boolean;
  diagnostics: Diagnostic[];
  pending: Command[];
  inFlight: boolean;
};
```

流程：

1. 启动：`GET /api/session` → `GET /api/document` → 构建 React Flow 节点/边 → 建立 SSE。
2. 结构编辑：入队命令 → 串行发送 `POST /api/commands`（带 baseRevision）。
3. 成功：用响应 revision 更新；本地节点/边按命令增量应用（避免整图重建）。
4. 失败：若 `SG_STALE_REVISION` → 拉取最新文档并提示；其他错误 → 回滚乐观更新（若有）并 toast。
5. 布局：拖动仅更新本地位置；停止拖动 200ms 后发送 `setLayout`（不进 undo）。
6. Undo/Redo：调用服务端；响应后重新拉取受影响子图或整图（依规模）。
7. SSE：`revision/compile/file/diagnostics` 事件更新状态；不直接改图。

并发保护：

- 同一时刻只允许一个结构命令在飞行；后续排队。
- 请求超时（3s）→ 重试一次；仍失败 → 重新拉取文档。
- 页面刷新后以服务端 revision 为准。

## 10. 与 Unity Shader Graph 的 UX 对照

| Unity 功能 | v1 | 说明 |
|---|---|---|
| Blackboard 属性/关键字/分类 | 是 | 拖拽创建、内联编辑 |
| 节点搜索（Space）/同义词 | 是 | 服务端目录 |
| 端口类型/精度颜色 | 是 | 精度指示可选 |
| 未连接输入内联控件 | 是 | 按类型 |
| 节点预览缩略图 | 是（异步） | 可关闭 |
| Master Preview（mesh/旋转/背景） | 是 | 原生渲染流 |
| Inspector 节点/属性/Target | 是 | 分栏 |
| 组/框/Sticky Note | 是 | React Flow 节点 |
| 复制/粘贴/重复/删除 | 是 | 内部剪贴板 |
| Undo/Redo | 是 | 服务端权威 |
| 错误徽标/编译日志/点击定位 | 是 | nodeId/slotId |
| 子图打开/面包屑 | SG-6 | 独立文档 |
| Custom Function HLSL 编辑 | SG-6 | CodeMirror 6 |
| 节点热力图/颜色模式 | 否 | 可选后续 |
| 多窗口/多标签 | 否 | 单文档 v1 |
| 从 OS 拖入纹理 | 否 | 使用资产选择器 |

## 11. 性能

- React Flow 节点 memo 化；只有变更节点重渲染。
- 文档增量应用：根据命令返回的 applied 列表更新对应节点/边，不整图替换。
- 大图（>300 节点）：关闭节点预览、降低画布动画、虚拟化 Blackboard 列表。
- MJPEG 流是独立 `<img>`，不参与 React 渲染；标签页隐藏暂停。
- Blackboard/Inspector 使用局部状态，避免全局 store 触发全树更新。
- 拖拽时 `requestAnimationFrame` 节流；布局提交防抖。

## 12. 主题与本地化

- 深色 Unity 风格：背景 `#1e1e1e`、面板 `#252526`、强调色 `#4a90d9`；CSS 变量集中管理。
- 分类颜色：Input/Math/Artistic/UV/Utility/UI 等稳定映射。
- i18n：zh-CN 默认，en-US 可切换；节点标题/分类来自服务端元数据，不硬编码。
- 高 DPI：画布与图片使用 devicePixelRatio；预览 size 由服务端决定。

## 13. 构建与嵌入

开发：

```powershell
# 终端 1：原生宿主
.\build\vs2022\Debug\hbg.exe --backend=d3d11 --sg-editor=assets\smoke_lit.sgraph.json --sg-port=8777

# 终端 2：前端 dev server（vite proxy /api → 8777）
cd web
npm install
npm run dev
```

生产：

- `npm run build` 输出 `web/dist`。
- CMake 将 `dist` 嵌入可执行文件（生成 `embedded_web.cpp`）或安装到 `web/` 目录；`hbg` 优先使用 `--sg-web-root` 指定的目录（开发），否则使用嵌入资源。
- 单文件发布：静态资源 gzip 预压缩；不引入外部 CDN。

## 14. 测试

| 层 | 工具 | 内容 |
|---|---|---|
| 单元 | Vitest | store/命令队列/协议解析/类型颜色 |
| 组件 | Testing Library | Blackboard/Inspector/Search 交互 |
| 端到端 | Playwright（可选） | 打开→加节点→连线→编译→预览→保存 |
| 协议契约 | 快照 | 命令请求/响应与 C++ fixture 对齐 |
| 性能 | 手工基准 | 300 节点图拖拽/缩放帧率、MJPEG 延迟 |

## 15. 验收标准

- 能完成：新建 Lit 图 → 添加 Property/纹理/乘法 → 连接 BaseColor → 编译 → 预览 → 保存。
- 错误路径：类型不匹配、未连接必需输入、变体超限、shaderc 失败均能定位到节点/槽位。
- 双客户端并发编辑返回 409 并可恢复；刷新后状态一致。
- 512² 预览流 ≥10 fps；编辑到预览刷新 <500ms（60 节点图，防抖后）。
- 300 节点图拖拽/缩放可用（目标 ≥30 fps，具体以实测记录）。


## 16. 当前实现（SG-5 v1，2026-11-24）

- 入口：`web/src/main.tsx`、`web/src/app/App.tsx`、`web/src/app/Toolbar.tsx`。
- 协议/状态：`web/src/api/client.ts`、`web/src/api/types.ts`、`web/src/state/editor.ts`。
- 画布：`web/src/graph/GraphCanvas.tsx`、`GraphNode.tsx`、`NodePreview.tsx`、`model.ts`。
- 面板：`web/src/panels/Blackboard.tsx`、`Inspector.tsx`、`Preview.tsx`、`Diagnostics.tsx`、`SearchPalette.tsx`。
- 控件：`web/src/widgets/ValueWidget.tsx`；主题：`web/src/theme/theme.css`。
- 测试：`web/tests/model.test.ts`（Vitest）、`web/tests/browser-smoke.mjs`（Playwright）。

开发/验证：

```powershell
cd web
npm install
npm run dev       # /api 代理到 127.0.0.1:8777
npm run build     # dist 可由 hbg --sg-web-root=web/dist 托管
npm test
npx playwright install chromium   # once
$env:SG_EDITOR_URL="http://127.0.0.1:8777/?token=<token>"
npm run test:e2e
```

实现取舍：

- 服务端仍是唯一权威；Web 只做快速端口校验，最终以 command 响应为准。
- 结构命令串行并整图刷新；拖动位置本地更新，`moveNodes` 只更新 layout/位置。
- 无 layout fixture 使用前端/宿主确定性拓扑 fallback；打开显式 sidecar 时不覆盖。
- 主预览编辑后需重新编译；节点预览按 revision 请求，202 轮询并缓存 blob URL。
- 当前未覆盖 SG5-12（组/便签/复制粘贴）与正式 i18n；详见 `checklists/sg5-web.md` §7。
