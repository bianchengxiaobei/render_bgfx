# VFX 后端 API 与引擎接入

> 目标：独立库只暴露 C++20 接口；hdrp-bgfx 适配层实现它们。
> 约束：公开头文件不出现 `bgfx::*` / `hbg::*` / RenderGraph 类型；所有 GPU 资源用 opaque handle。
> 线程：全部在主线程调用；资源销毁可延迟（由适配层 `ResourceManager` 处理）。

---

## 1. 头文件布局

```text
vfx/include/vfx/
├─ types.h            Vec2/3/4, Mat4, Bounds, BlendMode, CompareFunc, TextureFormat, UniformType, Limits
├─ handles.h          BufferHandle/TextureHandle/ProgramHandle/UniformHandle/SamplerHandle/ReadbackHandle
├─ asset.h            Graph JSON 解析（compiler/tool）
├─ runtime.h          Runtime/Effect/EffectDesc/Diagnostics；Asset 为 opaque
├─ device.h           Device 接口
├─ command_sink.h     CommandSink 接口、Access、DrawState、PassDesc
├─ shading.h          ShadingProvider 接口、FrameContext、FrameLighting
├─ events.h           EventAttribute, PropertyValue
├─ compiler.h         compileGraph（编辑器/工具）
└─ editor.h           V2 编辑器文档模型/布局（只读写 JSON，不进入 runtime）
```

- `types.h` 的 `Mat4` 使用 `std::array<float,16>`，**column-major**，与 hdrp-bgfx `core/math.h` 布局一致；右手系 Y-up、CCW 正面。
- 公开 API 不抛异常；失败返回 `nullptr`/`false` 并通过 `error()` 回调给出信息。
- 版本常量：`kVfxApiVersion`、`kVfxAssetVersion`。

## 2. Device（资源创建/销毁）

```cpp
namespace vfx {

enum class BufferUsage : uint8_t { Storage, StorageVertex, Indirect, Dynamic };
enum class TextureUsage : uint8_t { Sampled, RenderTarget, Storage };
enum class ShaderStage : uint8_t { Vertex, Fragment, Compute };
enum class ShaderBackend : uint8_t { Dxbc, Spirv };
enum class Access : uint8_t { Read, Write, ReadWrite };

struct BufferDesc {
    BufferUsage usage = BufferUsage::Storage;
    uint32_t size = 0;          // bytes
    uint32_t stride = 4;
    const void* initialData = nullptr;
    std::string_view name;
};

struct TextureDesc {
    uint16_t width = 1;
    uint16_t height = 1;
    uint8_t mipCount = 1;
    TextureUsage usage = TextureUsage::Sampled;
    TextureFormat format = TextureFormat::RGBA16F;
    const void* initialData = nullptr;
    uint32_t dataSize = 0;
    std::string_view name;
};

struct ShaderBlob {
    ShaderStage stage = ShaderStage::Compute;
    ShaderBackend backend = ShaderBackend::Dxbc;
    std::span<const std::byte> data;
};

class Device {
public:
    virtual ~Device() = default;

    virtual BufferHandle createBuffer(const BufferDesc&) = 0;
    virtual void destroyBuffer(BufferHandle) = 0;
    virtual bool updateBuffer(BufferHandle, std::span<const std::byte> data, uint32_t offset = 0) = 0;

    // Async GPU -> CPU readback; the implementation owns the staging storage
    // until copyReadback/releaseReadback, so Effect destruction cannot dangle.
    virtual ReadbackHandle readBufferAsync(BufferHandle, uint32_t size) = 0;
    virtual bool readbackReady(ReadbackHandle) const = 0;
    virtual bool copyReadback(ReadbackHandle, std::span<std::byte> data) const = 0;
    virtual void releaseReadback(ReadbackHandle) = 0;

    virtual TextureHandle createTexture(const TextureDesc&) = 0;
    virtual void destroyTexture(TextureHandle) = 0;
    virtual bool updateTexture(TextureHandle, std::span<const std::byte> data, uint32_t rowPitch) = 0;

    virtual ProgramHandle createProgram(std::span<const ShaderBlob> blobs) = 0;
    virtual void destroyProgram(ProgramHandle) = 0;

    virtual UniformHandle findUniform(std::string_view name, UniformType, uint16_t count = 1) = 0;
    virtual SamplerHandle findSampler(std::string_view name) = 0;

    virtual ShaderBackend backend() const = 0;
    virtual Limits limits() const = 0;   // maxTextureSamplers/maxComputeBindings
};

}
```

- `findUniform`/`findSampler` 按名称缓存；同一名称返回同一 handle。
- `BufferUsage::Indirect` 的 `size` 必须是 **32 的倍数**（bgfx 固定 stride）；`stride` 固定 32。
- `StorageVertex` 用于 attribute buffer（CS 写、VS 读）。
- `Storage`/`StorageVertex` 的 `initialData` 仅作为只读初始内容（`BGFX_BUFFER_COMPUTE_READ`）；需要 GPU 写的 buffer 不带 `initialData`，由 dispatch 初始化。
- `Limits`：`maxTextureSamplers`、`maxComputeBindings`；V1 要求 ≥16（hdrp-bgfx 当前为 16）。
- `readBufferAsync` 用于 automatic bounds：runtime 请求 24B，Hbg 用 `bgfx::read` 排队并用 sentinel 轮询，`copyReadback` 只在 ready 后读 backend-owned staging；effect 销毁时只 `releaseReadback`，不持有 GPU readback 指针。D3D11/Vulkan `hbg_vfx_reference_auto_bounds` 覆盖真实 readback；单测里的 D3D11 headless 初始化因无 window/backbuffer 会跳过。V3.5 的 `bounds.padding` 是 CPU 侧扩展，不需要新的 Device API。

## 3. CommandSink（命令记录）

```cpp
enum class PrimitiveTopology : uint8_t { TriangleList };

struct DrawState {
    bool depthTest = true;
    bool depthWrite = false;
    bool cullBackFace = false;
    BlendMode blend = BlendMode::Alpha;
    CompareFunc depthFunc = CompareFunc::LessEqual;
};

struct PassDesc {
    std::string_view name;
    bool simulation = true;      // 适配层用于选择 compute/raster
    bool readsSceneDepth = false;
    bool readsSceneColor = false;
};

class CommandSink {
public:
    virtual ~CommandSink() = default;

    virtual void beginPass(const PassDesc&) = 0;
    virtual void endPass() = 0;

    virtual void setStorageBuffer(uint8_t stage, BufferHandle, Access) = 0;
    virtual void setTexture(uint8_t stage, SamplerHandle, TextureHandle) = 0;
    virtual void setUniform(UniformHandle, const void* data, uint16_t count = 1) = 0;
    virtual void setState(const DrawState&) = 0;

    virtual void dispatch(ProgramHandle, uint32_t x, uint32_t y, uint32_t z) = 0;
    virtual void drawIndirect(ProgramHandle, BufferHandle indirect, uint32_t start = 0, uint32_t num = 1) = 0;
};

}
```

- `beginPass/endPass` 由适配层映射到当前的预声明 RenderGraph pass：适配层在构图阶段先 `addComputePass/addRasterPass`，在 pass 回调里调用 `beginPass` 只负责设置/清理 encoder/view 状态，**不会在 encode 时新建 pass**。
- 库在 `encodeSimulation/encodeOutput` 内部按需调用 `beginPass/endPass`；通常一个 pass 内多次 dispatch/draw。
- `setStorageBuffer` 的 `stage` 是逻辑槽位；适配层直接映射到 `gfx::Encoder::setStorageBuffer`。
- buffer stage 与 texture stage 不能重叠（D3D11 共用 SRV 槽位）；V1 的分配见 `architecture.md` §4.4。VFX lit variant 不得定义 `HBG_SHADOW_POINT_SAMPLER`/`HBG_SHADOW_AREA_SAMPLER`，否则 12/13 会与 attribute/indirection buffer 冲突。
- `drawIndirect` 映射到 `gfx::Encoder::submitIndirect`；`num=1` 表示一个 32B 记录。适配层必须在 draw 前绑定非空 vertex stream（V0 用静态 6 顶点 dummy buffer），否则 bgfx D3D11 不提交 indirect draw。
- 库不设置 view transform / view rect / scene color/depth；这些由适配层在 `beginPass` 前完成。

## 4. ShadingProvider（Lit/Unlit 着色）

```cpp
struct FrameContext {
    Mat4 view = Mat4::identity();
    Mat4 projection = Mat4::identity();
    Mat4 viewProjection = Mat4::identity();
    Mat4 prevViewProjection = Mat4::identity();
    Vec3 cameraPosition{};
    Vec3 cameraForward{0,0,1};
    Vec3 cameraRight{1,0,0};
    Vec3 cameraUp{0,1,0};
    Vec2 screenSize{1,1};
    float time = 0.0f;
    float deltaTime = 0.0f;
    uint64_t frameIndex = 0;
    TextureHandle sceneDepth{};    // 可无效
    TextureHandle sceneColor{};    // 可无效
    void* lighting = nullptr;      // 适配层私有（HDRP light data/shadow/IBL）
};

struct LitSettings {
    float metallic = 0.0f;
    float roughness = 0.5f;
    float softDistance = 0.1f;
};

class ShadingProvider {
public:
    virtual ~ShadingProvider() = default;

    // 在 output draw 之前绑定全局光照资源（light data/shadow/IBL/cluster/contact）。
    virtual void bindLighting(CommandSink&, const FrameContext&, const LitSettings&) = 0;

    // 可选：适配层自定义 output 程序选择（默认从 Asset 的 program key 取）。
    virtual ProgramHandle resolveProgram(const RenderOutputDesc&, ShaderBackend) = 0;
};

}
```

- `bindLighting` 只在 lit output 调用一次（或按需多次）；unlit 不调用。V1.4.3 的 lit VFX 优先绑定 cluster light list（`lightListParams.w=1`、`u_cluster_*`）；GPU buffer 路径使用 CPU cluster-texture mirror，无 cluster 时回退全局 light-list（`w=0`）。stage 12/13 仍保留给 attribute/indirection。
- Lit 的 stage 表必须与 `docs/vfx/architecture.md` §4.4 一致；适配层负责 `u_pass_*`/`u_global_*`/`u_object_*`/`u_cluster_*`/`u_environment_*` 上传。`metallic`/`roughness` 由 effect 的 float property 或 output 常量提供。
- 程序本体来自 `.vfxb` 的 shader blob；`resolveProgram` 默认返回资产内的 program。

## 5. Runtime / Effect

```cpp
struct EffectDesc {
    Mat4 transform = Mat4::identity();
    bool playOnAwake = true;
    float playRate = 1.0f;
    uint32_t randomSeed = 0;
    uint32_t instanceBatchLimit = 0; // V3.22/V3.23/V3.24: 0 = one full active/shadow batch
    bool hasBoundsOverride = false;
    Bounds boundsOverride{};
};

struct PropertyValue {
    PropertyType type = PropertyType::Float;
    float f[4] = {0,0,0,0};
    TextureHandle texture{};
    std::span<const CurveKey> curve{};
    std::span<const GradientKey> gradient{};
};

class Effect {
public:
    virtual ~Effect() = default;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;         // 重置到初始状态
    virtual void reset() = 0;
    virtual void advanceOneFrame() = 0; // 暂停时推进固定 1/60s 仿真；输出保持可绘制
    virtual void setTransform(const Mat4& world) = 0;
    virtual void setInstanceTransforms(std::span<const Mat4> worlds) = 0; // V3.15/V3.17/V3.18/V3.19/V3.22/V3.23/V3.24: one simulation, active/shadow split batch, or per-instance draws
    virtual void setCulled(bool culled) = 0;
    virtual void setInstanceCulled(std::size_t index, bool culled) = 0;
    virtual void setPlayRate(float) = 0;
    virtual bool isAlive() const = 0;

    virtual bool setProperty(std::string_view name, const PropertyValue&) = 0;
    virtual bool setFloat(std::string_view, float) = 0;
    virtual bool setVector2/3/4(std::string_view, Vec2/3/4) = 0;
    virtual bool setColor(std::string_view, Color) = 0;
    virtual bool setTexture(std::string_view, TextureHandle) = 0;
    virtual bool setCurve(std::string_view, std::span<const CurveKey>) = 0;
    virtual bool setGradient(std::string_view, std::span<const GradientKey>) = 0;

    virtual void sendEvent(std::string_view name, std::span<const EventAttribute> = {}) = 0;

    virtual Bounds bounds() const = 0;
    virtual Bounds visibleBounds() const = 0;
    virtual Bounds instanceBounds(std::size_t index) const = 0;
    virtual std::size_t instanceCount() const = 0;
    virtual bool supportsEffectInstancing() const = 0;
    virtual bool isCulled() const = 0;
    virtual bool isInstanceCulled(std::size_t index) const = 0; // V3.24: per-instance frustum state
    virtual std::uint32_t outputBatchCount() const = 0;        // V3.24: active batch count, 1 = effect-level
    virtual std::uint32_t outputBatchSize() const = 0;         // V3.24: active instances per batch
};

class Runtime {
public:
    Runtime(Device&, ShadingProvider*);
    ~Runtime();

    std::shared_ptr<const Asset> loadAsset(const std::filesystem::path&, std::string* error = nullptr);
    Effect* createEffect(const Asset&, const EffectDesc&, std::string* error = nullptr);
    Effect* reloadEffect(Effect*, const Asset&, const EffectDesc&, std::string* error = nullptr);
    Effect* reloadEffect(Effect*, const std::filesystem::path&, const EffectDesc&, std::string* error = nullptr);
    void destroyEffect(Effect*);

    void update(float deltaSeconds);
    void encodeSimulation(CommandSink&, const FrameContext&);
    void beginOutputFrame();
    void encodeOutput(CommandSink&, const FrameContext&);
    void encodeOutputEffect(CommandSink&, const FrameContext&, Effect&);
    void encodeOutputEffectBatch(CommandSink&, const FrameContext&, Effect&, std::uint32_t batchIndex);
    void encodeMotionVectors(CommandSink&, const FrameContext&);
    void encodeShadow(CommandSink&, const Mat4& shadowViewProjection);

    void setErrorCallback(std::function<void(std::string_view)>);
    bool hasShadowOutputs() const;
    Diagnostics diagnostics() const;
};

}
```

语义：
- `loadAsset(path)`：读取并校验 `.vfxb` bytes；不依赖 compiler target。
- `update(dt)`：clamp dt、推进 spawner/事件、更新 bounds；不产生 GPU 命令。
- `encodeSimulation`：按 system 依赖顺序 dispatch；内部 `beginPass({name, simulation=true, ...})`。
- `beginOutputFrame` / `encodeOutputEffect`：V3.16 additive API。renderer 可以按 effect 分批插入 transparent pass：先 begin 一次清空 output diagnostics/timing，再对每个 effect 调 encodeOutputEffect；`encodeOutput` 仍保留完整批次入口。
- `encodeOutput`：按输出排序提交 indirect draw；内部 `beginPass({name, simulation=false, ...})`。
- `encodeMotionVectors`：V3.0 additive API；在 host 已绑定 RG16F/RGBA16F motion-vector target 和 scene depth 后，按 output 提交同一 indirect draw，复用 `oldPosition` 计算当前/上一帧 clip 并写 `currentNDC - previousNDC`。V3.2 对 `AssetProgram::generateMotionVector == false` 的 output 直接跳过；缺省/旧资产为 true。V3.3 由 runtime 保存每帧 effect transform 历史，local system 额外上传 `u_vfx_prevWorld` 给 motion VS；world system 仍绑定 identity。需要 `ShadingProvider::resolveProgram({"vfx.motionVectors"})` 提供固定 program；不可用时静默 no-op。调用方应像 `encodeOutput` 一样用独立 graph pass 绑定目标，并在需要 TAA/motion blur 时排在 output 后。
- `FrameContext::prevViewProjection` 与 `viewProjection` 已足够描述相机历史；V3.3 的 effect 历史不新增 FrameContext 字段或公开 API。`encodeMotionVectors` 增加的 draw 会计入 `Diagnostics::draws`，耗时累加进 `outputMs`。
- `encodeShadow`：V3.1 additive API；host 已在 shadow atlas viewport 中设置 view transform/framebuffer/depth bias 后传入该 request 的 `viewProj`。runtime 只对 `castShadows` output 提交 indirect draw，需 `resolveProgram({"vfx.shadow"})`；固定 shader 用 camera billboard、当前 `u_vfx_world` 和 shadow VP 写 depth，透明 output 用 `s_vfxMain` alpha 裁剪。draw 数计入 `Diagnostics::shadowDraws`，不改变 color output。
- `hasShadowOutputs()`：是否存在任一 output 的 `castShadows`；renderer 用它禁用 static shadow cache，避免粒子上一帧的 shadow 被缓存复用。
- `setProperty` 的 Curve/Gradient 会立即重新生成并上传按类型打包的 LUT 纹理（`Device::updateTexture`）；V1 允许在播放中修改。compute shader 在 stage 9/10 采样，`setCurve/setGradient` 重传整张 256×N 类型纹理。
- `PropertyValue` 中的 `std::span` 只在调用期间有效；库会立即拷贝控制点，不保留调用方内存。
- GPU 事件已在 V1.2.3 接入，CPU 事件在 V1.2.4 接入：runtime 创建全局 `vfx.events` Storage buffer 与 `vfx.cpu.events` staging；event copy/prefix/reset program 不进入 system kernel。`sendEvent` 按 CPU link 入队，缺省属性使用 consumer 默认值，类型错误通过 error callback 报告。
- `Effect` 由 `Runtime` 拥有；`destroyEffect` 后指针失效。
- `reloadEffect` 先创建替换 effect，成功后才销毁旧 effect；失败时旧指针和资源保持有效。V1 是销毁重建，不保留播放时间/pending 事件/属性覆盖；调用方需使用返回的新指针。
- `reloadEffect` 后 runtime 会释放不再被任何 effect 引用的 `Asset` 内部持有；调用方自己的 `shared_ptr` 仍有效。
- 多 effect 各自独立 buffer/program；同一 `Asset` 可创建多个 effect。
- `Diagnostics::particles` 是 CPU spawn 估算，不是 GPU dead list 读回；`draws`/`dispatches`/`sortPasses` 为最近一次 encode 计数；`eventCount` 在 V1.2.3/4/V1.4.4 记录事件链数量；GPU/CPU 精确事件数需读 event buffer，V1 仅在 reference 测试路径读回。V1.0b.8 的 `--vfx-reference` 测试路径会实际读回 GPU deadList，但不回写交互 diagnostics。
- `Device` 没有 uniform/sampler 销毁接口；runtime 创建的名称缓存由适配层 `HbgVfxBackend` 持有，随 backend 析构释放。buffer/program/texture 由 runtime/Effect 释放。
- 输出 pass 由 runtime 设置 `u_vfx_world`：local system 用 effect transform，world system 用 identity；global camera uniforms 来自 `FrameContext`。
- V3.6：`Effect::setCulled(bool)` 由 host renderer 按相机 frustum 与 `Effect::bounds()` 设置；`isCulled()` 返回当前状态。asset 的 `culling` flags 为非零时，`CullSimulation` 会暂停 `update()` 时间/spawn 与 `encodeSimulation()` dispatch；culled effect 的 `encodeOutput()`/`encodeMotionVectors()` 不提交 draw。`encodeShadow()` 不读取该相机 culling 状态，shadow request 继续独立绘制。默认 flags=0 时只跳过不可见 output/MV draw，仿真继续。V3.7 的 `AssetProgram::sortPriority` 仅决定同一 effect 的 output 提交顺序；color/MV/shadow encode 共用稳定排序后的 output 列表，不影响 simulation/events。V3.8 的 `cameraDepth` 排序需要 `FrameContext::view`/`cameraPosition`/`cameraForward` 一致；runtime 用 `inverse(view)` 上传 `u_global_invView`，`youngest` 只读取粒子 `age` 属性。V3.9 的 `AssetProgram::sortOrder` 是 effective output order；与 system order 不同时 color VS 反向读取 indirection，MV/shadow 通过 `u_vfxMotionParams.z`/`u_vfxShadowParams.w` 反向，`RenderCount` 同时可用于 vertex stage。V3.10 的 `AssetProgram::sortMode` 是 effective output criterion；criterion 与 system 不同时，runtime 在 color/MV/shadow draw 前按需 dispatch `SortReset`(task 16) + 额外 output update/sort/fillIndirect；有 extra pipeline 的 system 使用 `u_vfxSortWorld`/`u_vfxSortCameraPos`/`u_vfxSortInvView`，避免与 raster pass 的 `u_global_*` 冲突。V3.11 的 output draw 仍由 `drawIndirect` 提交一次：args 的 `numVertices=6*min(count, 2048)`、`numInstances=ceil(count/2048)`；output/MV/shadow VS 固定绑定 stage 14 `RenderCount`，用 `gl_InstanceID*2048` 选择切片并对 partial tail 输出退化顶点。V3.12 为额外 criterion 增加 runtime-only sorted indirection；V3.14 再为每个额外 criterion 增加独立 KVP/RenderCount/indirect args，`SortBufferSet` 同时选择 stage 13 indirection、stage 14 count 和 draw args，base 与 extra state 不再互相覆写。V3.15 增加 additive `Effect::setInstanceTransforms`/`setInstanceCulled`、`instanceBounds`/`instanceCount`/`supportsEffectInstancing`：同一 `Effect` 可承载多个 scene instance，simulation 只跑一次，color/MV/shadow 逐 instance 写 transform 并 draw；仅 local-space 且 output sort 为 `none`/`youngest` 时可用。host 通过 `PipelineSettings::supportVFXInstancing` / `--vfx-instancing` 启用，默认关闭。V3.16 增加 `PipelineSettings::supportVFXTransparentSort` / `--vfx-transparent-sort`：renderer 以 effect bounds 中心生成透明键，把每个 effect 的 VFX pass 插入 sliced transparent DrawList；默认仍由单个 VFX pass 在 transparent 前绘制。V3.17 在 V3.15 分组且全部 instance 可见时，把每个 output 的 color/MV/shadow draw 合并为一次 indirect draw：`fill_indirect` 的 `numInstances = particleChunks * effectInstances`，`u_vfxInstanceParams.x` 传 effect instance 数，stage 15 的 `vfx.instance.transforms` 保存每 instance current/previous `Mat4`；任一 instance culled 或 N=1 时回退逐 instance draw。N=1 仍绑定 attributes buffer 到 stage 15 作为 descriptor fallback。V3.18 在 stage 15 的 transform slot 后追加 active instance 列表，`u_vfxInstanceParams` 扩展为 `{totalInstances, activeInstances, activeBase, activeIndirection}`：部分 instance 可见且可见数 >1 时 color/MV 仍用一个 batch draw，shader 只让 active slot 产生顶点；shadow 使用 totalInstances 且不读 active 列表。可见数 =1 保留逐 instance fallback，0 时跳过 color/MV。V3.19 为每个 system/sort list 懒创建 `.active.indirect` args buffer：`0 < active < total` 时在 `ensureOutputSort()` 后用 `fill_indirect` 写入 `numInstances = particleChunks * activeInstances`，color/MV 改用 active args；full batch 继续使用 simulation 生成的 total args，shadow 始终使用 total args。visible=1 也使用 active=1 args，避免按 total instance 重复提交。V3.20 在 automatic-bounds system 的 simulation 中额外上传 `u_vfxBoundsParams={1,0,0,0}` 与 `u_vfxBoundsRight/Up`（来自 `FrameContext::cameraRight/Up`），bounds kernel 用 angle/scale 计算 tight billboard AABB；非 automatic system 不设置这些 uniform。bounds readback、padding 与 host culling 接口不变。V3.21 增加 `Effect::visibleBounds()`：只合并未 cull 且有效的 `instanceBounds(i)`，`bounds()` 仍返回全量并集；`ForwardRenderer` 的 V3.16 effect-level 透明排序中心改用 visible bounds，全部 cull 或 bounds 无效时回退 primary instance position。V3.22 增加 `EffectDesc::instanceBatchLimit`（0 = 不切分）与 `PipelineSettings::vfxInstanceBatchSize` / `--vfx-instance-batch=<n>`：active instance 按固定大小切成连续 batch，每个 batch 懒创建 `.active.indirect[.N]` args，`u_vfxInstanceParams` 用 `activeBase + batchOffset` 映射 active list；color/MV 每个 batch 一次 `drawIndirect`，shader 源码不变。shadow 继续 total-sized batch，不读 active list。V3.22 没有 instance/property/texture split、prefix-sum variable-size batch、独立 active→batch buffer 或 GPU cull compaction。V3.23 把同一 `instanceBatchLimit` 扩展到 shadow：按 total instance 的连续 batch 懒创建 `.shadow.indirect[.N]`，`u_vfxInstanceParams` 用 `{totalInstances, localInstances, batchOffset, 0}`，shadow VS 在 batch-local instance index 上叠加 `batchOffset`；shadow 仍绘制全部 instance、不读 active list。`instanceBatchLimit` 只在创建 effect 时读取，运行期修改需重建 effect。V3.24 增加 `Effect::isInstanceCulled(index)`/`outputBatchCount()`/`outputBatchSize()` 与 `Runtime::encodeOutputEffectBatch()`：启用 `--vfx-transparent-sort` 且 `--vfx-instance-batch>0` 时，renderer 按 active instance batch 生成独立透明 key，每个 batch 使用自己的 `.active.indirect[.N]` args、union bounds 中心和 VFX output pass；`0` 仍保持 V3.16 effect-level 单 key，`n=1` 即逐 instance interleave。MV 和 shadow 不随透明 batch 拆分。

## 6. 编译器 API（编辑器/工具）

```cpp
struct CompileOptions {
    std::filesystem::path shaderPack;
    std::filesystem::path shaderc;
    std::filesystem::path bgfxSource;
    std::vector<ShaderBackend> backends{ShaderBackend::Dxbc, ShaderBackend::Spirv};
    bool debugInfo = false;
};

struct CompileResult {
    std::vector<std::byte> binary;
    std::string error;
    std::vector<ParseError> errors;
    bool ok() const { return error.empty(); }
};

CompileResult compileGraph(std::string_view json, const CompileOptions&, std::string_view sourceName = {});
CompileResult compileGraphFile(const std::filesystem::path&, const CompileOptions&);
```

- `vfxbake` 直接调用 `compileGraphFile`。
- 实现：`vfx/include/vfx/compiler.h`、`vfx/src/compiler/compiler.cpp`、`codegen.cpp`、`binary.cpp`；目标 `hbg_vfx_compiler`，不链接进 runtime/app；CMake alias 为 `vfx_compiler`。
- `shaderc`/`bgfxSource` 必须显式提供；同源 shader 在单次 `compileGraph` 内缓存；错误包含源文件、JSON 路径/可得的行号与 shaderc 输出。
- V1.0b.5 生成 unlit quad；V1.4.1 为 `lit:true` 生成 lit FS（SurfaceData/light loop/shadow/contact shadow），`shaderPack` 是 lit shader 的 include 根（`material/`、`lighting/`）；`compileGraphFile` 未显式提供时会从资产路径向上查找 `shaders`。lit D3D 变体在 FXC 下省略 `-O 3` 并关闭 `--Werror`，规避 light-loop 的保守未初始化告警。`debugInfo` 仍保留但暂不写 DEBUG section。

## 7. 引擎接入（手动 update/encode）

```cpp
// 初始化（hdrp-bgfx 适配层）
hbg::vfx::HbgVfxBackend backend(resources);
vfx::Runtime runtime(backend.device(), &backend.shading());
auto asset = runtime.loadAsset("effects/spark.vfxb", &error);
vfx::EffectDesc desc;
desc.transform = transform;
auto* effect = runtime.createEffect(*asset, desc, &error);

// 每帧 CPU
effect->setTransform(world);
runtime.update(dt);

// RenderGraph：仿真
auto sim = graph.addComputePass("VFX Simulation");
sim.allowCulling(false);
sim.setRenderFunc([&](RenderGraphContext& ctx) {
    backend.begin(ctx.encoder(), ctx.viewId());
    vfx::FrameContext frame = backend.makeFrameContext(viewState, resources);
    runtime.encodeSimulation(backend, frame);
});

// RenderGraph：输出（放在 Forward Transparent 之前）
auto out = graph.addRasterPass("VFX Output");
out.setColorAttachment(color);
out.readTexture(depth);
out.setDepthAttachment(depth, 0, true);
out.allowCulling(false);
out.setRenderFunc([&](RenderGraphContext& ctx) {
    backend.begin(ctx.encoder(), ctx.viewId());
    vfx::FrameContext frame = backend.makeFrameContext(viewState, resources);
    backend.bindLighting(backend, frame, {});     // lit output 需要
    runtime.encodeOutput(backend, frame);
});
```

- `HbgVfxBackend` 同时实现 `Device`、`CommandSink`、`ShadingProvider`（或拆成三个对象）。
- soft particles（V1.4.2）需要适配层把当前 scene depth import 到 `FrameContext::sceneDepth`；runtime 将 `s_vfxSceneDepth` 绑定到 stage 4，并上传 `u_vfxSoftParams`。输出 pass 必须显式 `readTexture(depth)`，且 depth attachment 使用只读模式。
- `begin(encoder, viewId)` 保存当前 encoder/view，供后续 bind/dispatch/draw。
- `makeFrameContext` 填充相机矩阵/屏幕/时间；V1.0b.7 的 `ForwardRenderer` 直接构造 `FrameContext`，适配层公开 helper 仍待后续。
- RenderGraph buffer 声明：适配层可 `importBuffer` 关键 VFX buffer（尤其是 indirect args）；V1 以 `allowCulling(false)` + 固定顺序保证执行，V1.4 再完善。

## 8. 适配层映射

| 库接口 | hdrp-bgfx 实现 |
|---|---|
| `Device::createBuffer` | `gfx::ResourceManager::createBuffer`（`Storage/StorageVertex/Indirect`） |
| `Device::createTexture` | `gfx::ResourceManager::createTexture`（LUT/RGBA16F） |
| `Device::createProgram` | `gfx::ResourceManager::createProgram` 或 `createComputeProgram`（blob 来自 `.vfxb`） |
| `Device::findUniform/findSampler` | `gfx::ResourceManager::createUniform` 缓存 |
| `CommandSink::setStorageBuffer` | `gfx::Encoder::setStorageBuffer` |
| `CommandSink::setTexture` | `gfx::Encoder::setTexture` |
| `CommandSink::dispatch` | `gfx::Encoder::dispatch` |
| `CommandSink::drawIndirect` | `gfx::Encoder::submitIndirect` |
| `CommandSink::setState` | `bgfx::setState` 封装（现有 `gfx::state` 可扩展） |
| `ShadingProvider::bindLighting` | 复用 `ForwardRenderer` 的 `PassUniforms`/`GlobalUniforms`/`ObjectUniforms`/光照 stage 绑定 |
| `ShadingProvider::resolveProgram("vfx.motionVectors")` | V3.0 `HbgVfxBackend` 按当前 backend 懒加载 `shaders/build/{dxbc,spirv}/vfx_motion_vectors.{vs,fs}.bin`；program 由 backend 持有 |
| `ShadingProvider::resolveProgram("vfx.shadow")` | V3.1 `HbgVfxBackend` 懒加载 `shaders/build/{dxbc,spirv}/vfx_shadow.{vs,fs}.bin`；program 由 backend 持有 |
| `FrameContext` | `ViewState` + `RenderScene` 相机 + 当前 `resources.depth`；`prevViewProjection` 供粒子 MV |

## 9. 错误、诊断、生命周期

- `vfx::ParseError`：`path`、`message`、`source`、`line`、`column`、`location(Json/Hlsl)`；`formatParseError` 输出 `file:line:column: path: message`。JSON 路径由轻量 locator 定位到 key；缺失字段回退到根。自定义 HLSL 的 `.code` 错误使用代码内行列。
- `CompileResult` 同时保留 `error` 摘要和 `errors` 结构化列表；`compileGraph(json, options, sourceName)` 可传源文件名，`compileGraphFile` 自动传入文件路径。
- `Runtime::setErrorCallback`：编译/加载/资源失败时回调；V1 只记录一次同类错误（避免刷屏）。
- `Diagnostics`：
  ```cpp
  struct Diagnostics {
      uint32_t effects = 0;
      uint32_t systems = 0;
      uint32_t particles = 0;     // 估计/读回值
      uint32_t draws = 0;
      uint32_t dispatches = 0;
      uint32_t sortPasses = 0;
      uint32_t eventCount = 0;
      float updateMs = 0.0f;
      float simulationMs = 0.0f;
      float outputMs = 0.0f;
      float encodeMs = 0.0f;
      float totalMs = 0.0f;
  };
  ```
- `encodeMs = simulationMs + outputMs`，`totalMs = updateMs + encodeMs`；`Runtime::update` 每帧重置 phase 计时；`shadowDraws` 由 `encodeShadow` 累加，`draws` 只统计 color/MV draw。`eventCount` 仍是事件链数量，精确 GPU 计数由 reference 读回，默认帧循环不做同步读回。
- `EffectImpl` 在创建资源前校验 capacity、当前后端 shader blob 和 binding stage 上限；失败时返回空 effect 且回收未引用 asset。
- 资源：`Runtime` 只销毁自己创建的 buffer/texture/program；外部纹理（engine 绑定）不销毁。
- 适配层 `ResourceManager` 负责两帧延迟销毁；`Runtime` 在 `destroyEffect` 时释放自己持有的句柄。
- 不支持多线程调用；V1 不做 job 化。

## 10. Mock Backend（测试）

- `vfx/tests/support/mock_backend.h` 实现 `Device`/`CommandSink`/`ShadingProvider`：
  - 记录每次 `setStorageBuffer/setTexture/setUniform/dispatch/drawIndirect`；
  - 返回递增的 opaque handle；
  - 提供 `commands()` 供断言（顺序、stage、count）。
- 用于验证：
  - spawn/init/update/outputUpdate/fillIndirect 的 dispatch 顺序与数量；
  - indirect args 的 8×uint 内容；
  - sort pass 数与 KVP 索引；
  - GPU event reset/prefix sum 调用、event buffer stage binding；
  - event buffer 头部/容量。

## 11. 实现状态（V1.2.4）

- 公共接口已落地：`vfx/include/vfx/device.h`、`command_sink.h`、`shading.h`、`runtime.h`、`events.h`；`types.h`/`handles.h` 补齐 `Mat4`、格式、opaque buffer/uniform/sampler handle。
- Mock：`vfx/tests/support/mock_backend.h`；测试 `vfx/tests/test_vfx_backend.cpp`。
- Runtime：`vfx/src/runtime/runtime_asset.cpp` 校验/读取 `.vfxb`；`vfx/src/runtime/runtime.cpp` 实现 effect 资源、常量/Burst 累积、simulation/output encode、LUT 上传和诊断。CMake target `hbg_vfx_runtime`；测试 target `hbg_vfx_runtime_tests`。
- CPU reference：`vfx/include/vfx/reference.h` 的 `SpawnerState`/`updateSpawner` 提供与 runtime 相同的 rate/burst/dt clamp 语义；`simulateParticle` 提供粒子 init/update 参考；`simulateEventCount` 提供 GPU 事件死亡计数参考。
- 适配层：`src/render/vfx/hbg_vfx_backend.h`、`hbg_vfx_backend.cpp`；CMake target `hbg_vfx_adapter`。测试 target `hbg_vfx_adapter_tests`。
- `HbgVfxBackend::createProgram` 消费 `.vfxb` shader section 的 `vfx::ShaderBlob`；runtime reader 在 V1.0b.6 落地，适配层不解析容器。
- `StorageVertex` 用 `BufferDesc::stride` 合成 `gfx::VertexLayout`，仅满足 `bgfx::createDynamicVertexBuffer` 的 stride 校验；VS 仍通过 `BUFFER_RAW_RO` + word offset 读取，不依赖该 layout 属性顺序。
- 带 `initialData` 的 Storage/StorageVertex 以 `BGFX_BUFFER_COMPUTE_READ` 创建；可写 storage 创建空 buffer。runtime 对 attributes/indirection/renderKVP/sortScratch 不传 initialData；attributes/indirection/renderKVP/sortScratch 以 StorageVertex 创建（KVP/scratch stride 8），避免 >64KB 的 DynamicIndex storage 在 Vulkan compute 绑定下 device lost；reset 时额外 dispatch `dead_list_init`，由 GPU 清空 attributes/indirection 并重建 deadList；spawner 清 renderKVP counter。
- V1.2.2 起每个 system 有 9 个 buffer；`renderKVP`（kind 7）为 `(1 + capacity) * 8`B、`sortScratch`（kind 8）为 `capacity * 8`B。`output_update` 写 key/index，sort 读 renderKVP、在 sortScratch 中 merge，最终写 indirection；`fill_indirect` 把 renderCount 同步为 KVP counter。
- V1.2.3 起事件资产额外创建全局 `vfx.events` Storage buffer；V1.2.4 为 CPU link 增加 `vfx.cpu.events` staging buffer（Storage + COMPUTE_READ），每帧 `Device::updateBuffer` 后由 `event_cpu_upload`（task 13）复制 CPU link 到全局 event buffer。runtime 在首帧/reset dispatch `event_reset`（task 12），再 CPU upload，producer simulation 后 dispatch `event_prefix_sum`（task 11，128-thread Sklansky），最后 consumer simulation。event buffer 每 link 为 `elementCount/totalCount/prefix/dropped/frameCount + sourceIndex[capacity] + payload[capacity*16]`；producer `update`/consumer `init` stage 2，CPU copy staging stage 0 / event stage 1。V1 限制每 effect 总 event buffer ≤64 KiB；GPU event buffer 不能 CPU update，CPU 数据必须经 staging + copy。
- `ForwardRenderer::ensureVfx` 在 `supportVFX` 开启且 scene 有 VFX 实例时惰性创建 `HbgVfxBackend`/`vfx::Runtime`；`updateVfx`、simulation pass、output pass 已接入 RenderGraph。
- `bindLighting` 在 V1.4.1 通过 `FrameContext::lighting` 读取 `ForwardLightingContext`，调用共享 `bindForwardLighting()` 绑定 stage 5/6/7/8/9/10/11/14/15；不绑定 stage 12/13。`resolveProgram` 返回无效 handle；ForwardRenderer 自行构造 `FrameContext`（相机/时间/尺寸）。Runtime 只在 lit output 调用 `bindLighting` 并上传 float property uniforms。
- `HbgVfxBackend` 对 uniform/sampler 名称缓存区分“自己创建”和“共享的 `u_global_*`”；shutdown 不销毁共享 handle。
- `drawIndirect` 首次调用时懒创建 6 顶点 dummy vertex buffer；与 V0 的 D3D11 indirect draw 约束一致。
- 适配层测试使用 bgfx `Noop` 后端验证 100 个 buffer/program 的资源生命周期、encoder 命令映射、从 checked-in `.vfxb` 创建 7 个 program，以及 runtime effect 资源路径；`vfx_scene.gltf` 已在 D3D11/Vulkan render-test 出图且两后端 PNG 0 diff。
- Runtime 测试使用 MockBackend 验证资源生命周期、9 个 buffer、6 次首次 dispatch/5 次后续 dispatch、逐 kernel stage/buffer binding（含 renderKVP/sortScratch）、spawnCount、property/Curve/Gradient LUT、local bounds、indirect output、reset/stop/play 和损坏 hash 拒绝；V1.1.1 新增 `spawner_bursts` 固定 dt 序列（同帧合并、period/repeat、dt clamp）；V1.2.2 新增 sorted 8192 的 pre-pass×2 + merge 调度/`sortPasses` 测试。
- V1.1.2 新增 `forces_1024.vfx.json/.vfxb`（gravity、useSize drag、absolute/relative force、turbulence、attractor）与 D3D11/Vulkan `hbg_vfx_reference_forces_*` CTest；CPU reference 与 GPU readback 在 10 帧内逐粒子 1e-3 比较，turbulence 关闭的 `minimal_unlit` golden 保持 0 diff。
- V1.1.4 新增 `curve_1024.vfx.json/.vfxb`（color/alpha/size over-life、256×1 RGBA16F LUT、同类型打包 256×N、compute stage 9/10）与 D3D11/Vulkan `hbg_vfx_reference_curves_*` CTest；LUT 字节、runtime binding、表达式 codegen 与 CPU reference 均有单测。
- V1.1.5 新增 `vfx_{bursts,forces,collision,curves}.gltf` + sidecar 和每后端 feature golden；`curve_visual` 用 8 个不同 `playRate` 实例展示 LUT。`run_vfx_golden.cmake` 支持可变帧数；1080p×300 帧性能记录见 `v1-checklist.md`。
- V1.0b.8 `src/render/vfx/vfx_reference.cpp` 通过 `HbgVfxBackend::nativeBuffer`/`findBuffer` 在测试路径执行真实 compute，并 `bgfx::read` 读回 attributes/deadList/indirection/renderCount/indirect；V1.2.1 增加 renderKVP counter/index 校验；V1.2.2 增加 distance KVP key、sorted indirection 与 pre-pass scratch 校验，支持 1024/8192/262144 容量。`--vfx-reference=<asset.vfxb>` 与 `--vfx-reference-graph=<effect.vfx.json>` 逐粒子比较 CPU reference（10 帧、1e-3）。`--vfx-event-reference` 读回 event buffer、receiver attributes/deadList/renderCount，校验 frameCount/prefix/dropped、payload 属性、particleId 与 CPU `simulateEventCount`；`--vfx-cpu-event-reference` 校验 CPU sendEvent 顺序/默认属性/类型拒绝、64 上限与 2 个 dropped。V1.2.3/4 覆盖单链、count=2、双链并发、capacity 丢弃和 CPU 事件。
- `vfx/tests/run_vfx_golden.cmake` 默认 640×360、3 帧、camera/seed，对 D3D11/Vulkan 截图与 golden 做逐字节比较；V1.1.5 feature golden 通过 `HBG_FRAMES` 使用 90/30/30/300 帧，并各自保存每后端 golden。

## 12. V2 编辑器文档 API（不属于 renderer 契约）

- `vfx/include/vfx/editor.h`：`Document::load/save`、system/output/block/property typed setter、结构 add/remove/move、block 参数、Curve/Gradient、undo/redo、layout sidecar、`ParseError`、`defaultBinaryPath()`、`defaultRecoveryPath()`、`errorFocus()`；V2.2b 增加 `copyBlock/pasteBlock/duplicateBlock`、`copyProperty/pasteProperty/duplicateProperty`、`duplicateSystem/duplicateOutput`、`addSubgraphBlock/subgraphNames`、`blockParameterJson/setBlockParameterJson`、`saveRecovery/loadRecovery/clearRecovery`；不暴露 nlohmann、ImGui、bgfx。
- `vfx/include/vfx/asset.h`：`parseGraphJson(json, source, GraphParseMode)`；默认 `FlattenSubgraphs` 保持 compiler/runtime/V1 语义，编辑器用 `PreserveSubgraphs` 保留 raw subgraph invocation。该参数是 additive API，不改变 `.vfxb` binary。
- `vfx/src/editor/document.cpp`：编辑原始 `.vfx.json`（`ordered_json`），每次修改后重新 `parseGraphJson()`；失败恢复旧 JSON；snapshot undo/redo 只保存图/脏标记，布局仍由 sidecar 管理。`errorFocus()` 将 `systems[i].{init,update}.blocks[j]` / `outputs[j]` / `properties[j]` 映射为选择目标。
- layout sidecar `<asset>.editor.json` 只由编辑器读写；`vfxbake`、runtime、graph hash 和 `.vfxb` 都不包含它。`defaultBinaryPath()` 与 `vfxbake` 使用同一 `.vfx.json` → `.vfxb` 规则。
- `src/app/vfx_editor_ui.cpp` 是 app 层 ImGui 消费者，复用 DebugUI context；不是 runtime 依赖。`HBG_BUILD_VFX=OFF` 时由 `vfx_editor_ui_stub.cpp` 提供明确失败。block 默认参数来自 `vfx/src/core/block_catalog.{h,cpp}`，与 V1 parser 共用 field 表。
- `VfxEditorUI::setCompileOptions()`/`setReloadCallback()` 注入 app 层 shaderc/bgfx source/shader pack 路径和 hot reload handler；`Save`/`Compile` 直接调用 `vfx::compileGraphFile()`（链接 `hbg_vfx_compiler`），不 shell 到 `vfxbake`。CLI `--vfx-shaderc`、`--vfx-bgfx-src`、`--vfx-shader-pack` 与 env/CMake 默认均可覆盖。
- `VfxEditorUI::setPreviewCallback()`/`setPlaybackCallback()`/`setPlaybackStatusProvider()`/`setDiagnosticsProvider()` 由 app 注入；preview 使用 `vfx::compileGraph(document->serialize())` 写临时 `.vfxb`，再调 `ForwardRenderer::previewVfxAsset(sourceAsset, binary)`，不触碰 canonical graph/binary。playback 映射到 `ForwardRenderer::{toggleVfxPaused,stepVfx,restartVfx,setVfxPlayRate}`，status 映射 `VfxPlaybackStatus`，诊断映射 `FrameDiagnostics.vfx*`。
- `--vfx-editor-compile` 只加载 graph、编译并退出，供 CI 校验 `.vfxb` 生成；`--vfx-editor-preview` 走未保存图 → 临时 `.vfxb` → preview effect 并退出；`VfxEditorUI::compile()/preview()` 均不写 graph/layout，适合在复制的 fixture 上运行。
- `ForwardRenderer::reloadVfxAsset(binaryPath)` 只影响匹配 scene `VfxInstance.asset` 的已经运行 effect；内部调用 `Runtime::reloadEffect()`，不改 `.vfxb`/既有 runtime API 语义（V2.4 新增 additive `Effect::advanceOneFrame()`）。没有匹配 instance 时返回失败文本，但编译和 binary 写入仍成功。
- `src/app/window.{h,cpp}`/`debug_ui.cpp` 只在 app 层注入 ImGui key/char 事件；不改变 runtime/backend API。
