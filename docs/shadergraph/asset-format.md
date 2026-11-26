# Shader Graph 资产格式（自有格式 v1）

> 关联：[architecture.md](architecture.md)、[codegen.md](codegen.md)。
> 原则：JSON 面向编辑器，人类可读、可 diff；`.sgb` 面向运行时，确定性、无外部依赖、无绝对路径。
> 参考先例：`docs/vfx/asset-format.md`、`vfx/src/compiler/binary.cpp`、`vfx/src/editor/document.cpp:795`。

---

## 1. 文件类型

| 文件 | 用途 | 是否进入运行时 |
|---|---|---|
| `*.sgraph.json` | 主图（含 Target/Master） | 否（编译为 `.sgb`） |
| `*.ssubgraph.json` | 子图（无 Target） | 否（编译期内联） |
| `*.sgraph.editor.json` | 主图布局 sidecar | 否 |
| `*.ssubgraph.editor.json` | 子图布局 sidecar | 否 |
| `*.sgb` | 编译产物（dxbc + spirv + 元数据） | 是 |
| `graphs/manifest.json` | `.sgb` 索引（发现/热重载） | 是 |

扩展名与 Unity 的 `.shadergraph` / `.shadersubgraph` 不同，避免误导入。

目录约定（默认）：

```
assets/ 或 tests/shadergraph/
├─ smoke_lit.sgraph.json
├─ smoke_lit.sgraph.editor.json
├─ smoke_lit.sgb
├─ manifest.json            # 汇总所有 .sgb
├─ textures/
└─ subgraphs/
   ├─ blend.ssubgraph.json
   └─ blend.ssubgraph.editor.json
```

- `manifest.json` 可由 `sgbake` 生成/更新；运行时也可扫描目录（仅开发模式）。
- 纹理路径相对资产根，禁止绝对路径。

## 2. `.sgraph.json` 顶层

```json
{
  "schemaVersion": 1,
  "kind": "graph",
  "name": "LitTest",
  "generatorVersion": "sg-0.1.0",
  "target": {
    "id": "lit",
    "subTarget": "lit",
    "settings": {
      "surfaceType": "opaque",
      "alphaClip": false,
      "doubleSided": false,
      "renderQueue": -1,
      "precision": "inherit",
      "normalSpace": "tangent"
    }
  },
  "properties": [],
  "keywords": [],
  "dropdowns": [],
  "categories": [],
  "nodes": [],
  "edges": [],
  "groups": [],
  "notes": [],
  "customFunctions": []
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `schemaVersion` | int | 是 | 当前 1；更高版本拒绝加载 |
| `kind` | string | 是 | `graph` / `subgraph` |
| `name` | string | 是 | 显示名；不等于文件名 |
| `generatorVersion` | string | 否 | 生成该文件的工具版本，仅诊断 |
| `target` | object | graph 必填 | 见 §7 |
| `properties` | array | 否 | 见 §4 |
| `keywords` | array | 否 | 见 §5 |
| `dropdowns` | array | 否 | Dropdown 定义，见 §5.1 |
| `categories` | array | 否 | Blackboard 分类，见 §6 |
| `nodes` | array | 是 | 见 §3 |
| `edges` | array | 否 | 见 §3 |
| `groups` | array | 否 | 节点组/框 |
| `notes` | array | 否 | Sticky Note |
| `customFunctions` | array | 否 | 保留给编辑器自定义函数资产；v1 编译器以节点 settings 与独立 `.hlsl` 为准 |

## 3. 节点、槽位、边

```json
{
  "id": 12,
  "type": "Multiply",
  "typeVersion": 1,
  "position": [320.0, 180.0],
  "settings": { "precision": "inherit" },
  "slots": {
    "0": { "mode": "connected" },
    "1": { "mode": "literal", "value": 2.0 },
    "2": { "mode": "connected" }
  }
}
```

```json
{ "from": { "node": 12, "slot": 2 }, "to": { "node": 15, "slot": 0 } }
```

| 字段 | 说明 |
|---|---|
| `id` | 图内唯一 int；由编辑器分配，不复用 |
| `type` | 稳定节点类型名（见 `nodes.md`）；不写 C# 类型全名 |
| `typeVersion` | 节点 spec 版本；用于迁移 |
| `position` | 初始布局；若 sidecar 存在则以 sidecar 为准；不参与内容哈希（见 §9/§12） |
| `settings` | 节点级设置（精度、模式、枚举等）；由节点 spec 声明；`Switch.cases` 使用 `"名称|比较|阈值;..."` 字符串，编辑模式显示 A-Z；`Custom Function.inputSlots/outputSlots` 使用 `"名称|类型;..."` 编码 |
| `slots` | key 为稳定 slot id；缺省表示使用节点默认值 |

槽位值模式：

| mode | 字段 | 说明 |
|---|---|---|
| `default` | 可选 `value`、`concreteType` | 使用节点默认值；动态槽可持久化具体类型 |
| `literal` | `value`、`concreteType` | 未连线但显式赋值（Literal 模式） |
| `connected` | 无 | 值来自边；若同时有 `value` 仅作回退 |

值编码：

- `float`：JSON number。
- `vector2/3/4`、`color`：number 数组，长度 2/3/4。
- `boolean`：bool。
- `integer`：int。
- `matrix2/3/4`：长度 4/9/16 的 number 数组，**JSON 行主序**；运行期上传 bgfx `Mat4` 时按引擎约定转换（默认列主序，SG-0 实测确认；见 `codegen.md`）。
- 纹理/采样器/Gradient：字符串引用或对象（见 §4）。

规则：

- 边必须连接 output→input；同一 input 只能有一条边。
- 禁止自环/环（除显式允许的递归子图，v1 不支持）。
- 未使用的节点保留（与 Unity 一致），但编译时只收集活跃节点。

## 4. 属性（Blackboard）

```json
{
  "name": "Base Color",
  "refName": "_BaseColor",
  "type": "color",
  "precision": "inherit",
  "exposed": true,
  "default": [1.0, 1.0, 1.0, 1.0],
  "category": "Surface",
  "floatMode": "default",
  "attributes": []
}
```

| 字段 | 说明 |
|---|---|
| `name` | 显示名；图内唯一（大小写敏感） |
| `refName` | HLSL 引用名，默认 `_` + 规范化 name；唯一、稳定、作为材质 override 键 |
| `type` | `float/vector2/vector3/vector4/color/boolean/integer/matrix2/matrix3/matrix4/texture2d/texture2darray/texture3d/cubemap/gradient/samplerstate/virtualtexture/dropdown` |
| `precision` | `inherit/half/float` |
| `exposed` | 是否出现在材质 Inspector |
| `default` | 按类型编码；纹理为路径字符串；Gradient 为 keys 数组 |
| `category` | Blackboard 分类名 |
| `floatMode` | `default/slider/integer/enum`（仅 float/integer） |
| `enumNames` | `floatMode=enum` 时的显示名数组；按数组顺序生成 Switch 自动 case |
| `enumValues` | 与 `enumNames` 对应的整数值；缺省时按索引补齐 |
| `attributes` | `[Range(min,max)]`、`[NoScaleOffset]` 等元数据，v1 只保留字符串 |

纹理属性附加字段：

```json
{
  "name": "Main Tex",
  "refName": "_MainTex",
  "type": "texture2d",
  "default": "textures/default.png",
  "sampler": { "filter": "linear", "wrap": "repeat", "anisotropic": "x8" },
  "defaultType": "white",
  "isHDR": true,
  "useTilingAndOffset": true,
  "tiling": [2.0, 3.0],
  "offset": [0.25, 0.5]
}
```

- `defaultType`（`texture2d` 的运行时回退）：`white/black/grey(gray)/bump(normal)/linearGrey/red`；写入 `.sgb` 属性表 reserved byte（0..5）。无纹理覆盖时 `GraphBinding` 绑定对应 1x1 常量纹理：white `(1,1,1,1)`、black `(0,0,0,1)`、grey `(0.5,0.5,0.5,1)`、bump `(0.5,0.5,1,1)`、linearGrey `(0.5,0.5,0.5,1)`、red `(1,0,0,1)`；Texture2DArray/3D/Cubemap 仍使用白色 fallback。
- `isHDR`（仅 `texture2d/texture2darray/texture3d/cubemap`）：声明 `u_sg_<refName>_HDR`，采样后按 Unity `DecodeHDRSample` 条件解码；运行期默认 `(1,1,0,0)` 为恒等值，材质可用 `propertyOverrides: { "<refName>_HDR": [scale, exponent, 0, alphaScale] }` 覆盖。
- `useTilingAndOffset`（仅 `texture2d`）：启用纹理 UV 变换；`tiling` 默认 `[1,1]`，`offset` 默认 `[0,0]`。代码生成声明 `u_sg_<refName>_ST`，Sample Texture 2D/LOD、Normal From Texture、Parallax Mapping/POM 使用 `uv * _ST.xy + _ST.zw`，Split Texture Transform 输出 `_ST.xy/_ST.zw`，Texture Only 恢复恒等。默认值写入 `.sgb`，材质可用 `propertyOverrides: { "<refName>_ST": [tilingX, tilingY, offsetX, offsetY] }` 覆盖。
- `sampler.anisotropic`（`none/x2/x4/x8/x16`，默认 `none`）写入 `.sgb` 的 samplerAnisotropy；Sampler State 节点或 SamplerState Property 会覆盖目标纹理属性的该值。bgfx 只支持全局 max anisotropy，非零值均使用设备最大值。
- `samplerstate` 属性使用 `sampler.filter`/`sampler.wrap`/`sampler.anisotropic` 作为 Property 输出连接 Sampler/HeightmapSampler 的元数据。
- `virtualtexture` 属性使用 `vtLayers`（1..4，默认 2）展开 `<refName>_LayerN` 隐式 Texture2D 层；可选 `vtLayerRefNames`/`vtLayerTextureTypes` 覆盖层引用与类型（`default`、`normalTangentSpace`、`normalObjectSpace`，数组长度需等于 `vtLayers`）。v1 为 Unity TextureStack fallback，真实 Granite VT stack/streaming/feedback 未接。
- 运行期纹理路径不写入 `.sgb`；`.sgb` 只存属性名与默认值标记，路径由材质 sidecar/资产系统绑定（与 VFX 资产一致）。

Gradient 编码：

```json
{ "type": "gradient", "keys": [ { "t": 0.0, "color": [1,1,1,1] }, { "t": 1.0, "color": [0,0,0,0] } ] }
```

- keys 按 `t` 升序稳定排序；重复 `t` 报错。
- v1 将 Gradient 节点 key 直接烘焙到生成代码；Blackboard Gradient 属性的运行期 `Texture2D` LUT 仍待实现。

### 自定义函数 v1

v1 的 `Custom Function` 节点使用节点 settings 描述函数和槽位：

```json
{
  "id": 1,
  "type": "Custom Function",
  "typeVersion": 1,
  "settings": {
    "sourceType": "string",
    "functionName": "HbgCustomAdd",
    "functionBody": "Out = A + B * T;",
    "inputSlots": "A|float;B|float;T|float",
    "outputSlots": "Out|float"
  },
  "slots": {}
}
```

- `inputSlots` / `outputSlots` 为分号分隔的 `名称|类型`；类型使用 `float`、`vector2/3/4`、`boolean`、`integer`、`matrix2/3/4` 等本地 ValueType 名称。
- 输入 slot id 为 `0..15`，输出 slot id 为 `100..115`，最多各 16 个；边引用这些稳定 id。
- `sourceType` 为 `string` 时使用 `functionBody` 生成 `void <functionName>(...)`；为 `file` 时使用 `functionSource` 作为 `#include` 路径（相对 shader include 根），可用 `.hlsl`/`.cginc`/`.cg`。
- `functionSourceUsePragmas` 已保留但 v1 不改变 include 行为；Unity GUID 源引用、纹理/SamplerState 槽位和 include 白名单尚未实现。
- 顶层 `customFunctions` 字段仍保留给后续编辑器资产格式，当前编译器以节点 settings 为准。

## 5. 关键字

```json
{
  "name": "Normal Map",
  "refName": "_NORMALMAP",
  "type": "boolean",
  "definition": "shaderfeature",
  "scope": "local",
  "default": false,
  "category": "Surface"
}
```

枚举关键字：

```json
{
  "name": "Mode",
  "refName": "_MODE",
  "type": "enum",
  "definition": "shaderfeature",
  "entries": [
    { "id": 1, "displayName": "A", "referenceName": "MODE_A" },
    { "id": 2, "displayName": "B", "referenceName": "MODE_B" }
  ],
  "default": 1
}
```

| 字段 | 说明 |
|---|---|
| `type` | `boolean` / `enum`（对齐 Unity `Generation/Enumerations/KeywordType.cs:6`） |
| `definition` | `shaderfeature/multicompile/predefined/dynamicbranch`（对齐 `KeywordDefinition.cs:6`） |
| `scope` | `local/global`；v1 只支持 `local` |
| 变体上限 | 默认 64 组合；超出必须报错并提供修复建议 |

v1 策略：关键字全部编译期展开为变体；`dynamicbranch` 在 v1 视为 `shaderfeature`，或在 v2 用 `uniform int` 实现。不得静默忽略。

### 5.1 下拉框 v1

Dropdown 是独立于 Keyword 的图级选择集合；v1 以活动 `value` 选择输入，不生成 keyword 预处理器分支。

```json
{
  "dropdowns": [
    {
      "name": "Mode",
      "refName": "_MODE_SELECTION",
      "value": 1,
      "entries": [
        { "id": 1, "displayName": "A" },
        { "id": 2, "displayName": "B" }
      ]
    }
  ],
  "nodes": [
    {
      "id": 1,
      "type": "Dropdown",
      "typeVersion": 1,
      "settings": { "dropdown": "_MODE_SELECTION" },
      "slots": {}
    }
  ]
}
```

- `dropdowns` 仅在非空时输出；`name`/`refName` 必填，`refName` 必须是唯一标识符；`value` 是 entry 索引，读取时越界会 clamp 到首/尾。
- `entries[].id` 必须在 `1..16` 内且唯一，并同时作为节点输入槽 id；v1 最多 16 项。输出槽固定为 id `0`，输入槽按 entry id 定义。
- 节点 settings 使用 `dropdown` 引用 `refName`；`dropdownRef`/`refName` 会归一化为 `dropdown`。
- 子图展开会把子图 dropdown 提升到父图，并用 `sg<node>_<ref>` 重命名冲突引用。SubGraph 节点可用 `settings["dropdown.<refName>"]` 为该实例选择 entry，值为 entry 的 `displayName` 或 0-based 索引；未设置或非法时回退到子图 dropdown 的 `value`。
- 上述实例选择在展开期特化；嵌套子图的逐实例选择尚未实现，本地也尚未生成 Unity 运行时 `if (<ref> == n)` 选择链。
- Unity 的 `DropdownNode.allowedInMainGraph` 为 false；本地 v1 允许主图用于直接活动值编译，后续再收紧到子图实例语义。

## 6. 分类、组、便签

```json
{ "name": "Surface", "children": ["Base Color", "Metallic"] }
```

```json
{ "id": 3, "title": "Lighting", "position": [100, 200], "size": [300, 200], "color": [0.2, 0.3, 0.4, 1], "nodes": [12, 15] }
```

```json
{ "id": 4, "title": "TODO", "position": [500, 100], "size": [200, 120], "text": "检查法线空间", "theme": "classic" }
```

- `categories` 只影响 Blackboard 顺序与分组；属性顺序以数组顺序为准。
- 节点/组/便签位置：`.sgraph.json` 中的 `position` 仅作初始布局；`.sgraph.editor.json` 存在时以 sidecar 为准。
- `groups`/`notes` 的 `position/size` 参与内容 diff，但不参与内容哈希。

## 7. Target 设置

```json
"target": {
  "id": "lit",
  "subTarget": "lit",
  "settings": {
    "surfaceType": "opaque",
    "alphaClip": false,
    "doubleSided": false,
    "renderQueue": -1,
    "precision": "inherit",
    "normalSpace": "tangent",
    "allowMaterialOverride": false
  }
}
```

| Target id | v1 状态 | 说明 |
|---|---|---|
| `lit` | 计划 | 复用 `SurfaceData` + forward/gbuffer |
| `unlit` | 计划 | 新增 unlit forward/depth |
| `fullscreen` | SG-7 | 全屏材质 pass |
| `canvas` | SG-7 | UI/2D quad 绘制 |
| `crt` | SG-7 | 离屏 Custom Render Texture |
| `preview` | 内部 | 节点/主预览，不写入用户资产 |

设置字段由 `TargetDesc` 声明；未知字段报错（避免静默丢失）。

## 8. 子图

```json
{
  "schemaVersion": 1,
  "kind": "subgraph",
  "name": "Blend",
  "outputs": [
    { "name": "Out", "type": "vector3", "slot": 0 }
  ],
  "properties": [],
  "nodes": [],
  "edges": []
}
```

- 子图不包含 Target；编译期展开（flatten），属性重命名加命名空间前缀。
- 子图可以有 exposed properties；展开时与父图属性合并，冲突报错。
- 递归/循环引用在加载时检测并报错；最大展开深度默认 16。

## 9. 布局 sidecar

```json
{
  "schemaVersion": 1,
  "revision": 42,
  "view": { "pan": [120, 80], "zoom": 0.9 },
  "nodes": [
    { "id": 12, "position": [320, 180], "collapsed": false }
  ],
  "groups": [
    { "id": 3, "position": [100, 200], "size": [300, 200], "collapsed": false }
  ],
  "notes": [
    { "id": 4, "position": [500, 100], "size": [200, 120] }
  ]
}
```

- sidecar 与服务端 Document 分离；布局移动不进入 undo 的图结构历史（可本地撤销）。
- sidecar 写入失败不得影响图保存；由编辑器提示。

## 10. `.sgb` 二进制

### 10.1 头部

```
offset  size  field
0       4     magic "SGB1"
4       2     formatVersion = 1
6       2     flags (bit0: hasDebugHlsl)
8       8     contentHash (FNV-1a 64 of canonical inputs)
16      8     sourceHash  (FNV-1a 64 of canonical JSON)
24      4     sectionCount
28      ...   sections[]
```

Section：

```
u32 kind
u64 offset (from file start)
u64 size
```

| kind | 名称 | 内容 |
|---|---|---|
| 1 | META | name、target id、generatorVersion、schemaVersion、nodeSpecVersion、stageBudget、requiredFeatureMask |
| 2 | STRINGS | 去重字符串表（UTF-8，长度前缀） |
| 3 | PROPERTIES | 属性表（名字在 STRINGS 中的索引） |
| 4 | KEYWORDS | 关键字表 |
| 5 | VARIANTS | 变体键 → pass 组合 |
| 6 | PASSES | pass 元数据、stage 表（语义→stage）、渲染状态、feature mask |
| 7 | BLOBS | dxbc/spirv 二进制 |
| 8 | HLSL | 可选调试源（按 pass 分文件） |
| 9 | DIAGNOSTICS | 编译期警告（仅信息，不进入运行时决策） |

- `requiredFeatureMask` 是运行期图特性位：bit0 `SceneColor`、bit1 `SceneDepth`、bit2 `Fog`。运行期只接受这些已知位；未知位在 `validateStageContract` 拒绝。
- Scene Color/Depth 系统采样器不占用引擎 5-15 固定 stage，而是按属性纹理顺序占用 graph stage 0-4 的空闲槽；属性和系统纹理总 stage 超过 `stageBudget` 时编译报错。运行期 `GraphMaterialSystem` 按同一顺序推导 stage 并绑定。

### 10.2 属性表条目

```
u32 nameIndex, refNameIndex
u8  type, precision, flags     // flags bit0=has sampler stage, bit1=useTilingAndOffset, bit2=isHDR
u8  default[4 * 4]             // 最多 vec4，按类型解释
f32 matrix[16]                 // matrix 类型；texture2d 的 bit1 置位时 [0..3]=tiling.xy, offset.xy
u32 defaultTextureIndex        // 0 = none
u8  samplerFilter              // 0=linear, 1=point, 2=trilinear
u8  samplerWrap                // 0=repeat, 1=clamp, 2=mirror, 3=mirror once
u8  samplerAnisotropy          // 0=none, 2=x2, 4=x4, 8=x8, 16=x16
u8  reserved
```

- Sampler State 节点连接采样节点的 Sampler 输入时，会把 filter/wrap/anisotropic 写入该纹理属性的上述字段；运行期 `GraphBinding` 用它设置 bgfx sampler flags。bgfx 只暴露全局 max anisotropy，因此非零 x2/x4/x8/x16 都映射为 `MIN/MAG_ANISOTROPIC`，实际等级由设备最大值决定（Device 初始化启用 `BGFX_RESET_MAXANISOTROPY`）。
- `Texture2D` 的 `useTilingAndOffset` 以 flags bit1 标记，默认 tiling/offset 复用未使用的 matrix 前 4 个 float；`GraphBinding` 创建 `u_sg_<refName>_ST`，先使用 `.sgb` 默认值，再应用 sidecar 的 `<refName>_ST` 属性覆盖。
- 纹理属性的 `isHDR` 以 flags bit2 标记；`GraphBinding` 创建 `u_sg_<refName>_HDR`，默认 `(1,1,0,0)`，并应用 sidecar 的 `<refName>_HDR` 属性覆盖。采样 shader 在解码向量 `.x > 0` 时调用 Unity `DecodeHDRSample` 等价 helper。
- 属性表 reserved byte 保存 `defaultType` 编码（`white=0/black=1/grey=2/bump=3/linearGrey=4/red=5`），由 `GraphBinding` 在无纹理覆盖时选择 1x1 fallback；Texture2DArray/3D/Cubemap 仍使用白色 fallback。
- 虚拟纹理 v1 不新增 `.sgb` 段：`Sample Virtual Texture` 将 PVT 或 VirtualTexture Property 的层展开为普通 `texture2d` 属性（PVT 为 `<vtName>_LayerN`，Property 可用 `vtLayerRefNames` 覆盖），运行期按普通纹理绑定；层类型与 LOD/Bias/Derivatives 采样语义写入生成的 shader，`sampleQuality` low/high 与 `noFeedback+LOD` 的顶点阶段约束也写入生成代码，真实 Granite VT stack/streaming/feedback 待接。

### 10.3 变体与 blob

```
Variants:
  u32 count
  repeat:
    u64 variantKey
    u32 passCount
    repeat: u16 passId, u16 blobGroupIndex

BlobGroup:
  u32 refCount
  repeat: { u8 stage, u8 backend, u16 pad, u32 offset, u32 size }

Blobs: 原始 dxbc/spirv 字节
```

- `stage`: 0=vertex, 1=fragment, 2=compute。
- `backend`: 0=dxbc, 1=spirv。
- 所有 offset 相对 BLOBS section 起点。
- 变体键位定义（从低到高）：bit0 AlphaClip、bit1 DoubleSided、bit2 Transparent、bit3 NormalMap（若目标使用）、bit4 MaskMap、bit8..15 图关键字位；其余保留。

### 10.4 确定性

- 字符串表按首次出现顺序；属性按 `refName` 排序；pass 按 target 声明顺序；变体按 key 升序；blob 按 `(pass, variant, backend, stage)` 排序。
- 不写入时间戳、绝对路径、主机名、随机 GUID。
- 同一源码 + 同一 shaderc 版本必须产生字节相同的 `.sgb`；`sgbake --check` 强制执行。

## 11. 材质 sidecar

见 [architecture.md](architecture.md) §3.5。补充：

- `shader` 路径相对 sidecar 文件目录；加载器内部解析为规范绝对路径，运行时按路径 mtime 热重载。
- `propertyOverrides` 的键为属性的 `refName`（v1 要求以 `_` 开头）；类型不匹配/未知属性返回错误或告警。启用 Tiling/Offset 的 `texture2d` 额外接受 `<refName>_ST`，值为 `[tilingX, tilingY, offsetX, offsetY]`（对应 Unity `_ST`）；`isHDR` 纹理额外接受 `<refName>_HDR`，值为 `[decodeScale, decodeExponent, 0, alphaScale]`（对应 Unity `DecodeHDRSample` 的 decode instructions）。
- `textureOverrides` 的路径相对 sidecar 文件目录，值可为 string 或 string[]；纹理按属性 sampler 或默认 linear/repeat 采样；图片加入 `RenderScene.images` 后由运行时上传。数组值按图属性解释：`texture2darray` 为 layer 列表，`texture3d` 为 slice 列表，`cubemap` 必须 6 个面（+X,-X,+Y,-Y,+Z,-Z）。分层源要求同尺寸/同格式/单 mip；缺失或非法时使用同维度白色 fallback。
- 未列出的属性使用 `.sgb` 默认值。
- `keywordOverrides` 只允许 `.sgb` 声明的关键字；值可为 boolean、integer 或 enum entry 名；变体缺失时回退基础变体并告警。
- `alphaMode`、`doubleSided`、`materialType` 可覆盖；图 target 的 unlit 还会在加载后改写 `Material.type`。

## 12. 清单与热重载

`graphs/manifest.json`：

```json
{
  "version": 1,
  "generatedBy": "sgbake",
  "graphs": [
    {
      "path": "graphs/lit_test.sgb",
      "contentHash": "0x9f2c...",
      "target": "lit",
      "variants": 8,
      "passes": ["forward", "gbuffer", "depth", "shadow", "motion"]
    }
  ]
}
```

- 运行时启动加载 manifest；缺失时可扫描目录（仅开发模式）。
- 热重载以 `path + contentHash` 为准；hash 变化才重建 program。

## 13. 版本与迁移

| 版本 | 范围 |
|---|---|
| `schemaVersion` | JSON 结构；加载时迁移到当前版本 |
| `typeVersion` | 单个节点 spec 的字段/默认值变更 |
| `formatVersion` | `.sgb` 二进制布局 |
| `generatorVersion` | 生成器实现版本；进入 contentHash |

规则：

- 读取到更高版本 → 拒绝并给出明确错误；不得"尽力解析"。
- 迁移函数集中在 `src/core/migrate.cpp`；每次迁移必须有 fixture 测试（旧 JSON → 新 JSON）。
- 删除节点类型时保留 `legacy` 映射；未知类型在编辑器中显示为 `UnknownNode` 并可删除，不得崩溃（对齐 `Editor/Data/Nodes/LegacyUnknownTypeNode.cs` 的行为）。

## 14. 最小示例

```json
{
  "schemaVersion": 1,
  "kind": "graph",
  "name": "SmokeUnlit",
  "target": { "id": "unlit", "subTarget": "unlit", "settings": { "surfaceType": "opaque", "alphaClip": false, "doubleSided": false, "renderQueue": -1, "precision": "inherit" } },
  "properties": [
    { "name": "Main Tex", "refName": "_MainTex", "type": "texture2d", "default": "textures/grid.png", "sampler": { "filter": "linear", "wrap": "repeat" }, "defaultType": "white" },
    { "name": "Tint", "refName": "_Tint", "type": "color", "default": [1, 1, 1, 1] }
  ],
  "keywords": [],
  "nodes": [
    { "id": 1, "type": "UV", "typeVersion": 1, "position": [0, 0], "settings": { "channel": 0 }, "slots": {} },
    { "id": 2, "type": "TilingAndOffset", "typeVersion": 1, "position": [200, 0], "settings": {}, "slots": { "2": { "mode": "literal", "value": [2, 2] } } },
    { "id": 3, "type": "SampleTexture2D", "typeVersion": 1, "position": [400, 0], "settings": {}, "slots": {} },
    { "id": 4, "type": "Multiply", "typeVersion": 1, "position": [600, 0], "settings": {}, "slots": {} },
    { "id": 5, "type": "BlockUnlitBaseColor", "typeVersion": 1, "position": [800, 0], "settings": {}, "slots": {} }
  ],
  "edges": [
    { "from": { "node": 1, "slot": 0 }, "to": { "node": 2, "slot": 0 } },
    { "from": { "node": 2, "slot": 1 }, "to": { "node": 3, "slot": 2 } },
    { "from": { "node": 3, "slot": 0 }, "to": { "node": 4, "slot": 0 } },
    { "from": { "node": 4, "slot": 2 }, "to": { "node": 5, "slot": 0 } }
  ],
  "groups": [],
  "notes": []
}
```

注：节点类型名与槽位 id 以 `nodes.md` / `nodes-inventory.md` 为准；示例仅展示结构。
