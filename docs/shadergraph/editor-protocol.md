# Shader Graph 编辑器协议（本地宿主）

> 关联：[architecture.md](architecture.md)、[web-editor.md](web-editor.md)、[asset-format.md](asset-format.md)。
> 形态：`hbg --sg-editor=<asset> --sg-port=8777` 启动本地 HTTP 服务；浏览器打开 `http://127.0.0.1:8777/?token=...`。
> 原则：C++ 文档是唯一权威；Web 只发送语义命令与读取快照；所有命令带 revision；不暴露任意文件读写。

---

## 1. 传输与鉴权

- 仅绑定 `127.0.0.1`；默认端口 8777，可配置。
- 启动时生成一次性 token（32 hex），写入 URL；除 `/api/health` 外，所有 `/api/*` 请求必须带 `X-SG-Token` 头或 `?token=`。
- 校验 `Origin`：只允许同源/`localhost`，不返回 CORS 通配；开发模式接受任意 loopback 端口（如 Vite `localhost:5173`），仍拒绝非 loopback Origin。
- 请求体上限 8 MiB；同时限制 `commands` 数组长度（如 512）。
- 目录沙箱：`open/save` 路径必须位于启动时指定的 `--sg-root` 内；规范化后拒绝 `..` 与符号链接逃逸。
- 日志：记录命令名与耗时，不记录属性值/文件内容。

## 2. 通用约定

- 所有响应 `Content-Type: application/json; charset=utf-8`，除图片/MJPEG/SSE。
- 统一信封：

```json
{ "ok": true, "revision": 42, "data": { } }
```

```json
{ "ok": false, "error": { "code": "SG_TYPE_MISMATCH", "message": "...", "details": {} } }
```

- `revision` 单调递增，任何文档变更（含 undo/redo）都会递增。
- 并发：客户端命令携带 `baseRevision`；若与当前 revision 不一致，返回 `409 STALE_REVISION` 并附带最新 revision，客户端需重新拉取或重放。
- 时间：命令默认同步执行（<2s）；编译/预览为异步 job。

## 3. 端点总表

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/session` | 服务器信息、当前文档路径、能力集 |
| GET | `/api/document?rev=` | 完整文档快照（图 + layout + 诊断摘要） |
| POST | `/api/commands` | 批量命令 |
| POST | `/api/undo`、`/api/redo` | 撤销/重做 |
| POST | `/api/open` | 打开另一个图/子图 |
| POST | `/api/save`、`/api/save-as` | 保存 |
| GET | `/api/diagnostics?rev=` | 当前诊断 |
| POST | `/api/compile` | 触发编译（预览或完整 bake） |
| GET | `/api/compile/{jobId}` | 查询编译结果 |
| GET | `/api/catalog/nodes?q=` | 节点目录/搜索 |
| GET | `/api/catalog/targets` | Target 能力矩阵 |
| GET | `/api/assets?kind=texture&q=` | 沙箱内资产列表（纹理选择器） |
| GET | `/api/preview.jpg` | 单帧主预览 |
| GET | `/api/preview/stream` | MJPEG 主预览流 |
| GET | `/api/preview/node/{nodeId}.png` | 节点预览 |
| GET | `/api/events` | SSE 事件流 |
| GET | `/api/health` | 存活检查（无需 token 也可返回 200） |

## 4. 文档快照

`GET /api/document` 返回：

```json
{
  "ok": true,
  "revision": 42,
  "data": {
    "path": "assets/smoke_lit.sgraph.json",
    "kind": "graph",
    "dirty": false,
    "graph": { /* asset-format §2 内容 */ },
    "layout": { /* asset-format §9 内容 */ },
    "targets": [ { "id": "lit", "available": true }, { "id": "fullscreen", "available": false } ],
    "capabilities": { "compile": true, "preview": true, "nodePreview": true }
  }
}
```

- `graph` 与磁盘格式一致，便于客户端 diff/缓存。
- `layout` 可单独更新，不参与内容哈希。
- 大图（>2 MB）时返回压缩（`Content-Encoding: gzip`）；客户端需支持。

## 5. 命令

`POST /api/commands`：

```json
{
  "baseRevision": 42,
  "commands": [
    { "op": "addNode", "type": "Multiply", "id": 12, "position": [320, 180] },
    { "op": "connect", "from": { "node": 12, "slot": 2 }, "to": { "node": 15, "slot": 0 } },
    { "op": "setSlotValue", "node": 12, "slot": 1, "mode": "literal", "value": 2.0 },
    { "op": "moveNodes", "nodes": [12, 15], "delta": [10, 0] }
  ]
}
```

响应：

```json
{
  "ok": true,
  "revision": 43,
  "data": {
    "applied": 4,
    "diagnostics": [ { "severity": "error", "code": "SG_TYPE_MISMATCH", "nodeId": 12, "slotId": 1, "message": "..." } ],
    "dirty": true
  }
}
```

- 原子性：命令按顺序应用；遇到错误默认回滚整批（`atomic: true` 默认），返回 `applied: 0`。
- `id` 由客户端分配时必须在图内唯一；服务端可拒绝并返回已用 id。
- 命令执行后立即做局部校验；全图校验异步/按需。

### 5.1 命令集（v1）

| op | 字段 | 说明 |
|---|---|---|
| `addNode` | `type,id?,position,settings?` | 创建节点；省略 `id` 时服务端分配并在响应 `data.id` 返回 |
| `removeNodes` | `ids[]` | 删除节点及相连边 |
| `duplicateNodes` | `ids[],offset` | 复制节点/边；返回 id 映射 |
| `moveNodes` | `nodes[],delta` | 位置变更（仅 layout） |
| `setNodeSetting` | `node,name,value` | 节点设置（精度/枚举等） |
| `setSlotValue` | `node,slot,mode,value,concreteType?` | Literal/default 值 |
| `setSlotConcreteType` | `node,slot,concreteType` | 未连接动态槽类型 |
| `connect` | `from{node,slot},to{node,slot}` | 连接；类型校验 |
| `disconnect` | `from?/to?` | 断开指定边/输入 |
| `addProperty` | `name,type,default?,category?` | Blackboard 属性 |
| `removeProperty` | `refName` | 删除属性及 PropertyNode 引用 |
| `renameProperty` | `refName,newName` | 显示名变更（refName 可不变） |
| `setProperty` | `refName,field,value` | 默认值/精度/分类等 |
| `addKeyword` | `name,type,entries?` | 关键字 |
| `removeKeyword` | `refName` | 删除关键字 |
| `setKeywordEntry` | `refName,entryId,field,value` | 枚举项编辑 |
| `addGroup` | `title,position,size,nodes?` | 节点组 |
| `removeGroup` | `id` | 删除组（保留节点） |
| `setGroup` | `id,field,value` | 标题/位置/大小/颜色 |
| `addNote` | `title,position,size,text` | 便签 |
| `setNote` | `id,field,value` | 文本/位置/大小 |
| `setTargetSetting` | `name,value` | Target 设置 |
| `setLayout` | `view/nodes/groups/notes` | 布局批量更新（不入 undo 结构） |
| `createSubgraph` | `name,selection?` | 由选中节点创建子图（SG-6） |
| `createCustomFunction` | `name,hlsl` | 自定义函数资产（SG-6） |

暂不支持：跨文档复制粘贴、批量替换属性、节点对齐/自动布局（可作为客户端本地工具）。

## 6. Undo / Redo / Layout

- `POST /api/undo`、`/api/redo` 无 body；返回新 revision 与受影响节点 id 列表。
- 结构变更（节点/边/属性/关键字/Target）进入 undo 历史；布局变更默认不入 undo（客户端可本地撤销），但需通过 `setLayout` 持久化到 sidecar。
- 历史深度默认 200；连续拖拽合并为一条（`moveNodes` 由客户端节流后批量发送）。
- 保存不清空历史；打开新文档清空。

## 7. 编译

`POST /api/compile`：

```json
{
  "mode": "preview",
  "targets": ["lit"],
  "variants": "used",
  "backends": ["dxbc", "spirv"],
  "includeHlsl": false
}
```

| mode | 说明 |
|---|---|
| `preview` | 只编译预览所需的单 pass/单变体，返回预览 shader 摘要 |
| `bake` | 编译全部 pass/变体，写 `.sgb` 与 manifest |

响应（异步）：

```json
{ "ok": true, "data": { "jobId": "c-0007", "revision": 42 } }
```

`GET /api/compile/{jobId}`：

```json
{
  "ok": true,
  "data": {
    "state": "succeeded",
    "revision": 42,
    "diagnostics": [],
    "artifacts": [ { "path": "graphs/smoke_lit.sgb", "hash": "0x9f2c" } ],
    "stats": { "passes": 5, "variants": 1, "backends": 2, "milliseconds": 420 }
  }
}
```

- `state`: `queued/running/succeeded/failed/cancelled/stale`。
- 编译期间继续编辑会使 job 变 `stale`；客户端需重新触发。
- `bake` 默认写盘；`preview` 默认写临时目录（不进入资产）。
- 编译失败返回结构化诊断；shaderc stderr 原文放在 `details.raw`，最多 8 KB。

## 8. 预览

### 8.1 主预览

`GET /api/preview.jpg?mesh=sphere&size=512&yaw=30&pitch=10&distance=2.4&background=checker&rev=43&channel=rgb`

| 参数 | 默认 | 说明 |
|---|---|---|
| `mesh` | `sphere` | `sphere/plane/cube/quad` |
| `size` | 512 | 128..1024；非 2 的幂也允许，内部取整 |
| `yaw/pitch` | 0/0 | 度；服务端限制范围 |
| `distance` | 自动 | 相机距离；限制 0.5..10 |
| `background` | `checker` | `checker/black/gray/sky` |
| `channel` | `rgb` | `rgb/r/g/b/a`（调试） |
| `rev` | 当前 | 指定 revision；过期则重新渲染 |
| `vt` | - | 缓存破坏参数（客户端时间戳） |

- 返回 `image/jpeg`；未编译/无效图返回 1×1 占位或 409。
- 单帧请求同步渲染（受渲染线程队列限制）；同一 revision+参数缓存 1 帧。

### 8.2 流式预览

`GET /api/preview/stream?mesh=sphere&size=512&rev=43`

- `Content-Type: multipart/x-mixed-replace; boundary=sgframe`。
- 默认 10-15 fps；编辑防抖 300ms；可 `?fps=5` 降频。
- 服务器在无变化时可复用上一帧；客户端断开即停止渲染。
- 失败降级：`/api/preview.jpg` 按需模式（`?fallback=poll`）。

### 8.3 节点预览

`GET /api/preview/node/{nodeId}.png?rev=43&size=128&channel=rgb`

| 参数 | 默认 | 说明 |
|---|---|---|
| `size` | 128 | 128..1024；节点缩略图建议 128 |
| `mesh` | 自动 | 2D 节点用 `quad`，几何/3D 节点用 `sphere`；可显式覆盖 |
| `channel` | `rgb` | `rgb/r/g/b/a` |
| `background` | `checker` | `checker/black/gray/sky` |
| `rev` | 当前 | 指定 revision；过期返回 409 |

- 异步生成：首次请求返回 `202` + `Retry-After`，body 为 `{ok,data:{node,slot,state}}`；客户端轮询，完成后返回 `image/png`。
- 节点使用第一路可预览输出（float/vector2/vector3/vector4）构建临时图：上游依赖 + `BlockBaseColor` + `preview` target；不修改原文档。
- 缓存：编译二进制按 `(generation, revision, nodeId, outputSlot)`；PNG 按编译键 + 渲染参数；响应带 `X-SG-Node-Preview-Cache: hit|miss`。
- `hasPreview=false`（含 Block）、未知节点、无可用输出返回 `404`；编译失败返回 `422 SG_COMPILE_FAILED`；preview 不可用返回 `503`。
- 参考 Unity `PreviewManager`：每节点小图、按需异步编译、上游拓扑、2D/3D 选择；当前固定预览时间，完整材质/灯光预览仍待 SG-6。

## 9. 目录与搜索

`GET /api/catalog/nodes?q=mult&limit=50`：

```json
{
  "ok": true,
  "data": {
    "nodes": [
      {
        "type": "Multiply",
        "title": "Multiply",
        "category": "Math/Basic",
        "synonyms": ["times"],
        "targets": ["lit","unlit"],
        "settings": [],
        "hasPreview": true,
        "blockNode": false,
        "defaultPrecision": "inherit",
        "slots": [
          {
            "id": 0,
            "name": "A",
            "displayName": "A",
            "direction": "in",
            "type": "dynamicvector",
            "required": false,
            "hidden": false,
            "allowConnection": true,
            "supportsLiteral": true,
            "requiresLiteral": false,
            "canPropagateLiteral": false,
            "allowedConcreteTypes": [],
            "default": 1.0
          }
        ]
      }
    ]
  }
}
```

- 搜索结果由 C++ 生成，排序稳定：精确标题 > 前缀 > 同义词 > 分类。
- `slots`/`settings`/`blockNode`/`defaultPrecision` 是 SG-5 客户端渲染端口和内联控件所需元数据；`slots[].default` 与 graph 文件的值编码一致。
- 未保存 layout 的 fixture 在宿主加载时按入边拓扑分配确定性初始位置（仅内存），避免节点重叠；显式 layout sidecar/位置优先。
- `GET /api/catalog/targets` 返回 `targets.md` 的能力矩阵（available/reason）。

### 9.1 资产选择

`GET /api/assets?kind=texture&q=grid&limit=100`：

```json
{
  "ok": true,
  "data": {
    "assets": [
      { "path": "textures/grid.png", "kind": "texture2d", "width": 512, "height": 512 }
    ]
  }
}
```

- `kind`: `texture2d/texture2darray/texture3d/cubemap`；v1 只返回白名单扩展名。
- 路径相对 `--sg-root`，禁止越界；不返回绝对路径。
- 仅用于属性默认值/材质覆盖选择，不提供导入/删除。

## 10. SSE 事件

`GET /api/events` 返回 `text/event-stream`：

```
event: revision
data: {"revision":43}

event: compile
data: {"jobId":"c-0007","state":"succeeded","revision":42}

event: file
data: {"path":"assets/a.sgraph.json","kind":"modified"}

event: diagnostics
data: {"revision":43,"count":2}
```

- 心跳每 15s 一行注释（`: ping`）。
- 事件只做通知；客户端仍需拉取快照/诊断，避免把 SSE 当状态源。

## 11. 线程与超时

| 操作 | 线程 | 超时 | 备注 |
|---|---|---|---|
| 命令 | 主线程 | 2s | 超时返回 503，不保证已应用 |
| 文档读取 | 主线程 | 1s | 只读快照 |
| 预览渲染 | 主线程 | 帧预算 | 队列长度 1，丢弃旧请求 |
| 编译 | worker | 可配（默认 120s） | 可取消；shaderc 子进程杀进程树 |
| SSE/MJPEG 写 | HTTP 线程 | 连接生命周期 | 帧数据深拷贝 |

## 12. 错误码（稳定）

| code | HTTP | 说明 |
|---|---|---|
| `SG_BAD_REQUEST` | 400 | JSON/字段非法 |
| `SG_NOT_FOUND` | 404 | 节点/文档/路径不存在 |
| `SG_STALE_REVISION` | 409 | baseRevision 不匹配 |
| `SG_TYPE_MISMATCH` | 422 | 连接/赋值类型不合法 |
| `SG_CYCLE` | 422 | 连接成环 |
| `SG_TARGET_UNSUPPORTED` | 422 | 当前 Target 不支持该操作/节点 |
| `SG_VARIANT_LIMIT` | 422 | 变体超限 |
| `SG_BUDGET_EXCEEDED` | 422 | sampler/uniform 预算超限 |
| `SG_COMPILE_FAILED` | 422 | shaderc/生成失败 |
| `SG_BUSY` | 503 | 队列满/主线程超时 |
| `SG_IO` | 500 | 文件读写失败 |
| `SG_INTERNAL` | 500 | 未知错误 |

## 13. 协议类型同步

- 计划新增 `tools/sg_protocol_gen.py`：从单一 schema（`docs/shadergraph/protocol.schema.json`）生成 `web/src/api/types.ts`。
- C++ 侧手写序列化，但必须与 schema 一致；CI 对 fixture 请求/响应做快照测试。
- 任何破坏性变更提升 `protocolVersion`，`/api/session` 返回该版本；Web 检测不匹配时提示刷新/升级。

## 14. 安全边界

- 不提供任意 shell/文件浏览；`open` 只接受 `.sgraph.json`/`.ssubgraph.json`。
- 预览参数白名单；拒绝路径/URL 注入。
- 编译调用的 shaderc 路径在启动时固定，不由请求指定。
- 关闭服务即释放端口；异常退出不保留锁文件。

## 15. 验收

| 项 | 验收 |
|---|---|
| 命令 | 所有 v1 命令有请求/响应 fixture；错误码覆盖 |
| 并发 | 两客户端同时编辑 → 409 + 可恢复 |
| 预览 | 512² MJPEG ≥10 fps；断开后停止渲染 |
| 编译 | preview <1s（60 节点图）；bake 有进度/可取消 |
| 安全 | 非本机绑定失败；无 token 401；路径逃逸被拒绝 |
