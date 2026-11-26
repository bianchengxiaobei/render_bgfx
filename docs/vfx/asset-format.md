# VFX 资产格式（V1）

> 本文定义 `vfxbake` 的输入 `.vfx.json` 与输出 `.vfxb`。
> 原则：JSON 面向编辑器/手写；`.vfxb` 面向运行时，确定性、无外部依赖、无绝对路径。
> 版本：schema 1 / binary 6。任何不兼容修改必须提升版本并在 reader 中拒绝旧/新格式混用。

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
| `subgraphs` | array | 否 | 子图定义（V1.3.1；parse 时展开） |
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
| `oldPosition` | float3 | 0 | 上一帧 position；init/update codegen 维护，V3.0 motion vectors 使用 |

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
| `bounds.mode` | `manual`（缺省）或 `automatic`；manual 时 `center`/`extents` 必填，automatic 可省略 center/extents。V3.5 起 `bounds.padding` 可选，仅 automatic 生效（由 GPU alive 粒子计算后加 padding） |
| `sort.mode` | `none` / `distance` / `custom` / `cameraDepth` / `youngest`；`order` = `ascending` / `descending`。V1.2.2 `distance` 的 key 为相机空间平方距离；`custom` 读取系统声明的 scalar `sortKey` 属性；V3.8 `cameraDepth` 为相机前向距离，`youngest` 为 `age` |
| `spawner.rate` | 每秒粒子数；可为 0 |
| `spawner.bursts` | 单次（无 period）或周期（有 period/repeat）；`time` 为首次触发时间，`repeat` 为总次数上限（0=无限） |
| `init.blocks` / `update.blocks` | 块列表（见 §6） |
| `events` | V1.2.3/4；`receive`/`emit` 按事件名连接；无 emit 的 receive 是 CPU 事件；属性映射见 §5.2 |
| `outputs` | 至少 1 个（V1 只支持 quad） |

## 5. Output

```json
{
  "type": "quad",
  "lit": false,
  "blend": "additive",
  "texture": "MainTex",
  "softParticles": true,
  "softDistance": 1.0,
  "castShadows": false,
  "generateMotionVector": true,
  "sortPriority": 0,
  "metallic": "Metallic",
  "roughness": 0.5,
  "sort": "distance",
  "sortOrder": "ascending"
}
```

| 字段 | 说明 |
|---|---|
| `type` | V1 固定 `quad` |
| `lit` | false = unlit；true = 复用 HDRP 光照 |
| `blend` | `alpha` / `additive` / `premultiply` |
| `texture` | property 名称；缺省使用白色纹理 |
| `softParticles` | V1.4.2；需要 scene depth；透明 blend；缺省 false |
| `softDistance` | V1.4.2；>0（最小 0.001），缺省 0.1；与粒子 eye depth 的 fade 距离 |
| `castShadows` | V3.1；进入该 output 的 shadow caster pass；缺省 false；透明/加性输出按纹理 alpha 裁剪，opaque 输出不裁剪 |
| `generateMotionVector` | V3.2；是否写场景 motion-vector buffer；缺省 true 以兼容 V3.0，显式 false 跳过 MV draw |
| `sortPriority` | V3.7；output 提交顺序，int32，越小越先；缺省 0，priority 相同保持声明顺序 |
| `metallic` | V1.4.1；0..1 常量或 float property 名；缺省 0 |
| `roughness` | V1.4.1；0..1 常量或 float property 名；缺省 0.5 |
| `sort` | system 排序开启时必需；V1.2.2 所有 output 默认共享 system 排序/indirection，V3.10 起可写与 system 不同的 criterion（`distance`/`custom`/`cameraDepth`/`youngest`）；`none` 仅用于 system 未排序 |
| `sortOrder` | V3.9；可选 `ascending` / `descending`，覆盖该 output 的绘制方向；缺省继承 `system.sort.order`；仅 system 排序开启时有效 |

- V1.4.1 lit output：FS 填充 `SurfaceData`，调用 `hbgSampleDirectionalShadow`/`hbgEvaluateLightLoop`；metallic/roughness 支持常量或 float property，property 通过 `u_vfxProp_*` 在运行时上传。VFX lit 使用全局 light-list fallback，point/area shadow sampler 不启用（12/13 保留给 attribute/indirection）。
- V1.4.2 soft output：FS 在 stage 4 采样 `s_vfxSceneDepth`，用 `u_vfxSoftParams`（x=distance，yz=1/viewport）和 `u_global_view`/`u_global_invViewProj` 线性化 scene/particle depth；`fade=smoothstep(saturate((sceneZ-selfZ)/softDistance))`。输出 pass 显式 `readTexture(depth)`，depth attachment 使用 read-only flag。无 depth 时绑定白色 fallback。
- 输出材质/纹理由适配层绑定，不写入 `.vfxb`。
- V3.1 `castShadows` 缺省 false；只控制 shadow caster pass，不改变 color output；透明/加性 output 在 shadow FS 中按 `s_vfxMain` alpha < 0.001 裁剪，opaque output 不裁剪。
- V3.2 `generateMotionVector` 缺省 true；只控制固定 `vfx.motionVectors` pass，不改变 color/shadow output。

### 5.1 排序语义（V1.2.2 / V3.8 / V3.9 / V3.10）

- `distance`：key = `dot(worldPosition - cameraPosition, worldPosition - cameraPosition)`；`ascending` 近到远，`descending` 远到近。
- `cameraDepth`（V3.8）：key = `dot(worldPosition - cameraPosition, cameraForward)`；`ascending` 由近到远，与 Unity `VFX_DEPTH_SORT_KEY` 一致。
- `youngest`（V3.8）：key = `age`；`ascending` 最年轻优先，对应 Unity `VFX_YOUNGEST_SORT_KEY`。
- `custom`：key = scalar `sortKey` 属性；没有该属性时编译失败。
- key 相等时始终按 `particleIndex` 升序（tie-break），与 `Sort.compute` 一致。
- `capacity <= 4096`：一次 bitonic dispatch 排序并直接写 indirection。
- `capacity > 4096`：4096 个 KVP 为一块，逐块 bitonic pre-pass 写 `sortScratch`，再 merge 到最终 indirection；每块单独 dispatch，避免大容量 LDS 并发超限。
- V1.2.2 所有 output 共享 system 排序；`output.sort` 只用于校验，不能覆盖 system 排序。
- V3.9 `output.sortOrder` 可覆盖单个 output 的绘制方向；system key/indirection 仍共享，反向 output 从 indirection 尾部读取，相等 key 的 tie 顺序也随之反向。
- V3.10 `output.sort` 可选择与 system 不同的 criterion；runtime 在 output/MV/shadow draw 前按需串行重排共享 KVP/indirection，相同 criterion 的连续 draw 复用列表。v1 不是并发独立 sorted list；system criterion 是缺省和基础 list。

### 5.2 GPU 事件（V1.2.3）

事件通过名称连接：一个 system 的 `events.emit` 与另一个 system 的 `events.receive` 使用同一 `name`。V1.2.3 限制：

- 每个 system 最多一个 `emit`、一个 `receive`，且同一 system 不能同时收发（事件链留到 V1.3）。
- 一个事件名最多一个 producer、一个 consumer；receiver 的 `spawner.rate` 必须为 0 且 `bursts` 为空。
- 事件 payload 最多 4 个属性；每个属性占一个 `float4` slot（标量/uint/int/bool 转为 float，V1 有精度限制）。
- event link 容量 = receiver 的 64 对齐 capacity；总事件缓冲超过 64 KiB 时编译器拒绝（V1 单 storage 限制）。
- 只有 `receive`、没有同名 `emit` 的事件是 **CPU 事件**；CPU receiver 必须显式声明 `receive.attributes`，且不能有 CPU spawner。

```json
"events": {
  "emit": [{
    "name": "death",
    "attributes": {
      "position": {"op": "attr", "name": "position"},
      "color": {"op": "attr", "name": "color"}
    }
  }],
  "receive": [{
    "name": "death",
    "attributes": {
      "position": "position",
      "color": "color"
    }
  }]
}
```

- `emit.attributes`：payload 名称 → 在 producer update 结束后的表达式；表达式使用 producer 的粒子属性。
- `receive.attributes`：consumer 属性名 → emit payload 名称。省略/空对象时按同名属性自动映射。
- 未声明 `trigger_event` 时，producer 粒子在本次 update 从 alive 变为 dead 时自动发 1 个事件（死亡子发射器）。
- `trigger_event` block（仅 update）：`event` 必填且匹配 emit 名，`count` 可选（默认 1），`condition` 可选。无 `condition` 时在死亡转换时触发；有 `condition` 时按表达式触发（V1 表达式没有比较运算，可由 bool 属性/select 构造）。
- producer 对每个粒子最多写 `capacity` 个事件；超过 receiver capacity 的 accepted 上限后写入 `dropped` 计数，不越界。
- 事件索引缓冲保存 producer 的 `particleIndex`，payload 在 emit 时复制到独立 event attribute slots；consumer init 在正常 init blocks 之后应用 payload，因此 payload 可覆盖 init 属性。
- V1.2.3 的 event buffer 是每个 effect 一份全局 storage；每个链路头部 5 个 uint：`elementCount`、accepted `totalCount`、本帧全局 `prefix`、`dropped`、`frameCount`，随后 `capacity` 个 source index 和 `capacity * 16` 个 payload words。
- 每帧执行顺序：reset（首帧）→ 无事件输入的 producer simulation → Sklansky event prefix sum（128 threads）→ consumer simulation。prefix 将本帧 accepted 事件数累加到全局 base，并在 consumer init 前把 `elementCount` 清零；`totalCount` 累计 accepted 事件，`dropped` 记录被 capacity 丢弃的尝试。
- `Diagnostics::eventCount` 在 V1.2.3/4 只表示参与的事件链数量；V1.4.4 `FrameDiagnostics.vfx_events` 仍暴露事件链数；精确事件数继续通过 reference 的 event buffer `frameCount`/`totalCount` 读回，默认帧循环不做同步读回。

#### CPU 事件（V1.2.4）

```json
"events": {
  "receive": [{
    "name": "hit",
    "attributes": {
      "position": "position",
      "color": "color"
    }
  }]
}
```

- 没有同名 `emit` 的 `receive` 编译为 CPU event link；`Effect::sendEvent("hit", attributes)` 入队。
- `receive.attributes` 的 key 是 consumer 属性，value 是 CPU `EventAttribute.name`。必须显式声明；最多 4 个 payload 属性。
- 缺少某个 event attribute 时使用 consumer 属性默认值；类型不兼容或未知事件名通过 runtime error callback 报告，事件不进入队列。
- runtime 维护 CPU pending queue 与 `vfx.cpu.events` staging buffer；每帧按发送顺序写入 event slot，再通过 `event_cpu_upload`（task 13）复制到全局 `vfx.events`，随后同一 Sklansky prefix/consumer init 路径消费。
- CPU 事件容量同样是 receiver 的 64 对齐 capacity；超出队列容量的调用写入 `dropped`，不越界。
- 每次 `sendEvent` 计 1 个事件；V1 不支持 CPU 事件属性数组/多属性集。

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
| `custom_hlsl` | `code`, `reads?`, `writes?`（V1.3.2） |

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
| `trigger_event` | `event`, `count?`, `condition?` | 触发 emit 事件；无 condition 时死亡触发，V1.2.3 |
| `collision_plane` | `normal`, `distance`, `mode`, `restitution`, `friction` | 解析平面 |
| `collision_sphere` | `center`, `radius`, `mode`, `restitution`, `friction` | 解析球 |
| `color_over_life` | `gradient` | `t = saturate(age/lifetime)`；覆盖 RGB |
| `alpha_over_life` | `curve` | `t = saturate(age/lifetime)`；覆盖 alpha |
| `size_over_life` | `curve` | `t = saturate(age/lifetime)`；覆盖 size |
| `custom_hlsl` | `code`, `reads?`, `writes?` | 受限 HLSL；init/update 均可用 |

碰撞 `mode`：`kill` / `bounce` / `stick`，默认 `bounce`；`restitution` 默认 `0.1`，`friction` 默认 `0`。`normal` 在运行时归一化，`distance` 是归一化平面到原点的有符号距离。`kill` 标记死亡；`bounce` 反射法线速度并按 friction 衰减切向速度；`stick` 将粒子放到碰撞点并清零速度。当前不使用粒子半径，也不写碰撞事件。

Over-life 块采样对应 Curve/Gradient property；`t = saturate(age / max(lifetime, 1e-6))`。`color_over_life` 仅覆盖 RGB，alpha 由 `alpha_over_life` 独立控制（原 `ColorOverLife` 的 ColorAndAlpha/Multiply composition 当前简化，见 D14）。Curve/Gradient 的 LUT 采样为 `u = (t * 255 + 0.5) / 256`，同类型 property 打包为 `256 × N` 纹理、row 顺序为 JSON property 顺序；compute stage 9=curve、10=gradient。

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
- `sample_texture` 属于 V1.1；`sample_curve/gradient` 的 name 当前必须是 curve/gradient property，V1.1.4 起在 GPU 上使用 stage 9/10 的 LUT 双线性采样；CPU reference 仅对 over-life block 做控制点求值。

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

## 8. 子图（V1.3.1）

```json
{
  "name": "OffsetColor",
  "inputs": [
    {"name":"Color", "type":"vector3", "default":[0.1,0.1,0.1]},
    {"name":"Offset","type":"vector3", "default":[0.1,0.2,0.3]}
  ],
  "outputs": [{
    "name":"Result", "type":"vector3",
    "value": {"op":"add", "a":{"op":"input","name":"Color"},
              "b":{"op":"input","name":"Offset"}}
  }],
  "init": {"blocks": []},
  "update": {"blocks": []}
}
```

- 表达式调用：`{"op":"subgraph","name":"OffsetColor","args":{"Color":...},"output":"Result"}`。只有一个 output 时可省略 `output`；多个 output 必须指定。
- 纯函数子图的 `value` 使用 `{"op":"input","name":"Color"}`（`arg` 同义）引用输入；为兼容手写图，子图内部的 `{"op":"property","name":"Color"}` 也会优先绑定同名 input。未传 args 时使用 input `default`，没有 default 则报错。
- block 子图调用：`{"type":"subgraph","name":"Fall","args":{"Gravity":...}}`；按父 block 所在 init/update stage 展开子图对应的 `init.blocks`/`update.blocks`。子图 block 可递归调用其他子图。
- V1 子图不能包含 spawner/output/事件；递归深度 ≤16，禁止直接或间接循环；循环/过深/未知 input/output/缺失参数均带 JSON 路径报错。
- 展开在 `parseGraphJson` 阶段完成：`graph.subgraphs` 被清空，系统 block/表达式替换为普通内容；`.vfxb` 不保留子图结构。

## 9. 自定义 HLSL（V1.3.2）

`custom_hlsl` 是 init/update 的语句片段，不是完整函数定义：

```json
{
  "type": "custom_hlsl",
  "reads": ["position", "velocity"],
  "writes": ["position"],
  "code": "VFX_SET_ATTR(position, VFX_ATTR(position) + VFX_ATTR(velocity) * VFX_DT);"
}
```

- `code` 必填（≤32 KiB）；`reads`/`writes` 可选，名称必须是内置或 `attributes` 声明的属性。
- 读属性必须用 `VFX_ATTR(name)`，且 `name` 在 `reads`；写属性必须用 `VFX_SET_ATTR(name, value)`，且 `name` 在 `writes`。读改写需要同时声明 reads/writes。
- 注入宏：`VFX_ATTR(name)`、`VFX_SET_ATTR(name, value)`、`VFX_PROP(name)`、`VFX_TIME`、`VFX_DT`；`VFX_LIFETIME_RATIO` 需要声明 reads `age` 与 `lifetime`。
- 禁止直接访问生成的 `a_*`、`attributes.Load_*`/`Store_*`、`s_vfx*`/`u_vfx*`、`gl_*`；禁止 `#` 预处理指令（含 include/define）、`buffer`/`uniform`/texture/sampler 声明、`return`/`discard`/barrier/static/volatile。
- 编译器在 init/update kernel 中生成显式作用域 `{ ... }`，避免局部变量泄漏；自定义代码只通过声明的 reads/writes 影响属性。
- 语法/类型最终由 shaderc 的 dxbc + spirv 双后端编译把关；失败时资产烘焙失败。校验错误包含 JSON 路径和 HLSL 行/列（`code:line:column`）。
- V1 不支持函数定义、include、平台特定 intrinsic、output stage 自定义块；完整 HLSL 语义留给后续版本。

## 10. `.vfxb` 二进制

### 10.1 Header（64B）

| 偏移 | 大小 | 字段 |
|---|---|---|
| 0 | 4 | magic `VFXB` |
| 4 | 2 | version = 6 |
| 6 | 2 | headerSize = 64 |
| 8 | 4 | flags（bit0=little-endian；bit1 保留 debug） |
| 12 | 4 | sectionCount = 9，最多 11（可选 BOUNDS id 11、CULLING id 12） |
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
| 8 | OUTPUTS | `u32 count` + 32B/条 |
| 9 | EVENTS | `u32 count` + `u32 bufferSize` + link 记录；见 §10 |
| 10 | DEBUG | 保留；V1.0b.5 不生成 |
| 11 | BOUNDS | V3.5 可选；`u32 count` + `count * 3*f32` padding；无 padding 资产不生成 |
| 12 | CULLING | V3.6 可选；`u32 flags`；无 culling 资产不生成 |

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
| 13 | `sortMode:u8`（0=none, 1=distance, 2=custom, 3=cameraDepth, 4=youngest） |
| 14 | `sortOrder:u8` |
| 15 | `outputCount:u8` |
| 16 | `boundsCenter:3*f32` |
| 28 | `boundsExtents:3*f32` |
| 40 | `spawnerRate:f32` |
| 44 | `burstOffset:u32`（相对 SYSTEMS section） |
| 48 | `burstCount:u32` |
| 52 | `firstKernel:u32`（全局 KERNELS 记录起点） |
| 56 | `kernelCount:u32` |
| 60 | `boundsMode:u8`（0=manual，1=automatic；旧资产为 0） |
| 61 | `reserved:3*u8` |

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

V1.0b.5 每 system 固定 7 条；V1.2.1 为 8 条；V1.2.2 起固定 **9** 条：`0=attributes`、`1=deadList`、`2=indirection`、`3=renderCount`、`4=spawnCount`、`5=indirectArgs`、`6=bounds`、`7=renderKVP`、`8=sortScratch`。deadList 为 `2 + capacity` uints；indirectArgs 为 32B；renderKVP 为 `(1 + capacity) * 8`B（word0=key、word1=counter，随后 `capacity` 个 `{key:f32, index:u32}`）；sortScratch 为 `capacity * 8`B，仅存 `{key,index}`。

#### KERNELS（44B/条）

| 偏移 | 字段 |
|---|---|
| 0 | `system:u32` |
| 4 | `task:u8` |
| 5 | `stage:u8` |
| 6 | `backend:u8` |
| 7 | `sortModeTag:u8`（V3.10；extra sort pipeline 的 criterion，0/旧资产 = 基础 pipeline） |
| 8 | `threadGroupX/Y/Z:3*u32` |
| 20 | `bindingStart:u32`（BINDINGS 条目索引） |
| 24 | `bindingCount:u32` |
| 28 | `blobOffset:u32`（相对 KERNELS section） |
| 32 | `blobSize:u32` |
| 36 | `program:u32` |
| 40 | `reserved:u32` |

记录之后是 4B 对齐的 shader blob。每个 logical shader 每个 backend 一条记录；output VS/FS 共享 `program`，compute kernel 的 `program` 唯一。event prefix/reset 为 global program，`system=0xffffffff`。

- `task`：`0=spawner`、`1=deadListInit`、`2=init`、`3=update`、`4=outputUpdate`、`5=fillIndirect`、`6=outputVertex`、`7=outputFragment`、`8=sortBitonicFinal`、`9=sortPrePass`、`10=sortMerge`、`11=eventPrefix`、`12=eventReset`、`13=eventCpuUpload`、`14=boundsInit`、`15=bounds`、`16=sortReset`（V3.10 extra pipeline 专用）。
- `stage`：`0=vertex`、`1=fragment`、`2=compute`。
- `backend`：`0=dxbc`、`1=spirv`。

#### BINDINGS（8B/条）

`{ kind:u8, stage:u8, access:u8, reserved:u8, index:u32 }`；`kind` 0=buffer、1=texture、2=global event buffer、3=CPU event staging buffer。`stage` 为 shader binding slot；compute VFX 使用 0–4，output VS 使用 12/13，output FS sampler 使用 0；soft scene depth 为 texture kind、stage 4、`index=0xfffffffe`（`kSceneDepthBindingIndex`）。event kind 的 `index` 保留为 0。

#### EVENTS

`u32 linkCount` + `u32 eventBufferSizeBytes`，随后每条 link 32B：

| 偏移 | 字段 |
|---|---|
| 0 | `producerSystem:u32` |
| 4 | `consumerSystem:u32` |
| 8 | `capacity:u32`（consumer 对齐 capacity） |
| 12 | `bufferOffsetWords:u32` |
| 16 | `payloadOffsetWords:u32` |
| 20 | `nameOff:u32`（字符串表） |
| 24 | `payloadCount:u32`（≤4） |
| 28 | `receiveCount:u32` |

随后 `payloadCount` 条 `{ nameOff:u32, slot:u32, type:u8+pad3, default:4*f32 }`（28B）和 `receiveCount` 条 `{ attributeNameOff:u32, slot:u32 }`。CPU link 的 `producerSystem=0xffffffff`，`type/default` 供 `sendEvent` 校验/补默认值。event buffer 每 link：`elementCount/totalCount/prefix/dropped/frameCount` 5*uint，随后 `capacity` 个 source index，再随后 `capacity*16` 个 payload float words。V1 总 event buffer ≤64 KiB；reader 校验 offset/capacity/slot/字符串。

#### OUTPUTS（32B/条）

| 偏移 | 字段 |
|---|---|
| 0 | `system:u32` |
| 4 | `program:u32` |
| 8 | `blend:u8` |
| 9 | `lit:u8` |
| 10 | `sortMode:u8`（effective output criterion；0=none, 1=distance, 2=custom, 3=cameraDepth, 4=youngest；V3.10 可不同于 system） |
| 11 | `sortOrder:u8`（effective output order；V3.9 可由 JSON `sortOrder` 覆盖 system order） |
| 12 | `textureProperty:u32`（无则 `0xffffffff`） |
| 16 | `flags:u32`（bit0=有 texture property） |
| 20 | `softParticles:u8` |
| 21 | `castShadows:u8`（V3.1；旧资产为 0） |
| 22 | `motionVectorDisabled:u8`（V3.2；0/旧资产=生成，1=跳过） |
| 23 | `reserved:u8` |
| 24 | `softDistance:f32` |
| 28 | `sortPriority:i32`（V3.7；旧资产为 0） |

- V3.1 复用原 `reserved` byte 21，`.vfxb` v6 的 section/record 大小不变；旧资产该字节为 0，即不投影。只有带 `castShadows:true` 的 output 需要重烘焙。V3.2 用原 reserved byte 22 表示 motion-vector disabled（0/旧资产=启用，1=禁用），更新 `generateMotionVector:false` 的资产需重烘焙。V3.9 的 `sortOrder` 覆盖写入既有 OUTPUTS 偏移 11 的 effective order；旧资产该字节仍等于 system order，字节不变；只有显式覆盖的 output 需要重烘焙。V3.10 的 per-output criterion 只在 output 与 system criterion 不同时追加 KERNELS 记录：`SortReset` task 16 + 额外 `OutputUpdate`/sort pipeline，并把 criterion 写入 KERNELS offset 7；不新增 BUFFERS/section、不改变 version 6。共享 KVP/indirection，runtime 在 draw 前串行重排。含 extra pipeline 的资产旧 runtime 因 task 16 拒绝加载；无 extra criterion 的既有资产字节不变。

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

- reader 只接受 `version == 6`（V1.4.2+）。V1.4.2 增加 output soft flag/softDistance（OUTPUTS 24B→32B）与 scene-depth binding；不兼容旧 version 1–5 资产；所有 checked-in `.vfxb` 已重新烘焙。V1.2.4 增加 CPU event link、event staging binding、eventCpuUpload task 与 payload type/default。
- 新增可选字段用 flags 表示；不改变布局。
- 不兼容修改 → version+1；旧 runtime 明确拒绝。
- `capacity` 实际值可能因 64 对齐而增大；资产记录 requested/actual 两值，运行时用 actual。
- V1.0b.6 reader 在 `vfx/src/runtime/runtime_asset.cpp` 校验 magic/version/header size/flags/contentHash/section 范围/双后端完整性；`.vfxb` 不依赖 compiler target 加载。V1.0b.8 起 header offset 28 的 `assetNameOffset` 指回 STRINGS 段，reader 回填 `Asset::name`；0 表示空名，兼容旧资产。
- V3.4 使用 SYSTEMS 偏移 60 的 reserved 首字节存 `boundsMode`（0=manual，1=automatic）；旧资产该字节为 0，无需重烘焙。record 大小仍为 64B，`.vfxb` version 仍为 6。
- V3.5 的 padding 使用可选 `BOUNDS` section（id 11）；只有至少一个 automatic system 的 padding 非零时才写入。缺 section 时 padding 全 0，旧 runtime 忽略该 section，`.vfxb` version 仍为 6。新增 `bounds.padding` 的资产需重烘焙；已有零 padding 资产字节不变。
- V3.6 的 culling flags 使用可选 `CULLING` section（id 12，`u32 flags`）；只有非零 flags 时写入。缺 section 时 flags=0（always simulate），旧 runtime 忽略该 section，`.vfxb` version 仍为 6；新增非零 `culling` 的资产需重烘焙。V3.7 的 `sortPriority` 写入 OUTPUTS 偏移 28，旧资产/旧二进制为 0；非零 priority 需要重烘焙，但不改变 record 大小或 `.vfxb` version 6。V3.8 的 `cameraDepth`/`youngest` 只新增 SYSTEMS/OUTPUTS `sortMode` 枚举值 3/4，不改变 record 大小或 version；旧值 0/1/2 和旧资产字节不变，但旧 runtime 会拒绝新枚举值。V3.9 的 per-output `sortOrder` 复用 OUTPUTS 偏移 11，旧资产仍写 system order，record/version 不变；显式覆盖的资产需要重烘焙。V3.10 的 per-output criterion 复用 OUTPUTS offset 10，并仅在 criterion 不同时追加 sort pipeline（KERNELS offset 7 tag、`SortReset` task 16）；既有资产不追加记录，字节不变。V3.11 的 output draw instancing 不改变 v6 section/record，只改 `fill_indirect` 参数与 output/MV/shadow shader（`numInstances=ceil(count/2048)`，VS 用 `gl_InstanceID * 2048` 映射）；所有 checked-in `.vfxb` 因 shader blob 变化已重烘焙。
- output VS/FS 的 binding 记录同样来自 KERNELS；runtime 用 VS 的 12/13 buffer 绑定和 FS 的 sampler 绑定。V3.9 反序 output 的 VS 绑定 stage 14 `RenderCount`；V3.11 起所有 output/MV/shadow VS 固定绑定 stage 14 做 partial-instance guard；无 texture property 时绑定 1×1 白纹理。
- runtime 对 Curve/Gradient 默认 LUT 按类型创建 RGBA16F 纹理（`256 × N`，N=该类型 property 数）；`setCurve`/`setGradient` 通过 `Device::updateTexture` 重传整张类型纹理。V1.1.4 起 compute shader 在 stage 9/10 采样 LUT；raster FS 仍只绑定 stage 0 main texture。

### 10.6 BOUNDS（可选；V3.5）

| 偏移 | 字段 |
|---|---|
| 0 | `count:u32`（必须等于 SYSTEMS 的 system 数） |
| 4 + i*12 | `padding.x:f32`, `padding.y:f32`, `padding.z:f32`（system i） |

- 只有至少一个 automatic system 的 `bounds.padding` 非零时写入；无 BOUNDS section 的旧资产 padding 全 0。
- 每个分量必须 finite；Unity `boundsPadding` 范围允许负值，因此不限制符号。负值可能缩小甚至反转 AABB，调用方自行保证有效。
- padding 只对 `bounds.mode == "automatic"` 生效；manual bounds 不使用 padding（对齐 Unity `VFXBasicInitialize` 的 CPU mapper）。
- `BOUNDS` section id 11、alignment 4；不改变 SYSTEMS 64B record 和 `.vfxb` v6 header。

### 10.7 CULLING（可选；V3.6）

| 偏移 | 字段 |
|---|---|
| 0 | `flags:u32`（bit0=`CullSimulation`，bit1=`CullBoundsUpdate`） |

- JSON 顶层 `"culling"` 支持 `"none"`（0）、`"simulation"`（1）、`"visible"`（3）；默认 `"none"`。
- 只有 flags 非零时写入；无 CULLING section 的旧资产 flags=0，保持 always simulate。
- automatic bounds 与 culling 不兼容（Unity 在 automatic 模式强制 `CullNone`）；JSON/reader 拒绝，editor 切换到 automatic 时清空 `culling`。
- `CULLING` section id 12、alignment 4；不改变 SYSTEMS/OUTPUTS record 和 `.vfxb` v6 header。

## 11. 校验规则（摘要）

- `capacity >= 1` 且 ≤ 1048576；64 对齐后记录 actual。
- `sort.mode != none` 时 V1 要求 `actual capacity <= 262144`；更大容量必须关闭排序或等待 V2（编译器报错，不静默降级）。
- `spawner.rate >= 0`；burst `count >= 0`；`period > 0`（若有周期）；`repeat >= 1`。
- `bounds.mode == manual` 时 center/extents 必填；extents > 0。`automatic` 时 center/extents 可省略；V3.5 起可写 `padding`，分量 finite，允许负值，只对 automatic 生效。GPU AABB 仍由 alive 粒子计算。
- `culling` 只接受 `none`/`simulation`/`visible`；非零 `culling` 不能与任一 automatic bounds system 组合。
- `sort.mode != none` 时每个 output 必须显式声明相同 `sort`（`distance`/`custom`/`cameraDepth`/`youngest`）；V1.2.2 共享 system 排序，所有 output 使用同一 sorted indirection。
- `sort.mode == custom` 时系统必须声明 scalar（float/int/uint/bool）`sortKey` 属性；否则编译器在 `systems[i].sort.mode` 报错。
- 属性名唯一；自定义属性 ≤8；不得与内置冲突。
- 表达式内部类型匹配、无环、嵌套深度 ≤64（V1.0b.3）；block 字段目标类型在 V1.0b.5 kernel 编译入口校验；子图深度 ≤16 在 V1.3 校验。
- 事件名唯一匹配（GPU 一个 emit/一个 receive；CPU 只有 receive）；同一 system 不能同时收发；receiver 不能有 CPU spawner；每个事件最多 4 个 payload 属性；event buffer 总量 ≤64 KiB；`trigger_event` 必须匹配该 system 的唯一 emit 名；CPU receive 必须显式声明 attributes。
- 自定义 HLSL：`code` ≤32 KiB；`reads`/`writes` 必须是已知属性且不能重复；`VFX_ATTR` 只能读 `reads`，`VFX_SET_ATTR` 只能写 `writes`；禁止绕过宏直接访问生成变量。V1.3.4 的 `custom_hlsl_visual` 资产覆盖字段类型、双后端烘焙与 D3D11/Vulkan golden。
- 子图：`inputs/outputs` 的 `name` 必须是合法标识符且唯一，`type` 限于纯函数可用类型；`outputs.value` 必填；递归深度 ≤16、循环/缺参/未知参数报错。V1.3.4 的 `subgraph_visual` 资产覆盖纯函数 + block 子图组合及双后端 golden。
- Lit output 的 `metallic`/`roughness` 必须是 0..1 常量，或指向存在的 `float` property；缺省 0/0.5。V1.4.1 的 `lit_visual` 资产覆盖双后端烘焙与 forward/deferred D3D11/Vulkan golden。
- V1.4.2 `softParticles=true` 时 `softDistance` 必须 ≥0.001；输出 FS 增加 stage 4 depth sampler，`.vfxb` OUTPUTS 记录 soft flag/distance。`soft_visual`/`soft_visual_off` 覆盖 soft on/off D3D11/Vulkan golden。
- 所有 kernel 的 dxbc + spirv 必须存在。

**分阶段实现**：V1.0b.1 实现 schema/路径/数值/枚举/引用/curve-gradient 确定性校验；V1.0b.3 已实现表达式内部类型/折叠/DAG 校验（parser 接入 `validateGraphExpressions`），block 字段目标类型在 V1.0b.5；子图深度/循环在 V1.3，事件图在 V1.2，自定义 HLSL 语义在 V1.3.2（`vfx/src/core/custom_hlsl.cpp` + shaderc 双后端），lit metallic/roughness 在 V1.4.1，shader blob 完整性在 V1.0b.5；runtime reader 在 V1.0b.6。

## 12. `vfxbake` CLI

```text
vfxbake --input <effect.vfx.json> --output <effect.vfxb>
        --shaderc <path> --bgfx-src <path>
        [--shader-pack <dir>] [--backend dxbc,spirv] [--check] [--verbose]

vfxbake --check <asset-dir> --shaderc <path> --bgfx-src <path>
```

- 退出码：0 成功；1 校验/编译失败；2 参数错误。
- 失败信息包含源文件、JSON 路径（如 `systems[0].update.blocks[3].type`）和可得行/列；自定义 HLSL 错误为 `.code:line:column`，并附 shaderc 输出摘要。
- `--check` 单文件模式比较 `--output`；批量模式递归查找 `*.vfx.json`，并比较同目录同名 `.vfxb`，不写文件；CMake 在 `HBG_SHADER_COMPILER` 可用时注册 `hbg_vfx_asset_check` CTest，CI 直接跑 `ctest` 即覆盖 **29** 个测试资产（V3.1 增加 `shadow_visual`），其中包括 `subgraph_visual`、`custom_hlsl_visual`、`lit_visual`、`soft_visual`/`soft_visual_off` 与性能/empty fixture。
- `--shader-pack` 提供 HDRP 风格 include 根（`material/`、`lighting/`）；lit output 需要该路径。`compileGraphFile` 未显式提供时会从资产路径向上查找 `shaders/material/surface_data_types.sc`。
- `--backend` 默认 `dxbc,spirv`；V1 验收要求双后端。
- 当前未提供 `--dump-shaders`；生成 shader 仅存在于编译临时目录。

## 13. 编辑器布局 sidecar（V2）

- 路径：与资产同目录，`foo.vfx.json` → `foo.vfx.editor.json`。
- 该文件不是运行时资产；`vfxbake`/`parseGraphJson` 不读取，`.vfxb`/graph hash 不包含它。
- schema：

```json
{
  "version": 1,
  "assetHash": "123456789",
  "view": {"pan": [0, 0], "zoom": 1.0},
  "nodes": {
    "systems/0/spawner": {"x": 48, "y": 40, "collapsed": false},
    "systems/0/init": {"x": 48, "y": 150, "collapsed": false},
    "systems/0/update": {"x": 48, "y": 330, "collapsed": false},
    "systems/0/outputs/0": {"x": 48, "y": 500, "collapsed": false}
  }
}
```

- `assetHash` 是 `vfx::graphHash()` 的十进制字符串；不匹配只提示 stale，不阻塞图加载。
- 未知/失效 context path 被忽略；缺失 context 使用默认布局。
- Save 会同时写 `.vfx.json` 与 sidecar；sidecar 损坏/缺失不影响运行时。
