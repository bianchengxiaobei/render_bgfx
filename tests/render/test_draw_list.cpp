#include "render/camera/view_state.h"
#include "render/culling/draw_list.h"
#include "render/material/render_queue.h"
#include "render/material/shader_pass.h"
#include "render/scene/render_scene.h"

#include <doctest/doctest.h>

#include <string>

using namespace hbg;
using namespace hbg::render;

namespace {

Camera makeCamera(const Vec3& position = Vec3(0.0f, 0.0f, 10.0f), float farClip = 100.0f) {
    Camera camera;
    camera.world = inverse(lookAtRH(position, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)));
    camera.yfov = 1.0471975512f;
    camera.nearClip = 0.1f;
    camera.farClip = farClip;
    return camera;
}

Bounds makeBounds(float centerX, float centerY, float centerZ, float radius = 1.0f) {
    Bounds bounds;
    bounds.min = Vec3(centerX - radius, centerY - radius, centerZ - radius);
    bounds.max = Vec3(centerX + radius, centerY + radius, centerZ + radius);
    return bounds;
}

uint32_t addMaterial(RenderScene& scene, asset::MaterialType type = asset::MaterialType::Lit,
                     asset::AlphaMode alphaMode = asset::AlphaMode::Opaque, int renderQueue = 2000) {
    asset::Material material;
    material.name = "material_" + std::to_string(scene.materials.size());
    material.type = type;
    material.alphaMode = alphaMode;
    material.renderQueue = renderQueue;
    scene.materials.push_back(std::move(material));
    return static_cast<uint32_t>(scene.materials.size() - 1);
}

uint32_t addMesh(RenderScene& scene, uint32_t materialIndex, const Bounds& localBounds = makeBounds(0, 0, 0)) {
    asset::Mesh mesh;
    mesh.name = "mesh_" + std::to_string(scene.meshes.size());
    mesh.bounds = localBounds;
    asset::SubMesh subMesh;
    subMesh.materialIndex = materialIndex;
    subMesh.indexOffset = 0;
    subMesh.indexCount = 3;
    subMesh.bounds = localBounds;
    mesh.submeshes.push_back(subMesh);
    scene.meshes.push_back(std::move(mesh));
    return static_cast<uint32_t>(scene.meshes.size() - 1);
}

uint32_t addObject(RenderScene& scene, uint32_t meshIndex, const Vec3& position, int sortingLayer = 0,
                   int sortingOrder = 0, int rendererPriority = 0) {
    RenderObject object;
    object.name = "object_" + std::to_string(scene.objects.size());
    object.meshIndex = meshIndex;
    object.world.values[12] = position.x;
    object.world.values[13] = position.y;
    object.world.values[14] = position.z;
    object.worldBounds = transformBounds(object.world, scene.meshes[meshIndex].bounds);
    object.sortingLayer = sortingLayer;
    object.sortingOrder = sortingOrder;
    object.rendererPriority = rendererPriority;
    scene.objects.push_back(std::move(object));
    return static_cast<uint32_t>(scene.objects.size() - 1);
}

void buildViewState(ViewState& viewState, const Camera& camera) {
    viewState.update(camera, 1280, 720, 1.0f, false, false);
}

} // namespace

TEST_CASE("render queues follow HDRP priority segmentation") {
    CHECK(kRenderQueueBackground == 1000);
    CHECK(kRenderQueueOpaque == 2000);
    CHECK(kRenderQueueAlphaTest == 2450);
    CHECK(kRenderQueueOpaqueLast == 2500);
    CHECK(kRenderQueueAfterPostprocessOpaque == 2501);
    CHECK(kRenderQueuePreRefractionFirst == 2650);
    CHECK(kRenderQueuePreRefractionLast == 2850);
    CHECK(kRenderQueueTransparentFirst == 2900);
    CHECK(kRenderQueueTransparentLast == 3100);
    CHECK(kRenderQueueLowTransparentFirst == 3300);
    CHECK(kRenderQueueLowTransparentLast == 3500);
    CHECK(kRenderQueueAfterPostprocessTransparentFirst == 3600);
    CHECK(kRenderQueueAfterPostprocessTransparentLast == 3800);
    CHECK(kRenderQueueLineRenderingFirst == 3750);
    CHECK(kRenderQueueLineRenderingLast == 3950);
    CHECK(kRenderQueueOverlay == 4000);

    CHECK(renderQueueType(999) == RenderQueueType::Unknown);
    CHECK(renderQueueType(kRenderQueueBackground) == RenderQueueType::Background);
    CHECK(renderQueueType(kRenderQueueOpaque) == RenderQueueType::Opaque);
    CHECK(renderQueueType(kRenderQueueOpaqueLast) == RenderQueueType::Opaque);
    CHECK(renderQueueType(kRenderQueueAfterPostprocessOpaque) == RenderQueueType::AfterPostProcessOpaque);
    CHECK(renderQueueType(kRenderQueuePreRefraction) == RenderQueueType::PreRefraction);
    CHECK(renderQueueType(kRenderQueueTransparent) == RenderQueueType::Transparent);
    CHECK(renderQueueType(kRenderQueueLowTransparent) == RenderQueueType::LowTransparent);
    CHECK(renderQueueType(kRenderQueueAfterPostprocessTransparent) == RenderQueueType::AfterPostprocessTransparent);
    CHECK(renderQueueType(kRenderQueueLineRendering) == RenderQueueType::LineRendering);
    CHECK(renderQueueType(kRenderQueueOverlay) == RenderQueueType::Overlay);
    CHECK(renderQueueType(kRenderQueueOverlayLast) == RenderQueueType::Overlay);

    CHECK(kRenderQueueRangeAllOpaque.contains(kRenderQueueOpaque));
    CHECK_FALSE(kRenderQueueRangeAllOpaque.contains(kRenderQueueTransparent));
    CHECK(kRenderQueueRangeAllTransparentWithLowRes.contains(kRenderQueuePreRefractionFirst));
    CHECK(kRenderQueueRangeAllTransparentWithLowRes.contains(kRenderQueueLowTransparentLast));
    CHECK_FALSE(kRenderQueueRangeAllTransparent.contains(kRenderQueueLowTransparent));
    CHECK(kRenderQueueRangeOverlay.contains(kRenderQueueOverlay));
    CHECK(kRenderQueueRangeAll.contains(0));
    CHECK(kRenderQueueRangeAll.contains(kRenderQueueOverlayLast));

    CHECK(RenderQueueRange{2000, 3000}.clamp(1000) == 2000);
    CHECK(RenderQueueRange{2000, 3000}.clamp(2500) == 2500);
    CHECK(RenderQueueRange{2000, 3000}.clamp(4000) == 3000);
    CHECK(RenderQueueRange{3000, 2000}.empty());

    CHECK(isOpaqueQueue(RenderQueueType::Opaque));
    CHECK(isTransparentQueue(RenderQueueType::Transparent));
    CHECK_FALSE(isOpaqueQueue(RenderQueueType::Overlay));
    CHECK(isTransparentQueue(RenderQueueType::Overlay));
    CHECK(changeRenderQueueType(RenderQueueType::Opaque, 0, true, false) == kRenderQueueAlphaTest);
    CHECK(changeRenderQueueType(RenderQueueType::Transparent, -50) == kRenderQueueTransparent - 50);
}

TEST_CASE("shader pass tags follow HDRP material pass support") {
    CHECK(shaderPassName(ShaderPass::GBuffer) == "GBuffer");
    CHECK(shaderPassName(ShaderPass::TransparentDepthPrepass) == "TransparentDepthPrepass");
    CHECK(shaderPassName(ShaderPass::Count).empty());
    CHECK((kAllForwardOpaquePassMask & shaderPassMask(ShaderPass::SRPDefaultUnlit)) != 0);
    CHECK((kAllTransparentPassMask & shaderPassMask(ShaderPass::TransparentBackface)) != 0);

    asset::Material lit;
    const ShaderPassMask litMask = defaultShaderPassMask(lit);
    CHECK((litMask & shaderPassMask(ShaderPass::Forward)) != 0);
    CHECK((litMask & shaderPassMask(ShaderPass::GBuffer)) != 0);
    CHECK((litMask & shaderPassMask(ShaderPass::DepthOnly)) != 0);
    CHECK((litMask & shaderPassMask(ShaderPass::ShadowCaster)) != 0);
    CHECK((litMask & shaderPassMask(ShaderPass::MotionVectors)) != 0);
    CHECK((litMask & shaderPassMask(ShaderPass::ForwardOnly)) == 0);
    CHECK((litMask & shaderPassMask(ShaderPass::TransparentDepthPrepass)) == 0);

    asset::Material maskedLit;
    maskedLit.alphaMode = asset::AlphaMode::Mask;
    const ShaderPassMask maskedLitMask = defaultShaderPassMask(maskedLit);
    CHECK((maskedLitMask & shaderPassMask(ShaderPass::ShadowCaster)) != 0);
    CHECK((maskedLitMask & shaderPassMask(ShaderPass::Forward)) != 0);

    asset::Material transparentLit;
    transparentLit.alphaMode = asset::AlphaMode::Blend;
    const ShaderPassMask transparentLitMask = defaultShaderPassMask(transparentLit);
    CHECK((transparentLitMask & shaderPassMask(ShaderPass::Forward)) != 0);
    CHECK((transparentLitMask & shaderPassMask(ShaderPass::TransparentDepthPrepass)) != 0);
    CHECK((transparentLitMask & shaderPassMask(ShaderPass::TransparentBackface)) != 0);
    CHECK((transparentLitMask & shaderPassMask(ShaderPass::TransparentDepthPostpass)) != 0);
    CHECK((transparentLitMask & shaderPassMask(ShaderPass::GBuffer)) == 0);
    CHECK((transparentLitMask & shaderPassMask(ShaderPass::DepthOnly)) == 0);
    CHECK((transparentLitMask & shaderPassMask(ShaderPass::ShadowCaster)) == 0);

    asset::Material unlit;
    unlit.type = asset::MaterialType::Unlit;
    const ShaderPassMask unlitMask = defaultShaderPassMask(unlit);
    CHECK((unlitMask & shaderPassMask(ShaderPass::ForwardOnly)) != 0);
    CHECK((unlitMask & shaderPassMask(ShaderPass::DepthForwardOnly)) != 0);
    CHECK((unlitMask & shaderPassMask(ShaderPass::Forward)) == 0);
    CHECK((unlitMask & shaderPassMask(ShaderPass::GBuffer)) == 0);
}

TEST_CASE("draw list filters by pass tag and HDRP render queue") {
    RenderScene scene;
    const uint32_t litOpaque = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Opaque, 2000);
    const uint32_t litTransparent = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Blend, 3000);
    const uint32_t unlitOpaque = addMaterial(scene, asset::MaterialType::Unlit, asset::AlphaMode::Opaque, 2000);
    const uint32_t unlitTransparent = addMaterial(scene, asset::MaterialType::Unlit, asset::AlphaMode::Blend, 3000);
    const uint32_t opaqueMesh = addMesh(scene, litOpaque);
    const uint32_t transparentMesh = addMesh(scene, litTransparent);
    const uint32_t unlitOpaqueMesh = addMesh(scene, unlitOpaque);
    const uint32_t unlitTransparentMesh = addMesh(scene, unlitTransparent);
    addObject(scene, opaqueMesh, Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, transparentMesh, Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, unlitOpaqueMesh, Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, unlitTransparentMesh, Vec3(0.0f, 0.0f, 0.0f));

    ViewState viewState;
    buildViewState(viewState, makeCamera());

    DrawListDesc desc;
    desc.passTags = shaderPassMask(ShaderPass::GBuffer);
    desc.queueRange = kRenderQueueRangeAll;
    DrawList list;
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 1);
    CHECK(list.items()[0].materialIndex == litOpaque);
    CHECK(list.items()[0].indexOffset == 0);
    CHECK(list.items()[0].indexCount == 3);

    desc.passTags = shaderPassMask(ShaderPass::TransparentDepthPrepass);
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 1);
    CHECK(list.items()[0].materialIndex == litTransparent);

    desc.passTags = shaderPassMask(ShaderPass::ForwardOnly);
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 2);
    CHECK(list.items()[0].materialIndex == unlitOpaque);
    CHECK(list.items()[1].materialIndex == unlitTransparent);

    desc.passTags = kForwardPassMask;
    desc.queueRange = kRenderQueueRangeAllOpaque;
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 2);
    bool foundLitOpaque = false;
    for (const DrawItem& item : list.items()) {
        foundLitOpaque = foundLitOpaque || item.materialIndex == litOpaque;
    }
    CHECK(foundLitOpaque);

    desc.passTags = kForwardPassMask;
    desc.queueRange = kRenderQueueRangeAllTransparentWithLowRes;
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 2);
    CHECK(list.items()[0].transparent);
    CHECK(list.items()[1].transparent);

    desc.passTags = kShaderPassMaskAll;
    desc.queueRange = kRenderQueueRangeAll;
    desc.sortingCriteria = SortingCriteria::None;
    list.build(scene, viewState, desc);
    CHECK(list.size() == 4);
    CHECK(list.filteredCount() == 0);
}

TEST_CASE("draw list sorts opaque queues and groups compatible batches") {
    RenderScene scene;
    const uint32_t baseMaterial = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Opaque, 2000);
    const uint32_t alphaMaterial = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Mask, 2450);
    const uint32_t baseMesh = addMesh(scene, baseMaterial);
    const uint32_t alphaMesh = addMesh(scene, alphaMaterial);
    addObject(scene, baseMesh, Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, baseMesh, Vec3(2.0f, 0.0f, 0.0f));
    addObject(scene, baseMesh, Vec3(-2.0f, 0.0f, 0.0f));
    addObject(scene, alphaMesh, Vec3(0.0f, 0.0f, 0.0f));

    ViewState viewState;
    buildViewState(viewState, makeCamera());
    DrawListDesc desc;
    desc.passTags = kForwardPassMask;
    desc.queueRange = kRenderQueueRangeAllOpaque;
    desc.sortingCriteria = kOpaqueSortingCriteria;

    DrawList list;
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 4);
    CHECK(list.items()[0].materialIndex == baseMaterial);
    CHECK(list.items()[1].materialIndex == baseMaterial);
    CHECK(list.items()[2].materialIndex == baseMaterial);
    CHECK(list.items()[3].materialIndex == alphaMaterial);
    CHECK(list.items()[3].queueType == RenderQueueType::Opaque);
    REQUIRE(list.batches().size() == 2);
    CHECK(list.batches()[0].itemCount == 3);
    CHECK(list.batches()[1].itemCount == 1);
}

TEST_CASE("gbuffer draw list keeps lit opaque variants only") {
    RenderScene scene;
    const uint32_t opaque = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Opaque, 2000);
    const uint32_t mask = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Mask, 2450);
    const uint32_t doubleSided = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Opaque, 2000);
    scene.materials[doubleSided].doubleSided = true;
    const uint32_t unlit = addMaterial(scene, asset::MaterialType::Unlit, asset::AlphaMode::Opaque, 2000);
    const uint32_t transparent = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Blend, 3000);

    addObject(scene, addMesh(scene, opaque), Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, addMesh(scene, mask), Vec3(1.0f, 0.0f, 0.0f));
    addObject(scene, addMesh(scene, doubleSided), Vec3(2.0f, 0.0f, 0.0f));
    addObject(scene, addMesh(scene, unlit), Vec3(3.0f, 0.0f, 0.0f));
    addObject(scene, addMesh(scene, transparent), Vec3(4.0f, 0.0f, 0.0f));

    ViewState viewState;
    buildViewState(viewState, makeCamera());
    DrawListDesc desc;
    desc.passTags = shaderPassMask(ShaderPass::GBuffer);
    desc.queueRange = kRenderQueueRangeAllOpaque;
    desc.sortingCriteria = kOpaqueSortingCriteria;

    DrawList list;
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 3);
    CHECK(list.items()[0].materialIndex == opaque);
    CHECK(list.items()[1].materialIndex == doubleSided);
    CHECK(list.items()[2].materialIndex == mask);
    for (const DrawItem& item : list.items()) {
        CHECK((item.passMask & shaderPassMask(ShaderPass::GBuffer)) != 0);
        CHECK_FALSE(item.transparent);
    }
    CHECK(list.filteredCount() == 2);
}

TEST_CASE("forward-only opaque list keeps unlit opaque objects out of deferred gbuffer") {
    RenderScene scene;
    const uint32_t litOpaque = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Opaque, 2000);
    const uint32_t unlitOpaque = addMaterial(scene, asset::MaterialType::Unlit, asset::AlphaMode::Opaque, 2000);
    const uint32_t unlitTransparent = addMaterial(scene, asset::MaterialType::Unlit, asset::AlphaMode::Blend, 3000);
    addObject(scene, addMesh(scene, litOpaque), Vec3(-2.0f, 0.0f, 0.0f));
    addObject(scene, addMesh(scene, unlitOpaque), Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, addMesh(scene, unlitTransparent), Vec3(2.0f, 0.0f, 0.0f));

    ViewState viewState;
    buildViewState(viewState, makeCamera());
    DrawListDesc desc;
    desc.passTags = kForwardOnlyOpaquePassMask;
    desc.queueRange = kRenderQueueRangeAllOpaque;
    desc.sortingCriteria = kOpaqueSortingCriteria;

    DrawList list;
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 1);
    CHECK(list.items()[0].materialIndex == unlitOpaque);
    CHECK_FALSE(list.items()[0].transparent);
    CHECK(list.filteredCount() == 2);
}

TEST_CASE("draw list sorts transparent objects back to front") {
    RenderScene scene;
    const uint32_t material = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Blend, 3000);
    const uint32_t mesh = addMesh(scene, material);
    addObject(scene, mesh, Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, mesh, Vec3(0.0f, 0.0f, -5.0f));
    addObject(scene, mesh, Vec3(0.0f, 0.0f, 5.0f));

    ViewState viewState;
    buildViewState(viewState, makeCamera());
    DrawListDesc desc;
    desc.passTags = kForwardPassMask;
    desc.queueRange = kRenderQueueRangeAllTransparentWithLowRes;
    desc.sortingCriteria = kTransparentSortingCriteria;

    DrawList list;
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 3);
    CHECK(list.items()[0].sortKey.distance > list.items()[1].sortKey.distance);
    CHECK(list.items()[1].sortKey.distance > list.items()[2].sortKey.distance);
    CHECK(list.items()[0].objectIndex == 1);
    CHECK(list.items()[2].objectIndex == 2);
}

TEST_CASE("draw list applies sorting layer and renderer priority") {
    RenderScene scene;
    const uint32_t material = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Opaque, 2000);
    const uint32_t mesh = addMesh(scene, material);
    addObject(scene, mesh, Vec3(0.0f, 0.0f, 0.0f), 0, 0, 10);
    addObject(scene, mesh, Vec3(1.0f, 0.0f, 0.0f), 0, 0, -10);
    addObject(scene, mesh, Vec3(2.0f, 0.0f, 0.0f), 5, 0, 0);

    ViewState viewState;
    buildViewState(viewState, makeCamera());
    DrawListDesc desc;
    desc.passTags = kForwardPassMask;
    desc.queueRange = kRenderQueueRangeAllOpaque;
    desc.sortingCriteria = kOpaqueSortingCriteria | SortingCriteria::RendererPriority;

    DrawList list;
    list.build(scene, viewState, desc);
    REQUIRE(list.size() == 3);
    CHECK(list.items()[0].objectIndex == 0);
    CHECK(list.items()[1].objectIndex == 1);
    CHECK(list.items()[2].objectIndex == 2);
}

TEST_CASE("draw list culls objects outside the camera frustum") {
    RenderScene scene;
    const uint32_t material = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Opaque, 2000);
    const uint32_t mesh = addMesh(scene, material);
    addObject(scene, mesh, Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, mesh, Vec3(100.0f, 0.0f, 0.0f));

    ViewState viewState;
    buildViewState(viewState, makeCamera());
    DrawListDesc desc;
    desc.passTags = kForwardPassMask;
    desc.queueRange = kRenderQueueRangeAllOpaque;
    desc.frustumCull = true;

    DrawList list;
    list.build(scene, viewState, desc);
    CHECK(list.size() == 1);
    CHECK(list.culledCount() == 1);

    desc.frustumCull = false;
    list.build(scene, viewState, desc);
    CHECK(list.size() == 2);
    CHECK(list.culledCount() == 0);
}

TEST_CASE("draw list rebuilds batches after per-request culling") {
    RenderScene scene;
    const uint32_t material = addMaterial(scene, asset::MaterialType::Lit, asset::AlphaMode::Opaque, 2000);
    const uint32_t mesh = addMesh(scene, material);
    addObject(scene, mesh, Vec3(0.0f, 0.0f, 0.0f));
    addObject(scene, mesh, Vec3(-10.0f, 0.0f, 0.0f));

    ViewState viewState;
    buildViewState(viewState, makeCamera());
    DrawListDesc desc;
    desc.passTags = shaderPassMask(ShaderPass::ShadowCaster);
    desc.queueRange = kRenderQueueRangeAllOpaque;
    desc.frustumCull = false;

    DrawList source;
    source.build(scene, viewState, desc);
    REQUIRE(source.size() == 2);

    std::array<Vec4, 6> planes = {
        Vec4(1.0f, 0.0f, 0.0f, 0.0f),
        Vec4(0.0f, 0.0f, 0.0f, 100.0f),
        Vec4(0.0f, 0.0f, 0.0f, 100.0f),
        Vec4(0.0f, 0.0f, 0.0f, 100.0f),
        Vec4(0.0f, 0.0f, 0.0f, 100.0f),
        Vec4(0.0f, 0.0f, 0.0f, 100.0f),
    };
    DrawList culled;
    culled.buildCulled(source, planes);
    REQUIRE(culled.size() == 1);
    CHECK(culled.culledCount() == 1);
    CHECK(culled.items()[0].objectIndex == 0);
    REQUIRE(culled.batches().size() == 1);
    CHECK(culled.batches()[0].itemCount == 1);
}

TEST_CASE("draw distance quantization is monotonic") {
    CHECK(quantizeDrawDistance(-1.0f) == 0);
    CHECK(quantizeDrawDistance(0.0f) == 0);
    CHECK(quantizeDrawDistance(1.0f) < quantizeDrawDistance(2.0f));
    CHECK(quantizeDrawDistance(2.0f) < quantizeDrawDistance(10.0f));
}
