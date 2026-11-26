#include "asset/scene_loader.h"
#include "render/material/shader_pass.h"
#include "render/shadergraph/graph_binding.h"
#include "render/shadergraph/sgb_asset.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path assetPath(const char* name) {
    return std::filesystem::path(HBG_PROJECT_ROOT) / "tests/shadergraph/assets" / name;
}

} // namespace

TEST_CASE("sgb runtime parses a lit graph asset") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("smoke_lit.sgb"), &error));
    CHECK(error.empty());
    CHECK(asset.target() == "lit");
    CHECK(asset.stageBudget() == 5);
    CHECK(asset.requiredFeatureMask() == 0u);
    CHECK(asset.contentHash() != 0u);
    CHECK(asset.sourceHash() != 0u);

    REQUIRE(asset.properties().size() == 2);
    const auto* mainTex = asset.findProperty("_MainTex");
    REQUIRE(mainTex != nullptr);
    CHECK(mainTex->kind == hbg::render::graph::PropertyKind::Texture2D);
    CHECK(mainTex->samplerStage == 0);
    const auto* tint = asset.findProperty("_Tint");
    REQUIRE(tint != nullptr);
    CHECK(tint->kind == hbg::render::graph::PropertyKind::Color);
    CHECK(tint->vector[0] == 1.0f);
    CHECK(tint->vector[3] == 1.0f);

    REQUIRE(asset.passes().size() == 4);
    CHECK(asset.findPass(hbg::render::graph::PassKind::Forward) != nullptr);
    CHECK(asset.findPass(hbg::render::graph::PassKind::GBuffer) != nullptr);
    CHECK(asset.findPass(hbg::render::graph::PassKind::Depth) != nullptr);
    CHECK(asset.findPass(hbg::render::graph::PassKind::Shadow) != nullptr);
    CHECK(asset.findPass(hbg::render::graph::PassKind::Motion) == nullptr);

    REQUIRE(asset.variants().size() == 1);
    CHECK(asset.variants().front().key == 0u);
    CHECK(asset.variants().front().groups.size() == 4);

    CHECK(hbg::render::graph::validateStageContract(asset, &error));
    CHECK(error.empty());

    const auto* forward = asset.findPass(hbg::render::graph::PassKind::Forward);
    REQUIRE(forward != nullptr);
    const hbg::render::graph::ShaderBlob vertex =
        asset.blob(forward->index, 0, hbg::render::graph::Backend::Dxbc, 0);
    CHECK(vertex.data != nullptr);
    CHECK(vertex.size > 4u);
    const hbg::render::graph::ShaderBlob fragment =
        asset.blob(forward->index, 0, hbg::render::graph::Backend::Spirv, 1);
    CHECK(fragment.data != nullptr);
    CHECK(fragment.size > 4u);
}

TEST_CASE("sgb runtime parses a property node asset") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("property_float.sgb"), &error));
    CHECK(error.empty());
    REQUIRE(asset.properties().size() == 1);
    const auto* amount = asset.findProperty("_Amount");
    REQUIRE(amount != nullptr);
    CHECK(amount->name == "Amount");
    CHECK(amount->kind == hbg::render::graph::PropertyKind::Float);
    CHECK(amount->vector[0] == 0.5f);
    CHECK(hbg::render::graph::validateStageContract(asset, &error));
    CHECK(error.empty());
}

TEST_CASE("sgb runtime accepts scene access feature mask") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("scene_access.sgb"), &error));
    CHECK(error.empty());
    CHECK(asset.requiredFeatureMask() ==
          (hbg::render::graph::kGraphFeatureSceneColor |
           hbg::render::graph::kGraphFeatureSceneDepth |
           hbg::render::graph::kGraphFeatureFog));
    CHECK(hbg::render::graph::validateStageContract(asset, &error));
    CHECK(error.empty());
}

TEST_CASE("sgb runtime parses sampler state overrides") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("sampler_state.sgb"), &error));
    CHECK(error.empty());
    const auto* mainTex = asset.findProperty("_MainTex");
    REQUIRE(mainTex != nullptr);
    CHECK(mainTex->kind == hbg::render::graph::PropertyKind::Texture2D);
    CHECK(mainTex->samplerFilter == 1);
    CHECK(mainTex->samplerWrap == 1);
    CHECK(mainTex->samplerAnisotropy == 8);
}

TEST_CASE("sgb runtime parses texture transform defaults and material overrides") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("texture_transform.sgb"), &error));
    CHECK(error.empty());
    const auto* mainTex = asset.findProperty("_MainTex");
    REQUIRE(mainTex != nullptr);
    CHECK(mainTex->kind == hbg::render::graph::PropertyKind::Texture2D);
    CHECK(mainTex->useTilingAndOffset);
    CHECK(mainTex->tiling[0] == doctest::Approx(2.0f));
    CHECK(mainTex->tiling[1] == doctest::Approx(3.0f));
    CHECK(mainTex->offset[0] == doctest::Approx(0.25f));
    CHECK(mainTex->offset[1] == doctest::Approx(0.5f));

    hbg::asset::Material material;
    const hbg::Vec4 defaults = hbg::render::graph::graphTextureTransform(*mainTex, material);
    CHECK(defaults.x == doctest::Approx(2.0f));
    CHECK(defaults.y == doctest::Approx(3.0f));
    CHECK(defaults.z == doctest::Approx(0.25f));
    CHECK(defaults.w == doctest::Approx(0.5f));

    hbg::asset::GraphPropertyOverride transform;
    transform.refName = "_MainTex_ST";
    transform.count = 4;
    transform.values = {4.0f, 5.0f, 0.125f, 0.75f};
    material.graphProperties.push_back(transform);
    const hbg::Vec4 overridden = hbg::render::graph::graphTextureTransform(*mainTex, material);
    CHECK(overridden.x == doctest::Approx(4.0f));
    CHECK(overridden.y == doctest::Approx(5.0f));
    CHECK(overridden.z == doctest::Approx(0.125f));
    CHECK(overridden.w == doctest::Approx(0.75f));
}

TEST_CASE("sgb runtime parses advanced texture property kinds") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("texture_advanced.sgb"), &error));
    CHECK(error.empty());
    const auto* array = asset.findProperty("_ArrayTex");
    const auto* volume = asset.findProperty("_VolumeTex");
    const auto* cube = asset.findProperty("_CubeTex");
    REQUIRE(array != nullptr);
    REQUIRE(volume != nullptr);
    REQUIRE(cube != nullptr);
    CHECK(array->kind == hbg::render::graph::PropertyKind::Texture2DArray);
    CHECK(volume->kind == hbg::render::graph::PropertyKind::Texture3D);
    CHECK(cube->kind == hbg::render::graph::PropertyKind::Cubemap);
}

TEST_CASE("sgb runtime parses texture default types") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("texture_defaults.sgb"), &error));
    CHECK(error.empty());

    const auto* white = asset.findProperty("_White");
    const auto* black = asset.findProperty("_Black");
    const auto* grey = asset.findProperty("_Grey");
    const auto* bump = asset.findProperty("_Bump");
    const auto* red = asset.findProperty("_Red");
    REQUIRE(white != nullptr);
    REQUIRE(black != nullptr);
    REQUIRE(grey != nullptr);
    REQUIRE(bump != nullptr);
    REQUIRE(red != nullptr);
    CHECK(white->defaultType == hbg::render::graph::TextureDefaultType::White);
    CHECK(black->defaultType == hbg::render::graph::TextureDefaultType::Black);
    CHECK(grey->defaultType == hbg::render::graph::TextureDefaultType::Grey);
    CHECK(bump->defaultType == hbg::render::graph::TextureDefaultType::Normal);
    CHECK(red->defaultType == hbg::render::graph::TextureDefaultType::Red);
}

TEST_CASE("sgb runtime parses virtual texture layer properties") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("virtual_texture.sgb"), &error));
    CHECK(error.empty());
    REQUIRE(asset.properties().size() == 2);

    const auto* layer0 = asset.findProperty("_VT_Layer0");
    const auto* layer1 = asset.findProperty("_VT_Layer1");
    REQUIRE(layer0 != nullptr);
    REQUIRE(layer1 != nullptr);
    CHECK(layer0->kind == hbg::render::graph::PropertyKind::Texture2D);
    CHECK(layer1->kind == hbg::render::graph::PropertyKind::Texture2D);
    CHECK(layer0->samplerStage == 0);
    CHECK(layer1->samplerStage == 1);
    CHECK(layer0->samplerWrap == 1);
    CHECK(layer1->samplerWrap == 1);
    CHECK(hbg::render::graph::validateStageContract(asset, &error));
    CHECK(error.empty());
}

TEST_CASE("sgb runtime parses custom virtual texture stack layers") {
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(assetPath("virtual_texture_stack.sgb"), &error));
    CHECK(error.empty());
    REQUIRE(asset.properties().size() == 3);

    const auto* base = asset.findProperty("_VTStack_Base");
    const auto* normal = asset.findProperty("_VTStack_Normal");
    const auto* data = asset.findProperty("_VTStack_Data");
    REQUIRE(base != nullptr);
    REQUIRE(normal != nullptr);
    REQUIRE(data != nullptr);
    CHECK(base->kind == hbg::render::graph::PropertyKind::Texture2D);
    CHECK(normal->kind == hbg::render::graph::PropertyKind::Texture2D);
    CHECK(data->kind == hbg::render::graph::PropertyKind::Texture2D);
    CHECK(base->samplerWrap == 1);
    CHECK(normal->samplerWrap == 1);
    CHECK(data->samplerWrap == 1);
    CHECK(hbg::render::graph::validateStageContract(asset, &error));
    CHECK(error.empty());
}

TEST_CASE("sgb runtime parses an asset from memory") {
    std::ifstream file(assetPath("smoke_lit.sgb"), std::ios::binary);
    REQUIRE(file.good());
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
    REQUIRE_FALSE(bytes.empty());

    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.loadFromMemory(bytes, &error));
    CHECK(error.empty());
    CHECK(asset.target() == "lit");
    CHECK(asset.passes().size() == 4);
    const hbg::render::graph::ShaderBlob vertex =
        asset.blob(0, 0, hbg::render::graph::Backend::Dxbc, 0);
    CHECK(vertex.data != nullptr);
    CHECK(vertex.size > 4u);
}

TEST_CASE("sgb runtime parses unlit passes and variants") {
    hbg::render::graph::Asset asset;
    REQUIRE(asset.load(assetPath("smoke_unlit.sgb")));
    CHECK(asset.target() == "unlit");
    CHECK(asset.passes().size() == 3);
    CHECK(asset.findPass(hbg::render::graph::PassKind::GBuffer) == nullptr);
    CHECK(asset.findPass(hbg::render::graph::PassKind::Forward) != nullptr);
    CHECK(asset.findPass(hbg::render::graph::PassKind::Depth) != nullptr);
    CHECK(asset.findPass(hbg::render::graph::PassKind::Shadow) != nullptr);
}

TEST_CASE("sgb runtime parses keyword metadata") {
    hbg::render::graph::Asset booleanAsset;
    REQUIRE(booleanAsset.load(assetPath("keyword_bool.sgb")));
    REQUIRE(booleanAsset.keywords().size() == 1);
    CHECK(booleanAsset.keywords().front().refName == "_MODE");
    CHECK(booleanAsset.keywords().front().entries.empty());

    hbg::render::graph::Asset enumAsset;
    REQUIRE(enumAsset.load(assetPath("keyword_enum.sgb")));
    REQUIRE(enumAsset.keywords().size() == 1);
    REQUIRE(enumAsset.keywords().front().entries.size() == 2);
    CHECK(enumAsset.keywords().front().entries[0].referenceName == "MODE_A");
    CHECK(enumAsset.keywords().front().entries[1].referenceName == "MODE_B");
    REQUIRE(enumAsset.variants().size() == 1);
    CHECK(enumAsset.variants().front().key == 0x100u);
}

TEST_CASE("sgb runtime rejects corrupted files") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbg_sgb_runtime_corrupt.sgb";
    {
        std::ofstream file(path, std::ios::binary);
        const std::array<char, 8> bad = {'N', 'O', 'T', 'S', 'G', 'B', '1', '!'};
        file.write(bad.data(), static_cast<std::streamsize>(bad.size()));
    }
    hbg::render::graph::Asset asset;
    std::string error;
    CHECK_FALSE(asset.load(path, &error));
    CHECK_FALSE(error.empty());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("sgb runtime parses every checked-in asset") {
    const std::filesystem::path directory =
        std::filesystem::path(HBG_PROJECT_ROOT) / "tests/shadergraph/assets";
    std::size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".sgb" || entry.path().filename().string() == "spike.sgb") {
            continue;
        }
        hbg::render::graph::Asset asset;
        std::string error;
        INFO(entry.path().string());
        REQUIRE(asset.load(entry.path(), &error));
        CHECK(error.empty());
        CHECK(asset.stageBudget() == 5);
        CHECK(hbg::render::graph::validateStageContract(asset, &error));
        CHECK(error.empty());
        ++count;
    }
    CHECK(count >= 32);
}

TEST_CASE("material sidecar applies graph overrides") {
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "hbg_sg3_sidecar_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory, ignored);
    std::filesystem::copy_file(assetPath("smoke_lit.sgb"), directory / "smoke_lit.sgb",
                               std::filesystem::copy_options::overwrite_existing, ignored);

    {
        const std::array<uint8_t, 58> bmp = {
            0x42, 0x4d, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
            0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
            0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x13, 0x0b, 0x00, 0x00,
            0x13, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x80,
            0x40, 0x00,
        };
        std::ofstream texture(directory / "tint.bmp", std::ios::binary);
        texture.write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
    }

    const std::filesystem::path sidecarPath = directory / "scene.hdrp.json";
    {
        std::ofstream sidecar(sidecarPath);
        sidecar << R"({
  "materials": {
    "GraphMat": {
      "shader": "smoke_lit.sgb",
      "materialType": "unlit",
      "alphaMode": "mask",
      "doubleSided": true,
      "propertyOverrides": {
        "_Tint": [0.25, 0.5, 0.75, 1.0],
        "_Amount": 2.5
      },
      "textureOverrides": {
        "_MainTex": ["tint.bmp", "tint.bmp"]
      },
      "keywordOverrides": {
        "_MODE": true
      }
    }
  }
})";
    }

    hbg::render::RenderScene scene;
    hbg::asset::Material material;
    material.name = "GraphMat";
    scene.materials.push_back(material);

    std::string error;
    REQUIRE(hbg::asset::SceneLoader::applySidecarToScene(scene, sidecarPath, &error));
    CHECK(error.empty());
    REQUIRE(scene.materials.size() == 1);
    const hbg::asset::Material& result = scene.materials.front();
    CHECK(result.hasGraphShader());
    CHECK(result.graphShader.filename() == "smoke_lit.sgb");
    CHECK(result.type == hbg::asset::MaterialType::Unlit);
    CHECK(result.alphaMode == hbg::asset::AlphaMode::Mask);
    CHECK(result.doubleSided);
    REQUIRE(result.graphProperties.size() == 2);
    const auto propertyNamed = [&](const char* name) {
        const auto found = std::find_if(result.graphProperties.begin(), result.graphProperties.end(),
                                        [&](const hbg::asset::GraphPropertyOverride& entry) {
                                            return entry.refName == name;
                                        });
        REQUIRE(found != result.graphProperties.end());
        return *found;
    };
    const hbg::asset::GraphPropertyOverride tint = propertyNamed("_Tint");
    CHECK(tint.count == 4);
    CHECK(tint.values[2] == 0.75f);
    const hbg::asset::GraphPropertyOverride amount = propertyNamed("_Amount");
    CHECK(amount.count == 1);
    CHECK(amount.values[0] == 2.5f);
    REQUIRE(result.graphTextures.size() == 1);
    CHECK(result.graphTextures.front().refName == "_MainTex");
    CHECK(result.graphTextures.front().texture.valid());
    CHECK(result.graphTextures.front().imageIndices.size() == 2);
    REQUIRE(scene.images.size() == 2);
    REQUIRE(result.graphKeywords.size() == 1);
    CHECK(result.graphKeywords.front().refName == "_MODE");
    CHECK(result.graphKeywords.front().value == "1");

    std::filesystem::remove_all(directory, ignored);
}

TEST_CASE("graph variant key follows engine and keyword overrides") {
    hbg::render::graph::Asset lit;
    REQUIRE(lit.load(assetPath("smoke_lit.sgb")));
    hbg::asset::Material material;
    CHECK(hbg::render::graph::graphVariantKey(lit, material) == 0u);
    material.alphaMode = hbg::asset::AlphaMode::Mask;
    CHECK(hbg::render::graph::graphVariantKey(lit, material) == 1u);
    material.doubleSided = true;
    CHECK(hbg::render::graph::graphVariantKey(lit, material) == 3u);
    material.doubleSided = false;
    material.alphaMode = hbg::asset::AlphaMode::Blend;
    CHECK(hbg::render::graph::graphVariantKey(lit, material) == 4u);

    hbg::render::graph::Asset booleanAsset;
    REQUIRE(booleanAsset.load(assetPath("keyword_bool.sgb")));
    hbg::asset::Material booleanMaterial;
    CHECK(hbg::render::graph::graphVariantKey(booleanAsset, booleanMaterial) == 0u);
    booleanMaterial.graphKeywords.push_back({"_MODE", "1"});
    CHECK(hbg::render::graph::graphVariantKey(booleanAsset, booleanMaterial) == 0x100u);

    hbg::render::graph::Asset enumAsset;
    REQUIRE(enumAsset.load(assetPath("keyword_enum.sgb")));
    hbg::asset::Material enumMaterial;
    CHECK(hbg::render::graph::graphVariantKey(enumAsset, enumMaterial) == 0x100u);
    enumMaterial.graphKeywords.push_back({"_MODE", "MODE_B"});
    CHECK(hbg::render::graph::graphVariantKey(enumAsset, enumMaterial) == 0x200u);
}

TEST_CASE("graph materials do not request motion vectors") {
    hbg::asset::Material lit;
    lit.graphShader = "graph.sgb";
    const hbg::render::ShaderPassMask litMask = hbg::render::defaultShaderPassMask(lit);
    CHECK((litMask & hbg::render::shaderPassMask(hbg::render::ShaderPass::Forward)) != 0u);
    CHECK((litMask & hbg::render::shaderPassMask(hbg::render::ShaderPass::MotionVectors)) == 0u);

    hbg::asset::Material unlit;
    unlit.graphShader = "graph.sgb";
    unlit.type = hbg::asset::MaterialType::Unlit;
    const hbg::render::ShaderPassMask unlitMask = hbg::render::defaultShaderPassMask(unlit);
    CHECK((unlitMask & hbg::render::shaderPassMask(hbg::render::ShaderPass::ForwardOnly)) != 0u);
    CHECK((unlitMask & hbg::render::shaderPassMask(hbg::render::ShaderPass::GBuffer)) == 0u);
    CHECK((unlitMask & hbg::render::shaderPassMask(hbg::render::ShaderPass::MotionVectors)) == 0u);
}

TEST_CASE("stage contract rejects graph stages outside the budget") {
    std::vector<uint8_t> bytes;
    {
        std::ifstream file(assetPath("smoke_lit.sgb"), std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    REQUIRE(bytes.size() > 64);
    const auto readU32 = [&](std::size_t offset) {
        return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8u) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16u) |
               (static_cast<uint32_t>(bytes[offset + 3]) << 24u);
    };
    const uint32_t sectionCount = readU32(24);
    std::size_t passesOffset = 0;
    for (uint32_t index = 0; index < sectionCount; ++index) {
        const std::size_t entry = 28 + static_cast<std::size_t>(index) * 20;
        if (readU32(entry) == 6) {
            passesOffset = static_cast<std::size_t>(bytes[entry + 4]) |
                           (static_cast<std::size_t>(bytes[entry + 5]) << 8u) |
                           (static_cast<std::size_t>(bytes[entry + 6]) << 16u) |
                           (static_cast<std::size_t>(bytes[entry + 7]) << 24u);
            break;
        }
    }
    REQUIRE(passesOffset != 0);
    const std::size_t firstStage = passesOffset + 4 + 12;
    REQUIRE(passesOffset + 20 < bytes.size());
    bytes[firstStage + 4] = 5;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbg_sgb_bad_stage.sgb";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    hbg::render::graph::Asset asset;
    std::string error;
    REQUIRE(asset.load(path, &error));
    CHECK_FALSE(hbg::render::graph::validateStageContract(asset, &error));
    CHECK_FALSE(error.empty());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
