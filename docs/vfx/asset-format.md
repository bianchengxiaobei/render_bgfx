# VFX 资产格式（V1）

> 本文定义 `vfxbake` 的输入 `.vfx.json` 与输出 `.vfxb`。
> 原则：JSON 面向编辑器/手写；`.vfxb` 面向运行时，确定性、无外部依赖、无绝对路径。
> 版本：schema 1 / binary 1。任何不兼容修改必须提升版本并在 reader 中拒绝旧/新格式混用。

---

## 1. `.vfx.json` 顶层

```json
{
  "version": 1,
  "name": "Sparks",
  "properties": [],
  "attributes": [],
  "subgraphs": [],
  "systems": []
}
```

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `version` | int | 是 | 当前为 1 |
| `name` | string | 是 | 资产名（仅调试/字符串表） |
| `properties` | array | 否 | 暴露属性 |
| `attributes` | array | 否 | 自定义粒子属性（最多 8 个，见 §3） |
| `subgraphs` | array | 否 | 子图定义（V1.3） |
| `systems` | array | 是 | 至少 1 个 system |

## 2. 属性（properties）

```json
{ "name": "Tint", "type": "color", "default": [1, 0.8, 0.4, 1] }
{ "name": "MainTex", "type": "texture", "default": "textures/spark.png" }
{ "name": "SizeCurve", "type": "curve", "keys": [[0,0],[0.2,1],[1,0]] }
{ "name": "ColorRamp", "type": "gradient", "keys": [
  {"t":0.0,"color":[1,1,1,1]},
  {"t":0.5,"color":[1,0.5,0.1,1]},
  {"t":1.0,"color":[1,0,0,0]}
] }
```

| type | default | 运行时上传 |
|---|---|---|
| `float` | number | `props[i].x` |
| `int` | int | `props[i].x`（float 位） |
| `bool` | bool | `props[i].x`（0/1） |
| `vector2` | [x,y] | `props[i].xy` |
| `vector3` | [x,y,z] | `props[i].xyz` |
| `vector4` / `color` | [x,y,z,w] | `props[i]` |
| `texture` | path/GUID（编辑器用） | 适配层绑定 sampler；`.vfxb` 只存名称 |
| `curve` | keys | 编译期烘焙 LUT |
| `gradient` | keys | 编译期烘焙 LUT |

- 名称唯一；内置属性名不能作为 property 名（例如 `position`）。
- 属性顺序按 JSON 顺序；运行时按名称查找；`.vfxb` 内索引稳定。
- `texture` 的路径不写入 `.vfxb`（引擎资产管理）；适配层按名称绑定。
- `curve`/`gradient` 的 keys 允许乱序输入，编译器按 `time` 升序稳定排序；重复 `time` 报错（确定性 LUT）。
- 属性名必须是标识符 `[A-Za-z_][A-Za-z0-9_]*`。

## 3. 内置属性（V1）

| 名称 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `position` | float3 | 0 | 世界或局部（按 system space） |
| `velocity` | float3 | 0 | |
| `color` | float3 | 1 | |
| `alpha` | float | 1 | |
| `size` | float | 0.1 | |
| `age` | float | 0 | |
| `lifetime` | float | 1 | |
| `seed` | uint | 0 | 每粒子随机种子 |
| `particleId` | uint | 0 | 稳定 ID |
| `spawnIndex` | uint | 0 | 本帧 spawn 序号 |
| `alive` | bool | true | 死亡回收 |
| `angle` | float | 0 | V1 billboard 旋转 |
| `scale` | float3 | 1 | |
| `targetPosition` | float3 | 0 | 预留 |
| `oldPosition` | float3 | 0 | 碰撞/插值预留 |

- Unity 的 `angleX/Y/Z`、`axisX/Y/Z`、`pivotX/Y/Z` 在 V1.1 后按需加入；V1 billboard 只用 `angle`。
- 自定义属性放在顶层 `attributes` 数组：`{"name":"user0","type":"float4","default":[0,0,0,0]}`，最多 8 个，名称不得与内置冲突。V1 的 layout 固定包含全部内置属性 + 已声明自定义属性（按此表顺序/JSON 顺序）；后续按 block 读写集合收窄。

## 4. System

```json
{
  "name": "Sparks",
  "capacity": 65536,
  "space": "world",
  "bounds": { "mode": "manual", "center": [0,0,0], "extents": [5,5,5] },
  "sort": { "mode": "distance", "order": "ascending" },
  "spawner": {
    "rate": 2000,
    "bursts": [
      { "time": 0, "count": 1000 },
      { "time": 0, "count": 200, "period": 1.0, "repeat": 8 }
    ]
  },
  "init": { "blocks": [] },
  "update": { "blocks": [] },
  "events": { "receive": [], "emit": [] },
  "outputs": []
}
```

| 字段 | 说明 |
|---|---|
| `capacity` | 1..1048576；编译期按 64 对齐（内部 capacity 可能大于请求值） |
| `space` | `world` / `local`（local 使用 effect transform） |
| `bounds.mode` | V1 只支持 `manual`；`center`/`extents` 必填；缺省 `bounds` 为 center `[0,0,0]`、extents `[1,1,1]` |
| `sort.mode` | `none` / `distance` / `custom`；`order` = `ascending` / `descending` |
| `spawner.rate` | 每秒粒子数；可为 0 |
| `spawner.bursts` | 单次（无 period）或周期（有 period/repeat）；`time` 为首次触发时间，`repeat` 为总次数上限（0=无限） |
| `init.blocks` / `update.blocks` | 块列表（见 §6） |
| `events` | V1.2；`receive` 引用事件名与属性映射；`emit` 定义死亡/更新事件 |
| `outputs` | 至少 1 个（V1 只支持 quad） |

## 5. Output

```json
{
  "type": "quad",
  "lit": false,
  "blend": "additive",
  "texture": "MainTex",
  "softParticles": false,
  "sort": "distance"
}
```

| 字段 | 说明 |
|---|---|
| `type` | V1 固定 `quad` |
| `lit` | false = unlit；true = 复用 HDRP 光照 |
| `blend` | `alpha` / `additive` / `premultiply` |
| `texture` | property 名称；缺省使用白色纹理 |
| `softParticles` | 需要 depth；`softDistance` 可在 output 扩展 |
| `sort` | 覆盖 system 排序；`none` 表示不排序 |

- Lit 额外属性（V1.4）：`metallic`、`roughness`（float property 或常量），缺省 0/0.5。
- 输出材质/纹理由适配层绑定，不写入 `.vfxb`。

## 6. Block 列表（V1）

块通用结构：

```json
{ "type": "gravity", "gravity": [0, -9.81, 0] }
```

- 字段值可以是字面量，也可以是表达式对象（§7）。
- 未知 `type`/字段 → 编译错误（带 JSON 路径）。
- 块顺序即执行顺序；`enabled:false` 跳过。

### 6.1 Spawner

| type | 参数 | 说明 |
|---|---|---|
| `constant_rate` | `rate`（也可在 system.spawner 配置） | 常量速率 |
| `burst` | `time`, `count`, `period?`, `repeat?` | 单次/周期 |
| `set_spawn_attribute` | `attribute`, `value` | 写入事件属性（V1.2） |

V1 建议直接用 `system.spawner` 的 rate/bursts；块形式保留给后续自定义。

### 6.2 Init

| type | 参数 |
|---|---|
| `set_position_sphere` | `center`, `radius`, `surface`(bool) |
| `set_position_box` | `center`, `size` |
| `set_velocity_random` | `minSpeed`, `maxSpeed`, `direction`, `coneAngle` |
| `set_velocity_direction` | `direction`, `speed` |
| `set_color` | `color`（表达式） |
| `set_alpha` | `alpha` |
| `set_size` | `size` |
| `set_lifetime` | `lifetime` |
| `set_angle` | `angle` |
| `set_scale` | `scale` |
| `set_attribute` | `attribute`, `value`（自定义属性） |
| `set_random` | `attribute`, `min`, `max` |

### 6.3 Update

| type | 参数 | 说明 |
|---|---|---|
| `gravity` | `gravity` | `velocity += gravity*dt` |
| `drag` | `drag?`（默认 0.5）, `useSize?`（默认 false） | `velocity *= exp(-drag*dt)`；`useSize` 时 `drag *= size^2*scale.x*scale.y` |
| `force` | `force`, `mode?`（`absolute` 默认 / `relative`）, `drag?`（默认 1）, `space?` | `absolute`: `velocity += force*dt`；`relative`: `velocity += (force-velocity)*min(1,drag*dt)`；`space` 当前忽略 |
| `attractor` | `position`, `strength`, `falloff?`（默认 1） | 点吸引/排斥 |
| `turbulence` | `intensity?`（默认 1）, `frequency?`（默认 1）, `octaves?`（默认 1，1..8）, `roughness?`（默认 0.5）, `lacunarity?`（默认 2）, `scroll?`（默认 0）, `mode?`（`relative` 默认 / `absolute`）, `drag?`（默认 1） | Value curl noise；坐标为 `position + scroll*time + 0.5` |
| `integrate` | — | `position += velocity*dt` |
| `age` | — | `age += dt` |
| `reap` | `condition`（默认 `age >= lifetime`） | 标记死亡 |
| `collision_plane` | `normal`, `distance`, `mode`, `restitution`, `friction` | 解析平面 |
| `collision_sphere` | `center`, `radius`, `mode`, `restitution`, `friction` | 解析球 |
| `color_over_life` | `gradient` | 采样 gradient |
| `alpha_over_life` | `curve` | 采样 curve |
| `size_over_life` | `curve` | 采样 curve |
| `custom_hlsl` | `code`, `reads`, `writes` | 受限 HLSL |

碰撞 `mode`：`kill` / `bounce` / `stick`。

V1.1.2 力场限制：
- 原项目 `Force`/`Drag`/`Turbulence` 的 `/ mass` 在 V1.1.2 固定 `mass = 1.0`（当前没有 mass 属性/Set Mass 块）；Gravity 原本就不除 mass。
- Turbulence 使用与 `Turbulence.cs` 相同的 3D curl 结构、默认 `relative`、默认 Value noise 参数；暂不支持 `FieldTransform`、Perlin/Cellular 选择和每粒子 seed 偏移。field 坐标在当前 system 空间计算。
- CPU/GPU 的 cell hash 使用整数 Wang hash（而不是 `VFXNoise.hlsl` 的大浮点 hash），避免 D3D 浮点乘加重排导致 1e-3 以上的漂移；噪声值分布与 curl 结构保持一致。

### 6.4 Output

V1 的 output 不写 block；使用 §5 的 output 配置。后续允许 output block（如 `orient`、`flipbook`）。

## 7. 表达式（Expression DAG）

- 字面量：number / array（按目标类型解释）。
- 节点：`{ "op": "...", ... }`。
- 类型：`float`、`float2`、`float3`、`float4`、`uint`、`bool`。
- 编译器做类型检查、常量折叠；不允许循环引用。

### 7.1 节点类型（V1）

| op | 参数 | 说明 |
|---|---|---|
| `attr` | `name` | 读取属性（内置/自定义） |
| `property` | `name` | 读取暴露属性 |
| `time` | — | 全局时间 |
| `dt` | — | 帧 delta |
| `lifetime_ratio` | — | `age / lifetime` |
| `add`/`sub`/`mul`/`div` | `a`, `b` | 标量/向量 |
| `min`/`max`/`clamp` | `a`, `b`, `c?` | |
| `lerp` | `a`, `b`, `t` | |
| `remap` | `x`, `inMin`, `inMax`, `outMin`, `outMax` | |
| `sin`/`cos`/`abs`/`saturate` | `x` | |
| `random` | `min`, `max`, `seed?` | 每粒子随机 |
| `noise` | `position`, `frequency`, `seed?` | value/simplex |
| `sample_curve` | `curve`, `t` | LUT |
| `sample_gradient` | `gradient`, `t` | LUT |
| `sample_texture` | `texture`, `uv` | V1.1+ |
| `select` | `condition`, `a`, `b` | |
| `swizzle` | `value`, `mask` | 如 `xyz` |
| `attribute` | 同 `attr` | 兼容别名 |

类型规则（V1.0b.3）：

- `float`、`float2/3/4`、`uint`、`int`、`bool`；标量可广播到 `floatN`，但不允许向量截断或向量隐式转标量。
- `int/uint -> float`、`int <-> uint` 生成显式 cast 节点；`bool` 只用于条件，不参与算术。
- `add/sub/mul/div/min/max/clamp/lerp/remap/sin/cos/abs/saturate/select/swizzle` 在编译期做类型提升；字面量可折叠的表达式会折叠。
- `attr/property/time/dt/lifetime_ratio/random/noise/sample_curve/sample_gradient` 不做常量折叠；`random` 必须由 seed（缺省 0）确定 code，不能把编译期随机数写死。
- 表达式内部引用错误、类型错误、未知参数/op 在解析阶段报错，路径如 `systems[0].init.blocks[0].alpha.b`。
- 生成的 HLSL 使用 `VFX_ATTR`/`VFX_PROP`/`VFX_TIME`/`VFX_DT`/`VFX_LIFETIME_RATIO`/`VFX_RAND*`/`VFX_NOISE`/`VFX_SAMPLE_*` 宏；V1.0b.5 已由 compiler 生成对应宏、buffer 声明和属性 local/store。
- `sample_texture` 属于 V1.1；`sample_curve/gradient` 的 name 当前必须是 curve/gradient property。

### 7.2 示例

```json
{ "op": "sample_gradient", "gradient": "ColorRamp",
  "t": { "op": "lifetime_ratio" } }
```

```json
{ "op": "mul",
  "a": { "op": "property", "name": "Size" },
  "b": { "op": "sample_curve", "curve": "SizeCurve", "t": { "op": "lifetime_ratio" } } }
```

## 8. 子图（V1.3）

```json
{
  "name": "RandomVelocity",
  "inputs": [{"name":"Speed","type":"float","default":1}],
  "outputs": [{"name":"Velocity","type":"float3"}],
  "init": {"blocks": []},
  "update": {"blocks": []}
}
```

- 父图通过 `{"op":"subgraph","name":"RandomVelocity","args":{"Speed":...}}` 调用。
- V1 只支持纯函数子图（无 spawner/output/事件），递归深度 ≤16，禁止循环。
- 编译器展开后按普通 block 处理；`.vfxb` 不保留子图结构（只保留调试名）。

## 9. 自定义 HLSL（V1.3）

```json
{
  "type": "custom_hlsl",
  "reads": ["position", "velocity", "age"],
  "writes": ["velocity"],
  "code": "velocity += VFX_ATTR(velocity) * 0.5 * VFX_DT;"
}
```

限制：
- 只能使用注入宏与内置数学函数；不能声明 buffer/uniform/texture；不能 `#include`/`#define`。
- 不能修改未在 `writes` 声明的属性；`reads` 未声明的属性不可访问。
- 不能使用 `discard`（output FS 的自定义除外；V1 不允许）。
- 必须同时通过 dxbc + spirv 编译；否则烘焙失败。
- 编译器在生成的 kernel 中插入一个显式作用域 `{ ... }`，避免变量泄漏。

## 10. `.vfxb` 二进制

### 10.1 Header（64B）

| 偏移 | 大小 | 字段 |
|---|---|---|
| 0 | 4 | magic `VFXB` |
| 4 | 2 | version = 1 |
| 6 | 2 | headerSize = 64 |
| 8 | 4 | flags（bit0=little-endian；bit1 保留 debug） |
| 12 | 4 | sectionCount = 9 |
| 16 | 8 | contentHash（FNV-1a 64，覆盖 offset 64 之后全部字节） |
| 24 | 4 | compilerVersion = 0x00010000 |
| 28 | 4 | assetNameOffset（STRINGS 段偏移；0 = 空名） |
| 32 | 32 | reserved（对齐，全 0） |

所有整数小端。section table 和 section content 一起参与 `contentHash`；`vfxbake --check` 逐字节比较。

### 10.2 Section table

每条 16B：`{ id:u32, offset:u32, size:u32, alignment:u32 }`，按 id 升序；`offset` 是文件绝对偏移。

| id | 名称 | 内容 |
|---|---|---|
| 1 | STRINGS | 去重、排序的 UTF-8 null-terminated 字符串表；offset 0 为空串 |
| 2 | PROPERTIES | `u32 count` + 32B/条（见下） |
| 3 | LUTS | Curve/Gradient 原始 RGBA16F 字节，16B 对齐 |
| 4 | SYSTEMS | `u32 count` + 64B/条 + bursts |
| 5 | BUFFERS | `u32 count` + 24B/条 |
| 6 | KERNELS | `u32 count` + 44B/条 + shader blobs |
| 7 | BINDINGS | `u32 count` + 8B/条 |
| 8 | OUTPUTS | `u32 count` + 24B/条 |
| 9 | EVENTS | `u32 count`；V1.0b.5 为 0 |
| 10 | DEBUG | 保留；V1.0b.5 不生成 |

#### PROPERTIES（32B/条）

| 偏移 | 字段 |
|---|---|
| 0 | `nameOff:u32` |
| 4 | `type:u8` |
| 5 | `flags:u8`（bit0=有 LUT） |
| 6 | `count:u16`（标量/向量组件数） |
| 8 | `defaultWords[4]:u32`（float bits；int/bool 也按默认 float 位存） |
| 24 | `lutOffset:u32`（相对 LUTS section；无则 `0xffffffff`） |
| 28 | `lutSize:u32` |

`type` 与 `vfx::PropertyType` 数值一致。Curve/Gradient 每条 LUT 固定 256 个 RGBA16F sample（`256 * 8 = 2048`B）；curve 仅使用 `.x`。

#### SYSTEMS（64B/条）

| 偏移 | 字段 |
|---|---|
| 0 | `nameOff:u32` |
| 4 | `requestedCapacity:u32` |
| 8 | `capacity:u32`（64 对齐后的实际值） |
| 12 | `space:u8` |
| 13 | `sortMode:u8` |
| 14 | `sortOrder:u8` |
| 15 | `outputCount:u8` |
| 16 | `boundsCenter:3*f32` |
| 28 | `boundsExtents:3*f32` |
| 40 | `spawnerRate:f32` |
| 44 | `burstOffset:u32`（相对 SYSTEMS section） |
| 48 | `burstCount:u32` |
| 52 | `firstKernel:u32`（全局 KERNELS 记录起点） |
| 56 | `kernelCount:u32` |
| 60 | `reserved:u32` |

Burst 16B：`{ time:f32, count:u32, period:f32, repeat:u32 }`；无周期/重复时为 0。

#### BUFFERS（24B/条）

| 偏移 | 字段 |
|---|---|
| 0 | `system:u32` |
| 4 | `kind:u8` |
| 5 | `flags:u8` |
| 6 | `reserved:u16` |
| 8 | `size:u32` |
| 12 | `stride:u32` |
| 16 | `initialOffset:u32`（相对 LUTS；无则 `0xffffffff`） |
| 20 | `initialSize:u32` |

V1.0b.5 每 system 固定 7 条：`0=attributes`、`1=deadList`、`2=indirection`、`3=renderCount`、`4=spawnCount`、`5=indirectArgs`、`6=bounds`。deadList 为 `2 + capacity` uints；indirectArgs 为 32B。

#### KERNELS（44B/条）

| 偏移 | 字段 |
|---|---|
| 0 | `system:u32` |
| 4 | `task:u8` |
| 5 | `stage:u8` |
| 6 | `backend:u8` |
| 7 | `reserved:u8` |
| 8 | `threadGroupX/Y/Z:3*u32` |
| 20 | `bindingStart:u32`（BINDINGS 条目索引） |
| 24 | `bindingCount:u32` |
| 28 | `blobOffset:u32`（相对 KERNELS section） |
| 32 | `blobSize:u32` |
| 36 | `program:u32` |
| 40 | `reserved:u32` |

记录之后是 4B 对齐的 shader blob。每个 logical shader 每个 backend 一条记录；output VS/FS 共享 `program`，compute kernel 的 `program` 唯一。

- `task`：`0=spawner`、`1=deadListInit`、`2=init`、`3=update`、`4=outputUpdate`、`5=fillIndirect`、`6=outputVertex`、`7=outputFragment`。
- `stage`：`0=vertex`、`1=fragment`、`2=compute`。
- `backend`：`0=dxbc`、`1=spirv`。

#### BINDINGS（8B/条）

`{ kind:u8, stage:u8, access:u8, reserved:u8, index:u32 }`；`kind` 0=buffer、1=texture。`stage` 为 shader binding slot；compute VFX 使用 0–4，output VS 使用 12/13，output FS sampler 使用 0。

#### OUTPUTS（24B/条）

| 偏移 | 字段 |
|---|---|
| 0 | `system:u32` |
| 4 | `program:u32` |
| 8 | `blend:u8` |
| 9 | `lit:u8` |
| 10 | `sortMode:u8` |
| 11 | `sortOrder:u8` |
| 12 | `textureProperty:u32`（无则 `0xffffffff`） |
| 16 | `flags:u32`（bit0=有 texture property） |
| 20 | `reserved:u32` |

### 10.3 Shader blob

- 每个 kernel/output 存两份：`backend=dxbc`、`backend=spirv`。
- blob 原样存储 shaderc 输出，4B 对齐；运行时按当前后端选择。
- 缺失后端 → 加载失败（明确报错）；V1 要求双后端齐全。
- `threadGroupX/Y/Z` 与 shader 内 `NUM_THREADS` 必须一致；加载时校验（可选）。

### 10.4 确定性规则

- 字符串表排序；属性按 JSON 顺序；system 按 JSON 顺序；kernel 按 `(system, task, stage, backend)` 固定顺序。
- LUT 字节由控制点确定性求值；浮点格式固定（RGBA16F little-endian）。
- 禁止写入时间戳、绝对路径、机器名。
- `vfxbake --check`：重新烘焙并逐字节比较；任何差异视为失败。

### 10.5 版本/兼容

- reader 只接受 `version == 1`（V1）。
- 新增可选字段用 flags 表示；不改变布局。
- 不兼容修改 → version+1；旧 runtime 明确拒绝。
- `capacity` 实际值可能因 64 对齐而增大；资产记录 requested/actual 两值，运行时用 actual。
- V1.0b.6 reader 在 `vfx/src/runtime/runtime_asset.cpp` 校验 magic/version/header size/flags/contentHash/section 范围/双后端完整性；`.vfxb` 不依赖 compiler target 加载。V1.0b.8 起 header offset 28 的 `assetNameOffset` 指回 STRINGS 段，reader 回填 `Asset::name`；0 表示空名，兼容旧资产。
- output VS/FS 的 binding 记录同样来自 KERNELS；runtime 用 VS 的 12/13 buffer 绑定和 FS 的 sampler 绑定。无 texture property 时绑定 1×1 白纹理。
- runtime 对 Curve/Gradient 默认 LUT 创建 RGBA16F 纹理；`setCurve`/`setGradient` 通过 `Device::updateTexture` 重传。当前 unlit shader 仍使用编译期分段函数，LUT 采样留到 V1.1。

## 11. 校验规则（摘要）

- `capacity >= 1` 且 ≤ 1048576；64 对齐后记录 actual。
- `sort.mode != none` 时 V1 要求 `actual capacity <= 262144`；更大容量必须关闭排序或等待 V2（编译器报错，不静默降级）。
- `spawner.rate >= 0`；burst `count >= 0`；`period > 0`（若有周期）；`repeat >= 1`。
- `bounds.mode == manual` 时 center/extents 必填；extents > 0。
- `sort.mode != none` 时每个 output 必须显式声明 `sort`（`none`/`distance`/`custom`）；`none` 表示该 output 不排序。
- 属性名唯一；自定义属性 ≤8；不得与内置冲突。
- 表达式内部类型匹配、无环、嵌套深度 ≤64（V1.0b.3）；block 字段目标类型在 V1.0b.5 kernel 编译入口校验；子图深度 ≤16 在 V1.3 校验。
- 事件图无环（V1）；事件属性读写一致。
- 自定义 HLSL 只读/写声明集合一致。
- Lit output 的 metallic/roughness 属性必须存在或使用默认值。
- 所有 kernel 的 dxbc + spirv 必须存在。

**分阶段实现**：V1.0b.1 实现 schema/路径/数值/枚举/引用/curve-gradient 确定性校验；V1.0b.3 已实现表达式内部类型/折叠/DAG 校验（parser 接入 `validateGraphExpressions`），block 字段目标类型在 V1.0b.5；子图深度/循环在 V1.3，事件图在 V1.2，自定义 HLSL 语义在 V1.3，lit metallic/roughness 在 V1.4，shader blob 完整性在 V1.0b.5；runtime reader 在 V1.0b.6。

## 12. `vfxbake` CLI

```text
vfxbake --input <effect.vfx.json> --output <effect.vfxb>
        --shaderc <path> --bgfx-src <path>
        [--shader-pack <dir>] [--backend dxbc,spirv] [--check] [--verbose]

vfxbake --check <asset-dir> --shaderc <path> --bgfx-src <path>
```

- 退出码：0 成功；1 校验/编译失败；2 参数错误。
- 失败信息包含 JSON 路径（如 `systems[0].update.blocks[3].type`）与 shaderc 输出摘要。
- `--check` 单文件模式比较 `--output`；批量模式递归查找 `*.vfx.json`，并比较同目录同名 `.vfxb`，不写文件；用于 CI。
- `--backend` 默认 `dxbc,spirv`；V1 验收要求双后端。
- 当前未提供 `--dump-shaders`；生成 shader 仅存在于编译临时目录。
