#include "app/sg_preview_renderer.h"

#include "app/sg_jpeg.h"
#include "core/log.h"
#include "core/math.h"
#include "gfx/buffer.h"
#include "gfx/encoder.h"
#include "gfx/framebuffer_cache.h"
#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/scene/render_scene.h"
#include "render/shadergraph/graph_binding.h"
#include "render/shadergraph/graph_program_cache.h"
#include "render/shadergraph/sgb_asset.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace hbg {

namespace graph = render::graph;

namespace {

constexpr bgfx::ViewId kPreviewView = 4090;
constexpr bgfx::ViewId kPreviewBlitView = 4091;
constexpr int kMinPreviewSize = 128;
constexpr int kMaxPreviewSize = 1024;
constexpr float kPreviewFovDegrees = 40.0f;
constexpr float kDefaultPreviewDistance = 3.0f;

struct PreviewVertex {
    float position[3];
    float normal[3];
    float tangent[4];
    float texcoord0[2];
    float texcoord1[2];
    std::uint8_t color[4];
};

graph::Backend backendForRenderer() {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Vulkan:
        return graph::Backend::Spirv;
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return graph::Backend::Dxbc;
    default:
        return graph::Backend::Dxbc;
    }
}

std::uint64_t hashBytes(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::uint8_t value : bytes) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

void appendQuad(std::vector<PreviewVertex>& vertices, std::vector<std::uint16_t>& indices,
                const std::array<PreviewVertex, 4>& quad) {
    const std::uint16_t base = static_cast<std::uint16_t>(vertices.size());
    for (const PreviewVertex& vertex : quad) {
        vertices.push_back(vertex);
    }
    indices.push_back(base);
    indices.push_back(static_cast<std::uint16_t>(base + 1));
    indices.push_back(static_cast<std::uint16_t>(base + 2));
    indices.push_back(base);
    indices.push_back(static_cast<std::uint16_t>(base + 2));
    indices.push_back(static_cast<std::uint16_t>(base + 3));
}

PreviewVertex makeVertex(float x, float y, float z, float nx, float ny, float nz, float tx, float ty,
                         float tz, float tw, float u, float v) {
    PreviewVertex vertex{};
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.position[2] = z;
    vertex.normal[0] = nx;
    vertex.normal[1] = ny;
    vertex.normal[2] = nz;
    vertex.tangent[0] = tx;
    vertex.tangent[1] = ty;
    vertex.tangent[2] = tz;
    vertex.tangent[3] = tw;
    vertex.texcoord0[0] = u;
    vertex.texcoord0[1] = v;
    vertex.texcoord1[0] = u;
    vertex.texcoord1[1] = v;
    vertex.color[0] = 255;
    vertex.color[1] = 255;
    vertex.color[2] = 255;
    vertex.color[3] = 255;
    return vertex;
}

void buildSphere(std::vector<PreviewVertex>& vertices, std::vector<std::uint16_t>& indices) {
    constexpr std::uint32_t kRings = 24;
    constexpr std::uint32_t kSegments = 48;
    constexpr float kPi = 3.14159265358979323846f;
    vertices.clear();
    indices.clear();
    vertices.reserve((kRings + 1) * (kSegments + 1));
    for (std::uint32_t ring = 0; ring <= kRings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(kRings);
        const float theta = kPi * v;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        for (std::uint32_t segment = 0; segment <= kSegments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(kSegments);
            const float phi = 2.0f * kPi * u;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);
            PreviewVertex vertex = makeVertex(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi,
                                              sinTheta * cosPhi, cosTheta, sinTheta * sinPhi, -sinPhi,
                                              0.0f, cosPhi, 1.0f, u, v);
            vertices.push_back(vertex);
        }
    }
    indices.reserve(kRings * kSegments * 6);
    for (std::uint32_t ring = 0; ring < kRings; ++ring) {
        for (std::uint32_t segment = 0; segment < kSegments; ++segment) {
            const std::uint16_t a = static_cast<std::uint16_t>(ring * (kSegments + 1) + segment);
            const std::uint16_t b = static_cast<std::uint16_t>(a + 1);
            const std::uint16_t c = static_cast<std::uint16_t>(a + kSegments + 1);
            const std::uint16_t d = static_cast<std::uint16_t>(c + 1);
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }
}

void buildPlane(std::vector<PreviewVertex>& vertices, std::vector<std::uint16_t>& indices) {
    vertices.clear();
    indices.clear();
    const std::array<PreviewVertex, 4> quad = {
        makeVertex(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f),
        makeVertex(1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f),
        makeVertex(1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f),
        makeVertex(-1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f)};
    appendQuad(vertices, indices, quad);
}

void addCubeFace(std::vector<PreviewVertex>& vertices, std::vector<std::uint16_t>& indices,
                 const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& normal,
                 const Vec3& tangent) {
    const std::array<PreviewVertex, 4> quad = {
        makeVertex(a.x, a.y, a.z, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z, 1.0f,
                   0.0f, 0.0f),
        makeVertex(b.x, b.y, b.z, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z, 1.0f,
                   1.0f, 0.0f),
        makeVertex(c.x, c.y, c.z, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z, 1.0f,
                   1.0f, 1.0f),
        makeVertex(d.x, d.y, d.z, normal.x, normal.y, normal.z, tangent.x, tangent.y, tangent.z, 1.0f,
                   0.0f, 1.0f)};
    appendQuad(vertices, indices, quad);
}

void buildCube(std::vector<PreviewVertex>& vertices, std::vector<std::uint16_t>& indices) {
    vertices.clear();
    indices.clear();
    const Vec3 x(1.0f, 0.0f, 0.0f);
    const Vec3 y(0.0f, 1.0f, 0.0f);
    const Vec3 z(0.0f, 0.0f, 1.0f);
    addCubeFace(vertices, indices, Vec3(-1, -1, 1), Vec3(1, -1, 1), Vec3(1, 1, 1),
                Vec3(-1, 1, 1), z, x);
    addCubeFace(vertices, indices, Vec3(1, -1, -1), Vec3(-1, -1, -1), Vec3(-1, 1, -1),
                Vec3(1, 1, -1), Vec3(0, 0, -1), Vec3(-1, 0, 0));
    addCubeFace(vertices, indices, Vec3(1, -1, 1), Vec3(1, -1, -1), Vec3(1, 1, -1),
                Vec3(1, 1, 1), x, Vec3(0, 0, -1));
    addCubeFace(vertices, indices, Vec3(-1, -1, -1), Vec3(-1, -1, 1), Vec3(-1, 1, 1),
                Vec3(-1, 1, -1), Vec3(-1, 0, 0), z);
    addCubeFace(vertices, indices, Vec3(-1, 1, 1), Vec3(1, 1, 1), Vec3(1, 1, -1),
                Vec3(-1, 1, -1), y, x);
    addCubeFace(vertices, indices, Vec3(-1, -1, -1), Vec3(1, -1, -1), Vec3(1, -1, 1),
                Vec3(-1, -1, 1), Vec3(0, -1, 0), x);
}

struct PreviewMesh {
    gfx::BufferHandle vertexBuffer;
    gfx::BufferHandle indexBuffer;
    std::uint32_t indexCount = 0;
};

bool createMesh(gfx::ResourceManager& resources, const std::vector<PreviewVertex>& vertices,
                const std::vector<std::uint16_t>& indices, const char* name, PreviewMesh& mesh) {
    if (vertices.empty() || indices.empty()) {
        return false;
    }
    gfx::VertexLayout layout;
    layout.position().normal().tangent().texCoord0().texCoord1().color0().end();

    gfx::BufferDesc vertexDesc;
    vertexDesc.type = gfx::BufferType::Vertex;
    vertexDesc.data = vertices.data();
    vertexDesc.size = static_cast<std::uint32_t>(vertices.size() * sizeof(PreviewVertex));
    vertexDesc.count = static_cast<std::uint32_t>(vertices.size());
    vertexDesc.layout = &layout;
    vertexDesc.name = std::string(name) + ".vertices";
    mesh.vertexBuffer = resources.createBuffer(vertexDesc);

    gfx::BufferDesc indexDesc;
    indexDesc.type = gfx::BufferType::Index;
    indexDesc.data = indices.data();
    indexDesc.size = static_cast<std::uint32_t>(indices.size() * sizeof(std::uint16_t));
    indexDesc.count = static_cast<std::uint32_t>(indices.size());
    indexDesc.name = std::string(name) + ".indices";
    mesh.indexBuffer = resources.createBuffer(indexDesc);
    mesh.indexCount = static_cast<std::uint32_t>(indices.size());
    return mesh.vertexBuffer.valid() && mesh.indexBuffer.valid() && mesh.indexCount > 0;
}

std::uint32_t clearColorFor(const std::string& background) {
    if (background == "black") {
        return 0x000000ff;
    }
    if (background == "gray") {
        return 0x303030ff;
    }
    if (background == "sky") {
        return 0x000000ff;
    }
    return 0x303030ff;
}

bool isClearPixel(const std::uint8_t* pixel, std::uint32_t clear) {
    const int red = static_cast<int>((clear >> 24u) & 0xffu);
    const int green = static_cast<int>((clear >> 16u) & 0xffu);
    const int blue = static_cast<int>((clear >> 8u) & 0xffu);
    return std::abs(static_cast<int>(pixel[0]) - red) <= 2 &&
           std::abs(static_cast<int>(pixel[1]) - green) <= 2 &&
           std::abs(static_cast<int>(pixel[2]) - blue) <= 2;
}

void applyBackground(std::vector<std::uint8_t>& pixels, int width, int height,
                     const std::string& background) {
    const std::uint32_t clear = clearColorFor(background);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                        static_cast<std::size_t>(x)) *
                                       4u;
            if (!isClearPixel(pixels.data() + offset, clear)) {
                continue;
            }
            if (background == "checker") {
                const bool light = ((x / 16) + (y / 16)) % 2 == 0;
                const std::uint8_t value = light ? 0x4a : 0x2c;
                pixels[offset + 0] = value;
                pixels[offset + 1] = value;
                pixels[offset + 2] = value;
                pixels[offset + 3] = 255;
            } else if (background == "sky") {
                const float t = static_cast<float>(y) / static_cast<float>(std::max(height - 1, 1));
                const Vec3 top(0.20f, 0.33f, 0.48f);
                const Vec3 bottom(0.62f, 0.68f, 0.74f);
                const Vec3 color(top * (1.0f - t) + bottom * t);
                pixels[offset + 0] =
                    static_cast<std::uint8_t>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f);
                pixels[offset + 1] =
                    static_cast<std::uint8_t>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f);
                pixels[offset + 2] =
                    static_cast<std::uint8_t>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f);
                pixels[offset + 3] = 255;
            }
        }
    }
}

void applyChannel(std::vector<std::uint8_t>& pixels, const std::string& channel) {
    if (channel == "rgb" || channel.empty()) {
        return;
    }
    std::size_t index = 0;
    if (channel == "r") {
        index = 0;
    } else if (channel == "g") {
        index = 1;
    } else if (channel == "b") {
        index = 2;
    } else if (channel == "a") {
        index = 3;
    } else {
        return;
    }
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
        const std::uint8_t value = pixels[offset + index];
        pixels[offset + 0] = value;
        pixels[offset + 1] = value;
        pixels[offset + 2] = value;
        pixels[offset + 3] = 255;
    }
}

} // namespace

struct SgGraphPreviewRenderer::Impl {
    gfx::ResourceManager* resources = nullptr;
    gfx::generated::GlobalUniformBinder globalBinder;
    gfx::generated::GlobalUniforms globals{};
    graph::GraphUniformCache uniformCache;
    graph::GraphProgramCache programCache;
    gfx::TextureHandle white;
    gfx::TextureHandle textureBlack;
    gfx::TextureHandle grey;
    gfx::TextureHandle normal;
    gfx::TextureHandle linearGrey;
    gfx::TextureHandle red;
    gfx::TextureHandle whiteArray;
    gfx::TextureHandle white3D;
    gfx::TextureHandle whiteCube;
    gfx::TextureHandle color;
    gfx::TextureHandle depth;
    gfx::TextureHandle readback;
    gfx::FrameBufferHandle framebuffer;
    std::uint16_t targetSize = 0;
    PreviewMesh sphere;
    PreviewMesh plane;
    PreviewMesh cube;
    PreviewMesh quad;
    std::shared_ptr<graph::Asset> asset;
    std::unique_ptr<graph::GraphBinding> binding;
    std::uint64_t assetHash = 0;

    void destroyTargets() {
        if (resources == nullptr) {
            return;
        }
        if (framebuffer.valid()) {
            resources->destroyFrameBuffer(framebuffer);
            framebuffer = {};
        }
        for (gfx::TextureHandle* texture : {&color, &depth, &readback}) {
            if (texture->valid()) {
                resources->destroyTexture(*texture);
                *texture = {};
            }
        }
        targetSize = 0;
    }

    void destroyMesh(PreviewMesh& mesh) {
        if (resources == nullptr) {
            return;
        }
        if (mesh.vertexBuffer.valid()) {
            resources->destroyBuffer(mesh.vertexBuffer);
        }
        if (mesh.indexBuffer.valid()) {
            resources->destroyBuffer(mesh.indexBuffer);
        }
        mesh = {};
    }

    void destroyRuntime() {
        if (resources == nullptr) {
            return;
        }
        if (binding != nullptr) {
            binding->destroy(*resources);
            binding.reset();
        }
        if (asset != nullptr) {
            programCache.release(asset->contentHash());
            asset.reset();
        }
        assetHash = 0;
    }

    bool ensureTargets(int requestedSize) {
        const std::uint16_t size = static_cast<std::uint16_t>(
            std::clamp(requestedSize, kMinPreviewSize, kMaxPreviewSize));
        if (size == targetSize && color.valid() && depth.valid() && readback.valid() &&
            framebuffer.valid()) {
            return true;
        }
        destroyTargets();
        if (resources == nullptr) {
            return false;
        }

        gfx::TextureDesc colorDesc;
        colorDesc.width = size;
        colorDesc.height = size;
        colorDesc.format = gfx::TextureFormat::RGBA8;
        colorDesc.renderTarget = true;
        colorDesc.name = "sg.preview.color";
        color = resources->createTexture(colorDesc);

        gfx::TextureDesc depthDesc;
        depthDesc.width = size;
        depthDesc.height = size;
        depthDesc.format = gfx::TextureFormat::D24S8;
        depthDesc.depthStencil = true;
        depthDesc.name = "sg.preview.depth";
        depth = resources->createTexture(depthDesc);

        gfx::TextureDesc readbackDesc;
        readbackDesc.width = size;
        readbackDesc.height = size;
        readbackDesc.format = gfx::TextureFormat::RGBA8;
        readbackDesc.flags = BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK;
        readbackDesc.name = "sg.preview.readback";
        readback = resources->createTexture(readbackDesc);

        gfx::FrameBufferKey key;
        key.attachments = {color, depth};
        framebuffer = resources->createFrameBuffer(key, "sg.preview.framebuffer");
        if (!color.valid() || !depth.valid() || !readback.valid() || !framebuffer.valid()) {
            destroyTargets();
            return false;
        }
        targetSize = size;
        return true;
    }

    bool ensureAsset(const std::vector<std::uint8_t>& bytes, std::string& error) {
        const std::uint64_t hash = hashBytes(bytes);
        if (asset != nullptr && assetHash == hash) {
            return true;
        }
        destroyRuntime();
        auto replacement = std::make_shared<graph::Asset>();
        if (!replacement->loadFromMemory(bytes, &error)) {
            return false;
        }
        if (!graph::validateStageContract(*replacement, &error)) {
            return false;
        }
        if (replacement->findPass(graph::PassKind::Forward) == nullptr) {
            error = "preview asset has no forward pass";
            return false;
        }
        asset = std::move(replacement);
        assetHash = hash;
        return true;
    }

    bool ensureBinding(std::string& error) {
        if (binding != nullptr) {
            return true;
        }
        if (asset == nullptr || resources == nullptr || !white.valid()) {
            error = "preview renderer is not initialized";
            return false;
        }
        asset::Material material;
        render::RenderScene scene;
        graph::GraphBindingFallbacks fallbacks;
        fallbacks.white = white;
        fallbacks.black = textureBlack;
        fallbacks.textureBlack = textureBlack;
        fallbacks.grey = grey;
        fallbacks.normal = normal;
        fallbacks.linearGrey = linearGrey;
        fallbacks.red = red;
        fallbacks.whiteArray = whiteArray;
        fallbacks.white3D = white3D;
        fallbacks.whiteCube = whiteCube;
        auto candidate = std::make_unique<graph::GraphBinding>();
        if (!candidate->init(*resources, uniformCache, *asset, material, scene, fallbacks, &error)) {
            return false;
        }
        binding = std::move(candidate);
        return true;
    }

    PreviewMesh* meshFor(const std::string& name) {
        if (name == "plane") {
            return &plane;
        }
        if (name == "cube") {
            return &cube;
        }
        if (name == "quad") {
            return &quad;
        }
        return &sphere;
    }

    bool buildCamera(const SgPreviewOptions& options, Mat4& view, Mat4& projection,
                     Vec3& cameraPosition) {
        const float distance = options.distance > 0.0f ? options.distance : kDefaultPreviewDistance;
        if (!std::isfinite(distance) || distance <= 0.0f) {
            return false;
        }
        const float yaw = options.yaw * 3.14159265358979323846f / 180.0f;
        const float pitch =
            std::clamp(options.pitch, -85.0f, 85.0f) * 3.14159265358979323846f / 180.0f;
        cameraPosition = Vec3(std::sin(yaw) * std::cos(pitch) * distance,
                              std::sin(pitch) * distance,
                              -std::cos(yaw) * std::cos(pitch) * distance);
        view = lookAtRH(cameraPosition, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
        const bool homogeneous = bgfx::getCaps()->homogeneousDepth;
        if (options.mesh == "quad") {
            projection = orthographicRH(-1.1f, 1.1f, -1.1f, 1.1f, 0.1f, 100.0f, homogeneous);
        } else {
            projection = perspectiveRH(kPreviewFovDegrees * 3.14159265358979323846f / 180.0f, 1.0f,
                                       0.1f, 100.0f, homogeneous);
        }
        return true;
    }

    void bindGlobals(const Mat4& view, const Mat4& projection, const Vec3& cameraPosition,
                     std::uint16_t size) {
        const Mat4 viewProjection = projection * view;
        globals = {};
        globals.view = view;
        globals.invView = inverse(view);
        globals.proj = projection;
        globals.invProj = inverse(projection);
        globals.viewProj = viewProjection;
        globals.invViewProj = inverse(viewProjection);
        globals.nonJitteredViewProj = viewProjection;
        globals.nonJitteredInvViewProj = inverse(viewProjection);
        globals.prevViewProj = viewProjection;
        globals.prevInvViewProj = inverse(viewProjection);
        globals.screenSize =
            Vec4(static_cast<float>(size), static_cast<float>(size), 1.0f / static_cast<float>(size),
                 1.0f / static_cast<float>(size));
        globals.cameraPos = Vec4(cameraPosition.x, cameraPosition.y, cameraPosition.z, 1.0f);
        globals.prevCameraPos = globals.cameraPos;
        globals.taaFrameInfo = Vec4(0.0f, 1.0f / 60.0f, 0.0f, 1.0f);
        globals.rtScale = Vec4(1.0f, 1.0f, 0.0f, 0.0f);
        globals.dynamicResolutionScale = Vec4(1.0f, 1.0f, 0.0f, 0.0f);
        globals.time = Vec4(0.0f, 1.0f / 60.0f, 0.0f, 0.0f);
        for (Vec4& frustumPlane : globals.frustumPlanes) {
            frustumPlane = Vec4();
        }
        globalBinder.upload(globals);
    }

    bool renderFrame(const SgPreviewOptions& options, std::string& error) {
        PreviewMesh* mesh = meshFor(options.mesh);
        if (mesh == nullptr || !mesh->vertexBuffer.valid() || !mesh->indexBuffer.valid()) {
            error = "preview mesh is unavailable";
            return false;
        }
        Mat4 view;
        Mat4 projection;
        Vec3 cameraPosition;
        if (!buildCamera(options, view, projection, cameraPosition)) {
            error = "invalid preview camera";
            return false;
        }
        const auto program =
            programCache.program(*asset, graph::PassKind::Forward, binding->variantKey());
        if (!program.valid()) {
            error = "preview shader program is unavailable";
            return false;
        }

        const std::uint32_t clear = clearColorFor(options.background);
        gfx::Encoder encoder(*resources);
        encoder.setViewName(kPreviewView, "SGPreview");
        encoder.setViewRect(kPreviewView, 0, 0, targetSize, targetSize);
        encoder.setViewFrameBuffer(kPreviewView, framebuffer);
        encoder.setViewClear(kPreviewView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clear, 1.0f, 0);
        encoder.setViewTransform(kPreviewView, view.values.data(), projection.values.data());
        bindGlobals(view, projection, cameraPosition, targetSize);
        binding->bind(encoder);
        encoder.setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS);
        const Mat4 model = Mat4::identity();
        encoder.setTransform(model.values.data());
        encoder.setVertexBuffer(0, mesh->vertexBuffer);
        encoder.setIndexBuffer(mesh->indexBuffer);
        encoder.submit(kPreviewView, program);
        bgfx::frame();
        resources->endFrame();
        return true;
    }

    bool readColor(std::vector<std::uint8_t>& pixels, std::string& error) {
        if (!color.valid() || !readback.valid()) {
            error = "preview color target is unavailable";
            return false;
        }
        bgfx::setViewRect(kPreviewBlitView, 0, 0, targetSize, targetSize);
        bgfx::blit(kPreviewBlitView,
                   {.handle = resources->native(readback), .width = targetSize, .height = targetSize},
                   {.handle = resources->native(color), .width = targetSize, .height = targetSize});
        bgfx::touch(kPreviewBlitView);

        pixels.resize(static_cast<std::size_t>(targetSize) * static_cast<std::size_t>(targetSize) * 4u);
        const std::uint32_t expectedFrame =
            bgfx::read({.handle = resources->native(readback), .width = targetSize, .height = targetSize},
                       pixels.data());
        if (expectedFrame == 0) {
            error = "preview color readback failed";
            return false;
        }
        std::uint32_t currentFrame = bgfx::frame();
        resources->endFrame();
        while (currentFrame < expectedFrame) {
            currentFrame = bgfx::frame();
            resources->endFrame();
        }
        return true;
    }
};

SgGraphPreviewRenderer::SgGraphPreviewRenderer() = default;

SgGraphPreviewRenderer::~SgGraphPreviewRenderer() = default;

bool SgGraphPreviewRenderer::init(gfx::ResourceManager& resources, std::string* error) {
    if (m_impl != nullptr) {
        if (m_impl->resources == &resources) {
            return true;
        }
        if (m_impl->resources != nullptr) {
            shutdown(*m_impl->resources);
        } else {
            m_impl.reset();
        }
    }
    auto impl = std::make_unique<Impl>();
    impl->resources = &resources;
    if (!impl->globalBinder.init(resources) || !impl->uniformCache.init(resources) ||
        !impl->programCache.init(resources, backendForRenderer())) {
        if (error != nullptr) {
            *error = "failed to initialize preview shader state";
        }
        impl->programCache.shutdown(resources);
        impl->uniformCache.shutdown(resources);
        impl->globalBinder.shutdown(resources);
        return false;
    }

    const std::array<std::uint8_t, 4> whitePixel = {255, 255, 255, 255};
    gfx::TextureDesc whiteDesc;
    whiteDesc.width = 1;
    whiteDesc.height = 1;
    whiteDesc.format = gfx::TextureFormat::RGBA8;
    whiteDesc.data = whitePixel.data();
    whiteDesc.dataSize = static_cast<std::uint32_t>(whitePixel.size());
    whiteDesc.flags = BGFX_SAMPLER_UVW_CLAMP;
    whiteDesc.name = "sg.preview.white";
    impl->white = resources.createTexture(whiteDesc);
    if (!impl->white.valid()) {
        if (error != nullptr) {
            *error = "failed to create preview fallback texture";
        }
        impl->globalBinder.shutdown(resources);
        impl->programCache.shutdown(resources);
        impl->uniformCache.shutdown(resources);
        return false;
    }

    std::vector<PreviewVertex> vertices;
    std::vector<std::uint16_t> indices;
    buildSphere(vertices, indices);
    if (!createMesh(resources, vertices, indices, "sg.preview.sphere", impl->sphere)) {
        if (error != nullptr) {
            *error = "failed to create preview sphere";
        }
        impl->destroyMesh(impl->sphere);
        resources.destroyTexture(impl->white);
        impl->globalBinder.shutdown(resources);
        impl->programCache.shutdown(resources);
        impl->uniformCache.shutdown(resources);
        return false;
    }
    buildPlane(vertices, indices);
    if (!createMesh(resources, vertices, indices, "sg.preview.plane", impl->plane)) {
        if (error != nullptr) {
            *error = "failed to create preview plane";
        }
        impl->destroyMesh(impl->sphere);
        impl->destroyMesh(impl->plane);
        resources.destroyTexture(impl->white);
        impl->globalBinder.shutdown(resources);
        impl->programCache.shutdown(resources);
        impl->uniformCache.shutdown(resources);
        return false;
    }
    buildPlane(vertices, indices);
    if (!createMesh(resources, vertices, indices, "sg.preview.quad", impl->quad)) {
        if (error != nullptr) {
            *error = "failed to create preview quad";
        }
        impl->destroyMesh(impl->sphere);
        impl->destroyMesh(impl->plane);
        impl->destroyMesh(impl->quad);
        resources.destroyTexture(impl->white);
        impl->globalBinder.shutdown(resources);
        impl->programCache.shutdown(resources);
        impl->uniformCache.shutdown(resources);
        return false;
    }
    buildCube(vertices, indices);
    if (!createMesh(resources, vertices, indices, "sg.preview.cube", impl->cube)) {
        if (error != nullptr) {
            *error = "failed to create preview cube";
        }
        impl->destroyMesh(impl->sphere);
        impl->destroyMesh(impl->plane);
        impl->destroyMesh(impl->cube);
        impl->destroyMesh(impl->quad);
        resources.destroyTexture(impl->white);
        impl->globalBinder.shutdown(resources);
        impl->programCache.shutdown(resources);
        impl->uniformCache.shutdown(resources);
        return false;
    }

    const auto createSolidFallback = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                         bool cubeMap, bool is3D, uint16_t layers,
                                         uint16_t depth, const char* name) {
        const uint32_t pixelCount = (cubeMap ? 6u : 1u) * (is3D ? depth : 1u) *
                                    (cubeMap ? 1u : layers);
        std::vector<uint8_t> pixels(static_cast<std::size_t>(pixelCount) * 4u);
        for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
            pixels[pixel * 4u + 0u] = r;
            pixels[pixel * 4u + 1u] = g;
            pixels[pixel * 4u + 2u] = b;
            pixels[pixel * 4u + 3u] = a;
        }
        gfx::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.layers = layers;
        desc.depth = is3D ? depth : 1;
        desc.format = gfx::TextureFormat::RGBA8;
        desc.cubeMap = cubeMap;
        desc.is3D = is3D;
        desc.data = pixels.data();
        desc.dataSize = static_cast<uint32_t>(pixels.size());
        desc.flags = BGFX_SAMPLER_UVW_CLAMP;
        desc.name = name;
        return resources.createTexture(desc);
    };
    const auto createWhiteFallback = [&](bool cubeMap, bool is3D, uint16_t layers, uint16_t depth,
                                         const char* name) {
        return createSolidFallback(255, 255, 255, 255, cubeMap, is3D, layers, depth, name);
    };
    impl->textureBlack =
        createSolidFallback(0, 0, 0, 255, false, false, 1, 1, "sg.preview.textureBlack");
    impl->grey =
        createSolidFallback(128, 128, 128, 255, false, false, 1, 1, "sg.preview.grey");
    impl->normal =
        createSolidFallback(128, 128, 255, 255, false, false, 1, 1, "sg.preview.normal");
    impl->linearGrey =
        createSolidFallback(128, 128, 128, 255, false, false, 1, 1, "sg.preview.linearGrey");
    impl->red =
        createSolidFallback(255, 0, 0, 255, false, false, 1, 1, "sg.preview.red");
    impl->whiteArray = createWhiteFallback(false, false, 2, 1, "sg.preview.whiteArray");
    impl->white3D = createWhiteFallback(false, true, 1, 1, "sg.preview.white3D");
    impl->whiteCube = createWhiteFallback(true, false, 1, 1, "sg.preview.whiteCube");
    if (!impl->textureBlack.valid() || !impl->grey.valid() || !impl->normal.valid() ||
        !impl->linearGrey.valid() || !impl->red.valid() || !impl->whiteArray.valid() ||
        !impl->white3D.valid() || !impl->whiteCube.valid()) {
        if (error != nullptr) {
            *error = "failed to create preview fallback textures";
        }
        for (gfx::TextureHandle* texture : {&impl->textureBlack, &impl->grey, &impl->normal,
                                            &impl->linearGrey, &impl->red, &impl->whiteArray,
                                            &impl->white3D, &impl->whiteCube, &impl->white}) {
            if (texture->valid()) {
                resources.destroyTexture(*texture);
            }
        }
        impl->destroyMesh(impl->sphere);
        impl->destroyMesh(impl->plane);
        impl->destroyMesh(impl->cube);
        impl->destroyMesh(impl->quad);
        impl->globalBinder.shutdown(resources);
        impl->programCache.shutdown(resources);
        impl->uniformCache.shutdown(resources);
        return false;
    }
    m_impl = std::move(impl);
    return true;
}

void SgGraphPreviewRenderer::shutdown(gfx::ResourceManager& resources) {
    if (m_impl == nullptr) {
        return;
    }
    m_impl->destroyRuntime();
    m_impl->destroyTargets();
    m_impl->destroyMesh(m_impl->sphere);
    m_impl->destroyMesh(m_impl->plane);
    m_impl->destroyMesh(m_impl->cube);
    m_impl->destroyMesh(m_impl->quad);
    for (gfx::TextureHandle* texture : {&m_impl->white, &m_impl->textureBlack, &m_impl->grey,
                                        &m_impl->normal, &m_impl->linearGrey, &m_impl->red,
                                        &m_impl->whiteArray, &m_impl->white3D,
                                        &m_impl->whiteCube}) {
        if (texture->valid()) {
            resources.destroyTexture(*texture);
        }
    }
    m_impl->programCache.shutdown(resources);
    m_impl->uniformCache.shutdown(resources);
    m_impl->globalBinder.shutdown(resources);
    m_impl->resources = nullptr;
    m_impl.reset();
}

bool SgGraphPreviewRenderer::render(const std::vector<std::uint8_t>& sgb,
                                    const SgPreviewOptions& options,
                                    std::vector<std::uint8_t>& jpeg, std::string& error) {
    if (m_impl == nullptr || m_impl->resources == nullptr) {
        error = "preview renderer is not initialized";
        return false;
    }
    if (sgb.empty()) {
        error = "preview shader is empty";
        return false;
    }
    if (!m_impl->ensureAsset(sgb, error) || !m_impl->ensureBinding(error) ||
        !m_impl->ensureTargets(options.size)) {
        if (error.empty()) {
            error = "preview resources are unavailable";
        }
        return false;
    }
    if (!m_impl->renderFrame(options, error)) {
        return false;
    }
    std::vector<std::uint8_t> pixels;
    if (!m_impl->readColor(pixels, error)) {
        return false;
    }
    applyBackground(pixels, m_impl->targetSize, m_impl->targetSize, options.background);
    applyChannel(pixels, options.channel);
    const bool png = options.format == "png";
    const bool encoded = png ? encodePng(pixels.data(), m_impl->targetSize, m_impl->targetSize, jpeg)
                             : encodeJpeg(pixels.data(), m_impl->targetSize, m_impl->targetSize,
                                          88, jpeg);
    if (!encoded) {
        error = png ? "preview PNG encoding failed" : "preview JPEG encoding failed";
        return false;
    }
    error.clear();
    return true;
}

} // namespace hbg
