# VFX 后端 API 与引擎接入

> 目标：独立库只暴露 C++20 接口；hdrp-bgfx 适配层实现它们。
> 约束：公开头文件不出现 `bgfx::*` / `hbg::*` / RenderGraph 类型；所有 GPU 资源用 opaque handle。
> 线程：全部在主线程调用；资源销毁可延迟（由适配层 `ResourceManager` 处理）。

---

## 1. 头文件布局

```text
vfx/include/vfx/
├─ types.h            Vec2/3/4, Mat4, Bounds, BlendMode, CompareFunc, TextureFormat, UniformType, Limits
├─ handles.h          BufferHandle/TextureHandle/ProgramHandle/UniformHandle/SamplerHandle
├─ asset.h            Graph JSON 解析（compiler/tool）
├─ runtime.h          Runtime/Effect/EffectDesc/Diagnostics；Asset 为 opaque
├─ device.h           Device 接口
├─ command_sink.h     CommandSink 接口、Access、DrawState、PassDesc
├─ shading.h          ShadingProvider 接口、FrameContext、FrameLighting
├─ events.h           EventAttribute, PropertyValue
└─ compiler.h         compileGraph（编辑器/工具）
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

- `bindLighting` 只在 lit output 调用一次（或按需多次）；unlit 不调用。
- Lit 的 stage 表必须与 `docs/vfx/architecture.md` §4.4 一致；适配层负责 `u_pass_*`/`u_global_*`/`u_object_*`/`u_cluster_*`/`u_environment_*` 上传。
- 程序本体来自 `.vfxb` 的 shader blob；`resolveProgram` 默认返回资产内的 program。

## 5. Runtime / Effect

```cpp
struct EffectDesc {
    Mat4 transform = Mat4::identity();
    bool playOnAwake = true;
    float playRate = 1.0f;
    uint32_t randomSeed = 0;
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
    virtual void setTransform(const Mat4& world) = 0;
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
    virtual bool isCulled() const = 0;
};

class Runtime {
public:
    Runtime(Device&, ShadingProvider*);
    ~Runtime();

    std::shared_ptr<const Asset> loadAsset(const std::filesystem::path&, std::string* error = nullptr);
    Effect* createEffect(const Asset&, const EffectDesc&, std::string* error = nullptr);
    void destroyEffect(Effect*);

    void update(float deltaSeconds);
    void encodeSimulation(CommandSink&, const FrameContext&);
    void encodeOutput(CommandSink&, const FrameContext&);

    void setErrorCallback(std::function<void(std::string_view)>);
    Diagnostics diagnostics() const;
};

}
```

语义：
- `loadAsset(path)`：读取并校验 `.vfxb` bytes；不依赖 compiler target。
- `update(dt)`：clamp dt、推进 spawner/事件、更新 bounds；不产生 GPU 命令。
- `encodeSimulation`：按 system 依赖顺序 dispatch；内部 `beginPass({name, simulation=true, ...})`。
- `encodeOutput`：按输出排序提交 indirect draw；内部 `beginPass({name, simulation=false, ...})`。
- `setProperty` 的 Curve/Gradient 会立即重新生成并上传 LUT 纹理（`Device::updateTexture`）；V1 允许在播放中修改。
- `PropertyValue` 中的 `std::span` 只在调用期间有效；库会立即拷贝控制点，不保留调用方内存。
- `sendEvent` 在 V1.0b.6 只记录“不支持”，V1.2 前没有事件 buffer。
- `Effect` 由 `Runtime` 拥有；`destroyEffect` 后指针失效。
- 多 effect 各自独立 buffer/program；同一 `Asset` 可创建多个 effect。
- `Diagnostics::particles` 是 CPU spawn 估算，不是 GPU dead list 读回；`draws`/`dispatches` 为最近一次 encode 计数。V1.0b.8 的 `--vfx-reference` 测试路径会实际读回 GPU deadList，但不回写交互 diagnostics。
- `Device` 没有 uniform/sampler 销毁接口；runtime 创建的名称缓存由适配层 `HbgVfxBackend` 持有，随 backend 析构释放。buffer/program/texture 由 runtime/Effect 释放。
- 输出 pass 由 runtime 设置 `u_vfx_world`：local system 用 effect transform，world system 用 identity；global camera uniforms 来自 `FrameContext`。
- `Effect::isCulled()` 在 V1.0b.6 恒为 false；相机/包围盒剔除在 V1.0b.7 集成。

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
    bool ok() const { return error.empty(); }
};

CompileResult compileGraph(std::string_view json, const CompileOptions&);
CompileResult compileGraphFile(const std::filesystem::path&, const CompileOptions&);
```

- `vfxbake` 直接调用 `compileGraphFile`。
- 实现：`vfx/include/vfx/compiler.h`、`vfx/src/compiler/compiler.cpp`、`codegen.cpp`、`binary.cpp`；目标 `hbg_vfx_compiler`，不链接进 runtime/app。
- `shaderc`/`bgfxSource` 必须显式提供；同源 shader 在单次 `compileGraph` 内缓存；错误包含 JSON 路径/shaderc 输出。
- V1.0b.5 只生成 unlit quad；`debugInfo` 保留但暂不写 DEBUG section；`shaderPack` 仅追加 include 路径。

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
out.setDepthAttachment(depth);
out.allowCulling(false);
out.setRenderFunc([&](RenderGraphContext& ctx) {
    backend.begin(ctx.encoder(), ctx.viewId());
    vfx::FrameContext frame = backend.makeFrameContext(viewState, resources);
    backend.bindLighting(backend, frame, {});     // lit output 需要
    runtime.encodeOutput(backend, frame);
});
```

- `HbgVfxBackend` 同时实现 `Device`、`CommandSink`、`ShadingProvider`（或拆成三个对象）。
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
| `FrameContext` | `ViewState` + `RenderScene` 相机 + 当前 `resources.depth` |

## 9. 错误、诊断、生命周期

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
      float encodeMs = 0.0f;
  };
  ```
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
  - 事件 prefix sum 调用。

## 11. 实现状态（V1.0b.7）

- 公共接口已落地：`vfx/include/vfx/device.h`、`command_sink.h`、`shading.h`、`runtime.h`、`events.h`；`types.h`/`handles.h` 补齐 `Mat4`、格式、opaque buffer/uniform/sampler handle。
- Mock：`vfx/tests/support/mock_backend.h`；测试 `vfx/tests/test_vfx_backend.cpp`。
- Runtime：`vfx/src/runtime/runtime_asset.cpp` 校验/读取 `.vfxb`；`vfx/src/runtime/runtime.cpp` 实现 effect 资源、常量/Burst 累积、simulation/output encode、LUT 上传和诊断。CMake target `hbg_vfx_runtime`；测试 target `hbg_vfx_runtime_tests`。
- CPU reference：`vfx/include/vfx/reference.h` 的 `SpawnerState`/`updateSpawner` 提供与 runtime 相同的 rate/burst/dt clamp 语义；`simulateParticle` 提供粒子 init/update 参考。
- 适配层：`src/render/vfx/hbg_vfx_backend.h`、`hbg_vfx_backend.cpp`；CMake target `hbg_vfx_adapter`。测试 target `hbg_vfx_adapter_tests`。
- `HbgVfxBackend::createProgram` 消费 `.vfxb` shader section 的 `vfx::ShaderBlob`；runtime reader 在 V1.0b.6 落地，适配层不解析容器。
- `StorageVertex` 用 `BufferDesc::stride` 合成 `gfx::VertexLayout`，仅满足 `bgfx::createDynamicVertexBuffer` 的 stride 校验；VS 仍通过 `BUFFER_RAW_RO` + word offset 读取，不依赖该 layout 属性顺序。
- 带 `initialData` 的 Storage/StorageVertex 以 `BGFX_BUFFER_COMPUTE_READ` 创建；可写 storage 创建空 buffer。runtime 对 attributes/indirection 不传 initialData；reset 时额外 dispatch `dead_list_init`，由 GPU 清空 attributes/indirection 并重建 deadList。
- `ForwardRenderer::ensureVfx` 在 `supportVFX` 开启且 scene 有 VFX 实例时惰性创建 `HbgVfxBackend`/`vfx::Runtime`；`updateVfx`、simulation pass、output pass 已接入 RenderGraph。
- `bindLighting` 仍只记录调用；`resolveProgram` 返回无效 handle；ForwardRenderer 自行构造 `FrameContext`（相机/时间/尺寸），未走 `HbgVfxBackend::makeFrameContext`。Runtime unlit 不调用 `bindLighting`；lit 留到 V1.4。
- `HbgVfxBackend` 对 uniform/sampler 名称缓存区分“自己创建”和“共享的 `u_global_*`”；shutdown 不销毁共享 handle。
- `drawIndirect` 首次调用时懒创建 6 顶点 dummy vertex buffer；与 V0 的 D3D11 indirect draw 约束一致。
- 适配层测试使用 bgfx `Noop` 后端验证 100 个 buffer/program 的资源生命周期、encoder 命令映射、从 checked-in `.vfxb` 创建 7 个 program，以及 runtime effect 资源路径；`vfx_scene.gltf` 已在 D3D11/Vulkan render-test 出图且两后端 PNG 0 diff。
- Runtime 测试使用 MockBackend 验证资源生命周期、6 次首次 dispatch/5 次后续 dispatch、逐 kernel stage/buffer binding、spawnCount、property/Curve/Gradient LUT、local bounds、indirect output、reset/stop/play 和损坏 hash 拒绝；V1.1.1 新增 `spawner_bursts` 固定 dt 序列（同帧合并、period/repeat、dt clamp）。
- V1.1.2 新增 `forces_1024.vfx.json/.vfxb`（gravity、useSize drag、absolute/relative force、turbulence、attractor）与 D3D11/Vulkan `hbg_vfx_reference_forces_*` CTest；CPU reference 与 GPU readback 在 10 帧内逐粒子 1e-3 比较，turbulence 关闭的 `minimal_unlit` golden 保持 0 diff。
- V1.0b.8 `src/render/vfx/vfx_reference.cpp` 通过 `HbgVfxBackend::nativeBuffer`/`findBuffer` 在测试路径执行真实 compute，并 `bgfx::read` 读回 attributes/deadList/indirection/renderCount/indirect；`--vfx-reference=<asset.vfxb>` 与 `--vfx-reference-graph=<effect.vfx.json>` 逐粒子比较 CPU reference（1024 粒子、10 帧、1e-3）。
- `vfx/tests/run_vfx_golden.cmake` 固定 640×360、3 帧、camera/seed，对 D3D11/Vulkan 截图与 `vfx/tests/golden/vfx_scene.png` 做逐字节比较。
