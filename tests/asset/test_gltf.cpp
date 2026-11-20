#include "asset/gltf_loader.h"
#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/scene_loader.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace hbg;
using namespace hbg::asset;

namespace {

constexpr const char* kPngDataUri =
    "data:image/png;base64,"
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=";

constexpr uint8_t kDecalPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c,
    0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0x0f, 0x00,
    0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xe3, 0x66, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
    0xae, 0x42, 0x60, 0x82,
};

void writeFile(const std::filesystem::path& path, const void* data, std::size_t size) {
    std::ofstream file(path, std::ios::binary);
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

std::filesystem::path makeFixtureDirectory() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "hbg_gltf_test";
    std::filesystem::create_directories(directory);
    return directory;
}

std::vector<uint8_t> makeTriangleBuffer() {
    std::vector<float> values = {
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
    };
    std::vector<uint8_t> bytes;
    bytes.resize(values.size() * sizeof(float));
    std::memcpy(bytes.data(), values.data(), bytes.size());
    const std::array<uint16_t, 3> indices = {0, 1, 2};
    const std::size_t indexOffset = bytes.size();
    bytes.resize(bytes.size() + sizeof(indices));
    std::memcpy(bytes.data() + indexOffset, indices.data(), sizeof(indices));
    return bytes;
}

std::string makeGltf(const std::string& imageUri, bool withUv, bool withNormals = true) {
    std::string attributes = R"("POSITION":0)";
    if (withNormals) {
        attributes += R"(,"NORMAL":1)";
    }
    if (withUv) {
        attributes += R"(,"TEXCOORD_0":2)";
    }
    const std::string texture = withUv ? R"(,"baseColorTexture":{"index":0})" : "";
    const std::string images = withUv ? R"(,"images":[{"uri":")" + imageUri + R"("}],"textures":[{"source":0}])" : "";
    const std::string material = R"({
        "name":"MatA",
        "alphaMode":"MASK",
        "alphaCutoff":0.25,
        "doubleSided":true,
        "pbrMetallicRoughness":{
            "baseColorFactor":[0.8,0.2,0.1,1.0],
            "metallicFactor":0.1,
            "roughnessFactor":0.7)" +
                                 texture + R"(
        }
    })";
    const std::string meshPrimitives = R"([
        {"attributes":{)" + attributes +
                                       R"(},"indices":3,"material":0},
        {"attributes":{)" + attributes +
                                       R"(},"indices":3,"material":1}
    ])";
    return R"({
        "asset":{"version":"2.0"},
        "extensionsUsed":["KHR_lights_punctual"],
        "buffers":[{"uri":"triangle.bin","byteLength":102}],
        "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":36},
            {"buffer":0,"byteOffset":72,"byteLength":24},
            {"buffer":0,"byteOffset":96,"byteLength":6}
        ],
        "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
            {"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
            {"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}
        ],
        "materials":[)" +
           material + R"(,)" + material + R"(],
        "meshes":[{"name":"Triangle","primitives":)" +
           meshPrimitives + R"(}],
        "cameras":[{"type":"perspective","perspective":{"yfov":0.7,"znear":0.1,"zfar":100.0}}],
        "extensions":{"KHR_lights_punctual":{"lights":[{"type":"point","color":[1.0,0.5,0.25],"intensity":5.0,"range":10.0}]}},
        "nodes":[
            {"name":"MeshNode","mesh":0,"translation":[1.0,2.0,3.0]},
            {"name":"CameraNode","camera":0,"translation":[0.0,1.0,5.0]},
            {"name":"LightNode","translation":[2.0,3.0,4.0],"extensions":{"KHR_lights_punctual":{"light":0}}},
            {"name":"Root","translation":[10.0,0.0,0.0],"children":[0]}
        ],
        "scenes":[{"nodes":[3,1,2]}],
        "scene":0
        )" +
           images + R"(
    })";
}

} // namespace

TEST_CASE("glTF loader reads mesh material camera and light") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle.bin";
    const std::filesystem::path gltfPath = directory / "triangle.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "hdrp":{"renderQueue":2451,"features":{"clearCoat":true,"subsurfaceScattering":true},"clearCoat":0.4,"clearCoatRoughness":0.12,"transmission":0.3,"thickness":0.5,"ior":1.4,"iridescence":0.2,"anisotropy":0.6,"anisotropyRotation":0.25,"subsurfaceMask":0.7},
        "lights":[{
            "name":"LightNode",
            "shadow":{
                "dimmer":0.4,
                "fadeDistance":42.0,
                "nearPlane":0.25,
                "resolution":1024,
                "tint":[0.1,0.2,0.3],
                "penumbraTint":true,
                "updateMode":"onEnable",
                "contactShadows":false,
                "softness":0.8,
                "blockerSamples":12,
                "filterSamples":20
            }
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;

    REQUIRE(scene.meshes.size() == 1);
    const Mesh& mesh = scene.meshes[0];
    CHECK(mesh.vertices.size() == 6);
    CHECK(mesh.indices.size() == 6);
    CHECK(mesh.submeshes.size() == 2);
    CHECK(mesh.submeshes[0].indexCount == 3);
    CHECK(mesh.submeshes[1].indexCount == 3);
    CHECK(mesh.hasNormals);
    CHECK(mesh.hasTangents);
    CHECK(mesh.hasUv0);
    CHECK(mesh.submeshes[0].materialIndex == 0);
    CHECK(mesh.submeshes[1].materialIndex == 0);

    bool nonZeroTangent = false;
    for (const MeshVertex& vertex : mesh.vertices) {
        if (vertex.tangent.x != 0.0f || vertex.tangent.y != 0.0f || vertex.tangent.z != 0.0f) {
            nonZeroTangent = true;
        }
    }
    CHECK(nonZeroTangent);

    REQUIRE(scene.materials.size() == 1);
    const Material& material = scene.materials[0];
    CHECK(material.name == "MatA");
    CHECK(material.alphaMode == AlphaMode::Mask);
    CHECK(material.renderQueue == 2451);
    CHECK((material.features & MaterialFeatureClearCoat) != 0);
    CHECK((material.features & MaterialFeatureSubsurface) != 0);
    CHECK(material.clearCoat == doctest::Approx(0.4f));
    CHECK(material.clearCoatRoughness == doctest::Approx(0.12f));
    CHECK(material.transmission == doctest::Approx(0.3f));
    CHECK(material.thickness == doctest::Approx(0.5f));
    CHECK(material.ior == doctest::Approx(1.4f));
    CHECK(material.iridescence == doctest::Approx(0.2f));
    CHECK(material.anisotropy == doctest::Approx(0.6f));
    CHECK(material.anisotropyRotation == doctest::Approx(0.25f));
    CHECK(material.subsurfaceMask == doctest::Approx(0.7f));
    CHECK(material.baseColorFactor == Vec4(0.8f, 0.2f, 0.1f, 1.0f));
    CHECK(material.metallic == doctest::Approx(0.1f));
    CHECK(material.roughness == doctest::Approx(0.7f));
    CHECK(material.doubleSided);

    REQUIRE(scene.images.size() == 1);
    CHECK(scene.images[0].width == 1);
    CHECK(scene.images[0].height == 1);
    CHECK(scene.images[0].format == gfx::TextureFormat::RGBA8);

    REQUIRE(scene.objects.size() == 1);
    CHECK(scene.objects[0].world[12] == doctest::Approx(11.0f));
    CHECK(scene.objects[0].world[13] == doctest::Approx(2.0f));
    CHECK(scene.objects[0].world[14] == doctest::Approx(3.0f));
    CHECK(scene.objects[0].worldBounds.min.x == doctest::Approx(11.0f));
    CHECK(scene.objects[0].worldBounds.max.x == doctest::Approx(12.0f));

    REQUIRE(scene.cameras.size() == 1);
    CHECK(scene.cameras[0].projection == render::Camera::Projection::Perspective);
    CHECK(scene.cameras[0].yfov == doctest::Approx(0.7f));

    REQUIRE(scene.lights.size() == 1);
    CHECK(scene.lights[0].type == render::LightType::Point);
    CHECK(scene.lights[0].intensity == doctest::Approx(5.0f));
    CHECK(scene.lights[0].range == doctest::Approx(10.0f));
    CHECK(scene.lights[0].castShadows);
    CHECK(scene.lights[0].shadowConfigured);
    CHECK(scene.lights[0].shadow.dimmer == doctest::Approx(0.4f));
    CHECK(scene.lights[0].shadow.fadeDistance == doctest::Approx(42.0f));
    CHECK(scene.lights[0].shadow.nearPlane == doctest::Approx(0.25f));
    CHECK(scene.lights[0].shadow.resolution == 1024);
    CHECK(scene.lights[0].shadow.tint == Vec3(0.1f, 0.2f, 0.3f));
    CHECK(scene.lights[0].shadow.penumbraTint);
    CHECK(scene.lights[0].shadow.updateMode == render::ShadowUpdateMode::OnEnable);
    CHECK_FALSE(scene.lights[0].shadow.contactShadows);
    CHECK(scene.lights[0].shadow.softness == doctest::Approx(0.8f));
    CHECK(scene.lights[0].shadow.blockerSamples == 12);
    CHECK(scene.lights[0].shadow.filterSamples == 20);
}

TEST_CASE("scene sidecar converts a punctual light to an area light") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_area.bin";
    const std::filesystem::path gltfPath = directory / "triangle_area.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_area.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lights":[{
            "name":"LightNode",
            "type":"area",
            "shape":"rectangle",
            "width":2.0,
            "height":1.0,
            "range":8.0,
            "areaShadowCone":0.6,
            "cookieScale":[2.0,3.0],
            "cookieOffset":[0.25,0.5]
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.lights.size() == 1);
    const render::Light& light = scene.lights[0];
    CHECK(light.type == render::LightType::Area);
    CHECK(light.areaShape == render::LightAreaShape::Rectangle);
    CHECK(light.areaWidth == doctest::Approx(2.0f));
    CHECK(light.areaHeight == doctest::Approx(1.0f));
    CHECK(light.areaShadowCone == doctest::Approx(0.6f));
    CHECK_FALSE(light.castShadows);
    CHECK(light.cookieScale == Vec2(2.0f, 3.0f));
    CHECK(light.cookieOffset == Vec2(0.25f, 0.5f));
}

TEST_CASE("scene sidecar enables rectangle area shadows only when shadow is authored") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_area_shadow.bin";
    const std::filesystem::path gltfPath = directory / "triangle_area_shadow.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_area_shadow.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lights":[{
            "name":"LightNode",
            "type":"area",
            "shape":"rectangle",
            "width":2.0,
            "height":1.0,
            "range":8.0,
            "shadow":{"resolution":256,"softness":0.4}
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.lights.size() == 1);
    const render::Light& light = scene.lights[0];
    CHECK(light.type == render::LightType::Area);
    CHECK(light.areaShape == render::LightAreaShape::Rectangle);
    CHECK(light.castShadows);
    CHECK(light.shadowConfigured);
    CHECK(light.shadow.resolution == 256);
    CHECK(light.areaShadowCone == doctest::Approx(120.0f));
}

TEST_CASE("scene sidecar loads a tube area light as a line") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_tube.bin";
    const std::filesystem::path gltfPath = directory / "triangle_tube.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_tube.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lights":[{
            "name":"LightNode",
            "type":"area",
            "shape":"tube",
            "width":3.0,
            "height":2.0,
            "intensity":7.0,
            "range":9.0
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    REQUIRE(result.scene->lights.size() == 1);
    const render::Light& light = result.scene->lights[0];
    CHECK(light.type == render::LightType::Area);
    CHECK(light.areaShape == render::LightAreaShape::Tube);
    CHECK(light.areaWidth == doctest::Approx(3.0f));
    CHECK(light.areaHeight == doctest::Approx(0.0f));
    CHECK(light.intensity == doctest::Approx(7.0f));
}

TEST_CASE("scene sidecar disables an unsupported disc area light") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_disc.bin";
    const std::filesystem::path gltfPath = directory / "triangle_disc.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_disc.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    const std::string json = makeGltf(kPngDataUri, true);
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lights":[{
            "name":"LightNode",
            "type":"area",
            "shape":"disc",
            "width":4.0,
            "height":4.0,
            "intensity":7.0,
            "range":9.0
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    REQUIRE(result.scene->lights.size() == 1);
    const render::Light& light = result.scene->lights[0];
    CHECK(light.type == render::LightType::Area);
    CHECK(light.areaShape == render::LightAreaShape::Disc);
    CHECK(light.intensity == doctest::Approx(0.0f));
}

TEST_CASE("scene sidecar loads static decal data and textures") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_decals.bin";
    const std::filesystem::path gltfPath = directory / "triangle_decals.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_decals.hdrp.json";
    const std::filesystem::path imagePath = directory / "decal.png";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    writeFile(imagePath, kDecalPng, sizeof(kDecalPng));
    std::string json = makeGltf("", false, false);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_decals.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "decals": [{
            "name": "floor",
            "texture": "decal.png",
            "normalTexture": "decal.png",
            "maskTexture": "decal.png",
            "position": [1.0, 2.0, 3.0],
            "rotation": [0.0, 90.0, 0.0],
            "size": [2.0, 4.0, 6.0],
            "baseColor": [0.8, 0.7, 0.6, 0.5],
            "opacity": 0.75,
            "angleFade": 30.0,
            "normalBlend": 0.5,
            "metallic": 0.2,
            "ao": 0.8,
            "roughness": 0.4,
            "layerMask": 5,
            "channelMask": 17
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 3);
    REQUIRE(scene.decals.size() == 1);
    const render::Decal& decal = scene.decals[0];
    CHECK(decal.name == "floor");
    CHECK(decal.albedoImage == 0);
    CHECK(decal.normalImage == 1);
    CHECK(decal.maskImage == 2);
    CHECK(decal.position == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(decal.rotation == Vec3(0.0f, 90.0f, 0.0f));
    CHECK(decal.size == Vec3(2.0f, 4.0f, 6.0f));
    CHECK(decal.baseColor == Vec4(0.8f, 0.7f, 0.6f, 0.5f));
    CHECK(decal.opacity == doctest::Approx(0.75f));
    CHECK(decal.angleFade == doctest::Approx(30.0f));
    CHECK(decal.normalBlend == doctest::Approx(0.5f));
    CHECK(decal.metallic == doctest::Approx(0.2f));
    CHECK(decal.ambientOcclusion == doctest::Approx(0.8f));
    CHECK(decal.roughness == doctest::Approx(0.4f));
    CHECK(decal.layerMask == 5u);
    CHECK(decal.channelMask == 17u);
    CHECK(decal.enabled);
}

TEST_CASE("scene sidecar loads static reflection probe data and textures") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_probes.bin";
    const std::filesystem::path gltfPath = directory / "triangle_probes.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_probes.hdrp.json";
    const std::filesystem::path imagePath = directory / "probe.png";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    writeFile(imagePath, kDecalPng, sizeof(kDecalPng));
    std::string json = makeGltf("", false, false);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_probes.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "probes": [{
            "name": "room",
            "texture": "probe.png",
            "position": [1.0, 2.0, 3.0],
            "rotation": [0.0, 90.0, 0.0],
            "boxExtents": [2.0, 3.0, 4.0],
            "blendDistance": 1.5,
            "intensity": 0.75
        }, {
            "texture": "probe.png",
            "enabled": false
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 2);
    REQUIRE(scene.probes.size() == 2);
    const render::ReflectionProbe& probe = scene.probes[0];
    CHECK(probe.name == "room");
    CHECK(probe.image == 0);
    CHECK(probe.position == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(probe.rotation == Vec3(0.0f, 90.0f, 0.0f));
    CHECK(probe.boxExtents == Vec3(2.0f, 3.0f, 4.0f));
    CHECK(probe.blendDistance == doctest::Approx(1.5f));
    CHECK(probe.intensity == doctest::Approx(0.75f));
    CHECK(probe.enabled);
    CHECK(scene.probes[1].image == 1);
    CHECK_FALSE(scene.probes[1].enabled);
}

TEST_CASE("scene sidecar loads local fog volumes and density masks") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_local_fog.bin";
    const std::filesystem::path gltfPath = directory / "triangle_local_fog.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_local_fog.hdrp.json";
    const std::filesystem::path imagePath = directory / "fog_mask.png";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    writeFile(imagePath, kDecalPng, sizeof(kDecalPng));
    std::string json = makeGltf("", false, false);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_local_fog.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "localFogVolumes": [{
            "name": "mist",
            "shape": "box",
            "position": [1.0, 2.0, 3.0],
            "rotation": [0.0, 90.0, 0.0],
            "size": [4.0, 2.0, 6.0],
            "albedo": [0.8, 0.7, 0.6],
            "meanFreePath": 12.0,
            "anisotropy": 0.25,
            "positiveFade": [0.5, 0.75, 1.0],
            "negativeFade": [0.25, 0.5, 0.75],
            "invertFade": true,
            "falloffMode": "exponential",
            "distanceFadeStart": 5.0,
            "distanceFadeEnd": 40.0,
            "maskTexture": "fog_mask.png",
            "maskPlane": "yz",
            "maskTiling": [2.0, 3.0, 1.0],
            "maskScroll": [0.1, 0.2, 0.0],
            "maskIntensity": 0.75,
            "maskInvert": true
        }, {
            "name": "sphere",
            "shape": "sphere",
            "position": [0.0, 1.0, 0.0],
            "size": [2.0, 2.0, 2.0],
            "enabled": false
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.images.size() == 1);
    REQUIRE(scene.localFogVolumes.size() == 2);
    const render::LocalFogVolume& volume = scene.localFogVolumes[0];
    CHECK(volume.name == "mist");
    CHECK(volume.shape == render::LocalFogShape::Box);
    CHECK(volume.maskImage == 0);
    CHECK(volume.center == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(volume.rotation == Vec3(0.0f, 90.0f, 0.0f));
    CHECK(volume.size == Vec3(4.0f, 2.0f, 6.0f));
    CHECK(volume.albedo == Vec3(0.8f, 0.7f, 0.6f));
    CHECK(volume.meanFreePath == doctest::Approx(12.0f));
    CHECK(volume.anisotropy == doctest::Approx(0.25f));
    CHECK(volume.positiveFade == Vec3(0.5f, 0.75f, 1.0f));
    CHECK(volume.negativeFade == Vec3(0.25f, 0.5f, 0.75f));
    CHECK(volume.invertFade);
    CHECK(volume.falloff == render::LocalFogFalloff::Exponential);
    CHECK(volume.distanceFadeStart == doctest::Approx(5.0f));
    CHECK(volume.distanceFadeEnd == doctest::Approx(40.0f));
    CHECK(volume.maskPlane == render::LocalFogMaskPlane::YZ);
    CHECK(volume.maskTiling == Vec3(2.0f, 3.0f, 1.0f));
    CHECK(volume.maskScroll == Vec3(0.1f, 0.2f, 0.0f));
    CHECK(volume.maskIntensity == doctest::Approx(0.75f));
    CHECK(volume.maskInvert);

    CHECK(scene.localFogVolumes[1].shape == render::LocalFogShape::Sphere);
    CHECK_FALSE(scene.localFogVolumes[1].enabled);
    CHECK(scene.localFogVolumes[1].maskImage == -1);
}

TEST_CASE("scene sidecar binds lightmap atlas rect and ST") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_lightmap.bin";
    const std::filesystem::path gltfPath = directory / "triangle_lightmap.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_lightmap.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf(kPngDataUri, true);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_lightmap.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lightmap": {
            "version": 1,
            "texelPerUnit": 6.0,
            "atlases": [{"file":"lm0","size":64,"layers":3,"format":"rgba16f"}],
            "objects": {
                "MeshNode": {"atlas":0,"rect":[2,2,32,32],"contributeGI":true}
            }
        }
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.lightmapAtlases.size() == 1);
    CHECK(scene.lightmapAtlases[0].file == "lm0");
    CHECK(scene.lightmapAtlases[0].size == 64);
    CHECK(scene.lightmapBaseDirectory == directory);
    REQUIRE(scene.objects.size() == 1);
    const render::RenderObject& object = scene.objects[0];
    CHECK(object.hasLightmap());
    CHECK(object.lightmapAtlas == 0u);
    CHECK(object.lightmapSize == Vec2(32.0f, 32.0f));
    CHECK(object.lightmapST.x == doctest::Approx(0.5f));
    CHECK(object.lightmapST.y == doctest::Approx(0.5f));
    CHECK(object.lightmapST.z == doctest::Approx(0.03125f));
    CHECK(object.lightmapST.w == doctest::Approx(0.03125f));
}

TEST_CASE("scene sidecar rejects an out-of-bounds lightmap rect") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_lightmap_bad.bin";
    const std::filesystem::path gltfPath = directory / "triangle_lightmap_bad.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_lightmap_bad.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf(kPngDataUri, true);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_lightmap_bad.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "lightmap": {
            "version": 1,
            "texelPerUnit": 6.0,
            "atlases": [{"file":"lm0","size":64,"layers":3,"format":"rgba16f"}],
            "objects": {"MeshNode":{"atlas":0,"rect":[63,0,4,4]}}
        }
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    CHECK_FALSE(result.ok());
    CHECK(result.error.find("outside its atlas") != std::string::npos);
}

TEST_CASE("glTF loader handles primitives without uv or normals") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_no_uv.bin";
    const std::filesystem::path gltfPath = directory / "triangle_no_uv.gltf";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf("", false, false);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_no_uv.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;

    REQUIRE(scene.meshes.size() == 1);
    CHECK_FALSE(scene.meshes[0].hasUv0);
    CHECK_FALSE(scene.meshes[0].hasTangents);
    CHECK(scene.meshes[0].hasNormals);
}

TEST_CASE("scene sidecar loads VFX instances and properties") {
    const std::filesystem::path directory = makeFixtureDirectory();
    const std::filesystem::path bufferPath = directory / "triangle_vfx.bin";
    const std::filesystem::path gltfPath = directory / "triangle_vfx.gltf";
    const std::filesystem::path sidecarPath = directory / "triangle_vfx.hdrp.json";

    const std::vector<uint8_t> buffer = makeTriangleBuffer();
    writeFile(bufferPath, buffer.data(), buffer.size());
    std::string json = makeGltf(kPngDataUri, true);
    const std::size_t uri = json.find("\"uri\":\"triangle.bin\"");
    REQUIRE(uri != std::string::npos);
    json.replace(uri, std::string("\"uri\":\"triangle.bin\"").size(), "\"uri\":\"triangle_vfx.bin\"");
    writeFile(gltfPath, json.data(), json.size());

    const std::string sidecar = R"({
        "vfx": [{
            "name": "Sparks",
            "asset": "effects/spark.vfxb",
            "position": [1.0, 2.0, 3.0],
            "rotation": [0.0, 90.0, 0.0],
            "scale": [2.0, 2.0, 2.0],
            "play": false,
            "playRate": 0.5,
            "seed": 42,
            "properties": [
                {"name": "Tint", "type": "color", "value": [1.0, 0.0, 0.0, 1.0]},
                {"name": "Size", "type": "float", "value": 0.3},
                {"name": "Direction", "type": "vector3", "value": [0.0, 1.0, 0.0]}
            ]
        }]
    })";
    writeFile(sidecarPath, sidecar.data(), sidecar.size());

    const SceneLoadResult result = SceneLoader::load(gltfPath);
    REQUIRE_MESSAGE(result.ok(), result.error);
    const render::RenderScene& scene = *result.scene;
    REQUIRE(scene.vfxInstances.size() == 1u);
    const render::VfxInstance& instance = scene.vfxInstances.front();
    CHECK(instance.name == "Sparks");
    CHECK(instance.asset == directory / "effects" / "spark.vfxb");
    CHECK(instance.position.x == doctest::Approx(1.0f));
    CHECK(instance.position.y == doctest::Approx(2.0f));
    CHECK(instance.position.z == doctest::Approx(3.0f));
    CHECK(instance.rotation.y == doctest::Approx(90.0f));
    CHECK(instance.scale.x == doctest::Approx(2.0f));
    CHECK_FALSE(instance.playOnAwake);
    CHECK(instance.playRate == doctest::Approx(0.5f));
    CHECK(instance.randomSeed == 42u);
    REQUIRE(instance.properties.size() == 3u);
    CHECK(instance.properties[0].name == "Tint");
    CHECK(instance.properties[0].kind == render::VfxPropertyKind::Color);
    CHECK(instance.properties[0].values[0] == doctest::Approx(1.0f));
    CHECK(instance.properties[1].kind == render::VfxPropertyKind::Float);
    CHECK(instance.properties[1].values[0] == doctest::Approx(0.3f));
    CHECK(instance.properties[2].kind == render::VfxPropertyKind::Vector3);
}
