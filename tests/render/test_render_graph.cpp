#include "render/graph/render_graph.h"

#include <doctest/doctest.h>

#include <string>

using namespace hbg;
using namespace hbg::render;

namespace {

gfx::TextureDesc transientColorDesc() {
    gfx::TextureDesc desc;
    desc.width = 64;
    desc.height = 32;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    return desc;
}

RenderGraphPassFunc noop() {
    return [](RenderGraphContext&) {};
}

} // namespace

TEST_CASE("render graph culls passes that do not feed a live output") {
    RenderGraph graph("culling");
    graph.beginFrame(64, 32, 0);

    const RenderGraphTextureHandle first = graph.createTexture(transientColorDesc(), "first");
    const RenderGraphTextureHandle second = graph.createTexture(transientColorDesc(), "second");
    const RenderGraphTextureHandle unused = graph.createTexture(transientColorDesc(), "unused");

    graph.addRasterPass("Producer").writeTexture(first).setRenderFunc(noop());
    graph.addRasterPass("Consumer").readTexture(first).writeTexture(second).setRenderFunc(noop());
    graph.addRasterPass("Presentation").readTexture(second).writeBackbuffer().setRenderFunc(noop());
    graph.addRasterPass("Dead writer").writeTexture(unused).setRenderFunc(noop());
    graph.addRasterPass("Dead read").readTexture(first).setRenderFunc(noop());

    REQUIRE(graph.compile());
    CHECK(graph.diagnostics().recordedPasses == 5);
    CHECK(graph.diagnostics().livePasses == 3);
    CHECK(graph.diagnostics().culledPasses == 2);
    CHECK(graph.diagnostics().transientTextures == 2);
    REQUIRE(graph.passViewIds().size() == 5);
    CHECK(graph.passViewIds()[0] == kRenderGraphViewBase);
    CHECK(graph.passViewIds()[1] == kRenderGraphViewBase + 1);
    CHECK(graph.passViewIds()[2] == kRenderGraphBackbufferView);
    CHECK(graph.passViewIds()[3] == 0);
    CHECK(graph.passViewIds()[4] == 0);
    REQUIRE(graph.passNames().size() == 5);
    CHECK(graph.passNames()[0] == "Producer");
    CHECK(graph.passNames()[2] == "Presentation");
    CHECK(kDebugUiView > kRenderGraphBackbufferView);
}

TEST_CASE("render graph keeps all writers of a live texture and attachment writes") {
    RenderGraph graph("writers");
    graph.beginFrame(64, 32, 1);

    const RenderGraphTextureHandle color = graph.createTexture(transientColorDesc(), "color");
    const RenderGraphTextureHandle depth = graph.createTexture(
        [] {
            gfx::TextureDesc desc;
            desc.width = 64;
            desc.height = 32;
            desc.format = gfx::TextureFormat::D24S8;
            desc.depthStencil = true;
            return desc;
        }(),
        "depth");

    graph.addRasterPass("Clear").setColorAttachment(color).setDepthAttachment(depth).setRenderFunc(noop());
    graph.addRasterPass("Opaque").setColorAttachment(color).setDepthAttachment(depth).setRenderFunc(noop());
    graph.addRasterPass("Transparent").setColorAttachment(color).setDepthAttachment(depth).setRenderFunc(noop());
    graph.addRasterPass("Tonemap").readTexture(color).writeBackbuffer().setRenderFunc(noop());

    REQUIRE(graph.compile());
    CHECK(graph.diagnostics().livePasses == 4);
    CHECK(graph.passViewIds()[0] == kRenderGraphViewBase);
    CHECK(graph.passViewIds()[1] == kRenderGraphViewBase + 1);
    CHECK(graph.passViewIds()[2] == kRenderGraphViewBase + 2);
    CHECK(graph.passViewIds()[3] == kRenderGraphBackbufferView);
}

TEST_CASE("render graph supports a four-target pass with an independent view size") {
    RenderGraph graph("gbuffer");
    graph.beginFrame(64, 32, 0);

    const RenderGraphTextureHandle color0 = graph.createTexture(transientColorDesc(), "color0");
    const RenderGraphTextureHandle color1 = graph.createTexture(transientColorDesc(), "color1");
    const RenderGraphTextureHandle color2 = graph.createTexture(transientColorDesc(), "color2");
    const RenderGraphTextureHandle color3 = graph.createTexture(transientColorDesc(), "color3");
    const RenderGraphTextureHandle depth = graph.createTexture(
        [] {
            gfx::TextureDesc desc;
            desc.width = 64;
            desc.height = 32;
            desc.format = gfx::TextureFormat::D24S8;
            desc.depthStencil = true;
            return desc;
        }(),
        "depth");

    graph.addRasterPass("GBuffer")
        .setColorAttachment(color0, 0)
        .setColorAttachment(color1, 1)
        .setColorAttachment(color2, 2)
        .setColorAttachment(color3, 3)
        .setDepthAttachment(depth)
        .setViewSize(32, 16)
        .allowCulling(false)
        .setRenderFunc(noop());
    graph.addRasterPass("Present").readTexture(color0).writeBackbuffer().setRenderFunc(noop());

    REQUIRE(graph.compile());
    CHECK(graph.diagnostics().livePasses == 2);
    CHECK(graph.lastError().empty());
}

TEST_CASE("render graph records a compute pass without a framebuffer") {
    RenderGraph graph("compute");
    graph.beginFrame(64, 32, 0);

    gfx::TextureDesc desc;
    desc.width = 128;
    desc.height = 1;
    desc.format = gfx::TextureFormat::R32U;
    desc.computeWrite = true;
    const RenderGraphTextureHandle histogram = graph.createTexture(desc, "histogram");

    graph.addComputePass("Histogram")
        .writeTexture(histogram)
        .allowCulling(false)
        .setRenderFunc([](RenderGraphContext& context) { CHECK_FALSE(context.frameBuffer().valid()); });
    graph.addRasterPass("Present").readTexture(histogram).writeBackbuffer().setRenderFunc(noop());

    REQUIRE(graph.compile());
    CHECK(graph.diagnostics().livePasses == 2);
    CHECK(graph.passViewIds()[0] == kRenderGraphViewBase);
    CHECK(graph.passViewIds()[1] == kRenderGraphBackbufferView);
}

TEST_CASE("render graph rejects an invalid view size") {
    RenderGraph graph("view-size");
    graph.beginFrame(64, 32, 0);
    const RenderGraphTextureHandle color = graph.createTexture(transientColorDesc(), "color");
    graph.addRasterPass("Bad view").setColorAttachment(color).setViewSize(32, 0).setRenderFunc(noop());

    CHECK_FALSE(graph.compile());
    CHECK(graph.lastError().find("invalid pass view size") != std::string::npos);
}

TEST_CASE("render graph keeps an explicitly uncullable read-only pass") {
    RenderGraph graph("uncullable");
    graph.beginFrame(64, 32, 0);

    const RenderGraphTextureHandle texture = graph.createTexture(transientColorDesc(), "input");
    graph.addRasterPass("Writer").writeTexture(texture).setRenderFunc(noop());
    graph.addRasterPass("Side effect").readTexture(texture).allowCulling(false).setRenderFunc(noop());
    graph.addRasterPass("Presentation").writeBackbuffer().setRenderFunc(noop());

    REQUIRE(graph.compile());
    CHECK(graph.diagnostics().livePasses == 3);
    CHECK(graph.diagnostics().culledPasses == 0);
}

TEST_CASE("render graph keeps distinct mip attachments and their lifetimes") {
    RenderGraph graph("mips");
    graph.beginFrame(64, 32, 0);

    gfx::TextureDesc desc = transientColorDesc();
    desc.mipCount = 4;
    const RenderGraphTextureHandle mipChain = graph.createTexture(desc, "mip-chain");

    for (uint16_t mip = 0; mip < 4; ++mip) {
        graph.addRasterPass("Mip " + std::to_string(mip))
            .setColorAttachment(mipChain, 0, mip)
            .allowCulling(false)
            .setRenderFunc(noop());
    }
    graph.addRasterPass("Read mip chain").readTexture(mipChain).writeBackbuffer().setRenderFunc(noop());

    REQUIRE(graph.compile());
    CHECK(graph.diagnostics().recordedPasses == 5);
    CHECK(graph.diagnostics().livePasses == 5);
    CHECK(graph.diagnostics().transientTextures == 1);
}

TEST_CASE("render graph rejects a mip outside the texture chain") {
    RenderGraph graph("bad mip");
    graph.beginFrame(64, 32, 0);

    gfx::TextureDesc desc = transientColorDesc();
    desc.mipCount = 1;
    const RenderGraphTextureHandle texture = graph.createTexture(desc, "single-mip");
    graph.addRasterPass("Bad mip").setColorAttachment(texture, 0, 1).allowCulling(false).setRenderFunc(noop());
    graph.addRasterPass("Present").readTexture(texture).writeBackbuffer().setRenderFunc(noop());

    CHECK_FALSE(graph.compile());
    CHECK(graph.lastError().find("invalid color attachment mip") != std::string::npos);
}

TEST_CASE("render graph rejects a transient read before its writer") {
    RenderGraph graph("validation");
    graph.beginFrame(64, 32, 0);

    const RenderGraphTextureHandle texture = graph.createTexture(transientColorDesc(), "missing");
    graph.addRasterPass("Read only").readTexture(texture).setRenderFunc(noop());

    CHECK_FALSE(graph.compile());
    CHECK(graph.lastError().find("before it is written") != std::string::npos);
}

TEST_CASE("render graph rejects stale handles after a new frame") {
    RenderGraph graph("generations");
    graph.beginFrame(64, 32, 0);
    const RenderGraphTextureHandle old = graph.createTexture(transientColorDesc(), "old");

    graph.beginFrame(64, 32, 1);
    graph.addRasterPass("Stale").writeTexture(old).writeBackbuffer().setRenderFunc(noop());

    CHECK_FALSE(graph.compile());
    CHECK(graph.lastError().find("invalid texture write") != std::string::npos);
}

TEST_CASE("render graph rejects malformed passes") {
    SUBCASE("missing render function") {
        RenderGraph graph("malformed");
        graph.beginFrame(64, 32, 0);
        graph.addRasterPass("No func").writeBackbuffer();
        CHECK_FALSE(graph.compile());
        CHECK(graph.lastError().find("no render function") != std::string::npos);
    }

    SUBCASE("backbuffer plus attachment") {
        RenderGraph graph("malformed");
        graph.beginFrame(64, 32, 0);
        const RenderGraphTextureHandle color = graph.createTexture(transientColorDesc(), "color");
        graph.addRasterPass("Mixed").setColorAttachment(color).writeBackbuffer().setRenderFunc(noop());
        CHECK_FALSE(graph.compile());
        CHECK(graph.lastError().find("backbuffer and also has attachments") != std::string::npos);
    }

    SUBCASE("multiple color resources at one attachment") {
        RenderGraph graph("malformed");
        graph.beginFrame(64, 32, 0);
        const RenderGraphTextureHandle first = graph.createTexture(transientColorDesc(), "first");
        const RenderGraphTextureHandle second = graph.createTexture(transientColorDesc(), "second");
        RenderGraphBuilder pass = graph.addRasterPass("Conflict");
        pass.setColorAttachment(first).setColorAttachment(second).setRenderFunc(noop());
        CHECK_FALSE(graph.compile());
        CHECK(graph.lastError().find("multiple color resources") != std::string::npos);
    }

    SUBCASE("multiple backbuffer passes") {
        RenderGraph graph("malformed");
        graph.beginFrame(64, 32, 0);
        graph.addRasterPass("Present A").writeBackbuffer().setRenderFunc(noop());
        graph.addRasterPass("Present B").writeBackbuffer().setRenderFunc(noop());
        CHECK_FALSE(graph.compile());
        CHECK(graph.lastError().find("more than one pass writes the backbuffer") != std::string::npos);
    }

    SUBCASE("transient buffer read before write") {
        RenderGraph graph("malformed");
        graph.beginFrame(64, 32, 0);
        gfx::BufferDesc desc;
        desc.type = gfx::BufferType::Vertex;
        desc.size = 48;
        desc.count = 3;
        const RenderGraphBufferHandle buffer = graph.createBuffer(desc, "vertices");
        graph.addRasterPass("Buffer read").readBuffer(buffer).writeBackbuffer().setRenderFunc(noop());
        CHECK_FALSE(graph.compile());
        CHECK(graph.lastError().find("transient buffer") != std::string::npos);
    }
}
