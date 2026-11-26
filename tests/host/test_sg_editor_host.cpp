#include "app/sg_editor_host.h"

#include <doctest/doctest.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "cpp-httplib/httplib.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace {

struct HostReply {
    int status = 0;
    std::string body;
    std::string contentType;
};

HostReply request(httplib::Client& client, const char* method, const std::string& path,
                  const std::string& body = {}, const std::string& contentType = {}) {
    httplib::Result result;
    if (std::string(method) == "GET") {
        result = client.Get(path);
    } else {
        result = client.Post(path, body, contentType);
    }
    if (!result) {
        return {};
    }
    return {result->status, result->body, result->get_header_value("Content-Type")};
}

} // namespace

TEST_CASE("SG editor host serves document, commands, security, and save") {
    const std::filesystem::path root = std::filesystem::path(HBG_PROJECT_ROOT) / "tests/shadergraph";
    const std::filesystem::path savedGraph = root / "host_test_tmp.sgraph.json";
    const std::filesystem::path savedLayout = root / "host_test_tmp.sgraph.editor.json";
    std::error_code cleanupError;
    std::filesystem::remove(savedGraph, cleanupError);
    std::filesystem::remove(savedLayout, cleanupError);

    hbg::SgEditorHost host;
    hbg::SgEditorHost::Options options;
    options.documentPath = "assets/smoke_lit.sgraph.json";
    options.root = root;
    options.projectRoot = HBG_PROJECT_ROOT;
    options.port = 0;
    options.token = "0123456789abcdef0123456789abcdef";
    std::string error;
    REQUIRE_MESSAGE(host.start(options, error), error);
    CHECK(host.port() != 0);
    CHECK(host.token() == options.token);

    std::atomic<bool> done{false};
    HostReply session;
    HostReply document;
    HostReply preview;
    HostReply command;
    HostReply stale;
    HostReply escape;
    HostReply catalog;
    HostReply saved;
    HostReply unauthorized;
    HostReply forbidden;
    HostReply devOrigin;
    std::thread clientThread([&] {
        httplib::Client client("127.0.0.1", host.port());
        client.set_default_headers({{"X-SG-Token", options.token}});
        session = request(client, "GET", "/api/session");
        document = request(client, "GET", "/api/document");
        preview = request(client, "GET", "/api/preview.jpg?size=128");
        command = request(client, "POST", "/api/commands",
                          R"({"baseRevision":1,"commands":[{"op":"addNode","type":"Multiply","id":200,"position":[10,20]}]})",
                          "application/json");
        stale = request(client, "POST", "/api/commands",
                        R"({"baseRevision":1,"commands":[{"op":"addNode","type":"Multiply","id":201}]})",
                        "application/json");
        escape = request(client, "POST", "/api/open", R"({"path":"../escape.sgraph.json"})",
                         "application/json");
        catalog = request(client, "GET", "/api/catalog/nodes?q=multiply&limit=5");
        saved = request(client, "POST", "/api/save-as", R"({"path":"host_test_tmp.sgraph.json"})",
                        "application/json");

        httplib::Client anonymous("127.0.0.1", host.port());
        unauthorized = request(anonymous, "GET", "/api/session");

        httplib::Client badOrigin("127.0.0.1", host.port());
        badOrigin.set_default_headers(
            {{"X-SG-Token", options.token}, {"Origin", "http://evil.example"}});
        forbidden = request(badOrigin, "GET", "/api/session");

        httplib::Client localDevOrigin("127.0.0.1", host.port());
        localDevOrigin.set_default_headers(
            {{"X-SG-Token", options.token}, {"Origin", "http://localhost:5173"}});
        devOrigin = request(localDevOrigin, "GET", "/api/session");
        done = true;
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(done.load());
    clientThread.join();
    host.poll();

    CHECK(unauthorized.status == 401);
    CHECK(forbidden.status == 403);
    CHECK(devOrigin.status == 200);
    CHECK(session.status == 200);
    CHECK(session.body.find("\"protocolVersion\":1") != std::string::npos);
    CHECK(session.body.find("\"preview\":false") != std::string::npos);
    CHECK(document.status == 200);
    CHECK(preview.status == 503);
    CHECK(command.status == 200);
    CHECK(command.body.find("\"revision\":2") != std::string::npos);
    CHECK(command.body.find("\"id\":200") != std::string::npos);
    CHECK(stale.status == 409);
    CHECK(stale.body.find("SG_STALE_REVISION") != std::string::npos);
    CHECK(escape.status == 400);
    CHECK(escape.body.find("parent path") != std::string::npos);
    CHECK(catalog.status == 200);
    CHECK(catalog.body.find("Multiply") != std::string::npos);
    CHECK(catalog.body.find("\"slots\":") != std::string::npos);
    CHECK(catalog.body.find("\"direction\":\"in\"") != std::string::npos);
    CHECK(catalog.body.find("\"defaultPrecision\":") != std::string::npos);
    CHECK(saved.status == 200);
    CHECK(std::filesystem::exists(savedGraph));
    CHECK(std::filesystem::exists(savedLayout));

    host.stop();
    CHECK_FALSE(host.running());
    std::filesystem::remove(savedGraph, cleanupError);
    std::filesystem::remove(savedLayout, cleanupError);
}


TEST_CASE("SG editor host compiles and serves node previews") {
    const std::filesystem::path root = std::filesystem::path(HBG_PROJECT_ROOT) / "tests/shadergraph";
    hbg::SgEditorHost host;
    hbg::SgEditorHost::Options options;
    options.documentPath = "assets/smoke_lit.sgraph.json";
    options.root = root;
    options.projectRoot = HBG_PROJECT_ROOT;
    options.previewBackend = "dxbc";
    options.port = 0;
    options.token = "0123456789abcdef0123456789abcdef";
    std::string error;
    REQUIRE_MESSAGE(host.start(options, error), error);

    std::atomic<bool> callbackSawPng{false};
    host.setPreviewCallback([&callbackSawPng](const std::vector<std::uint8_t>& sgb,
                                              const hbg::SgPreviewOptions& preview,
                                              std::vector<std::uint8_t>& image,
                                              std::string& renderError) {
        if (sgb.empty()) {
            renderError = "empty node preview shader";
            return false;
        }
        callbackSawPng = preview.format == "png";
        image = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00};
        return true;
    });

    std::atomic<bool> done{false};
    HostReply session;
    HostReply nodePreview;
    HostReply blockPreview;
    HostReply missingPreview;
    std::thread clientThread([&] {
        httplib::Client client("127.0.0.1", host.port());
        client.set_default_headers({{"X-SG-Token", options.token}});
        session = request(client, "GET", "/api/session");
        for (int attempt = 0; attempt < 120; ++attempt) {
            nodePreview = request(client, "GET", "/api/preview/node/4.png?rev=1&size=128");
            if (nodePreview.status != 202) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        blockPreview = request(client, "GET", "/api/preview/node/5.png");
        missingPreview = request(client, "GET", "/api/preview/node/999.png");
        done = true;
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(done.load());
    clientThread.join();
    host.poll();
    host.poll();

    CHECK(session.status == 200);
    CHECK(session.body.find("\"nodePreview\":true") != std::string::npos);
    CHECK(nodePreview.status == 200);
    CHECK(nodePreview.contentType == "image/png");
    CHECK(nodePreview.body.size() == 9);
    CHECK(callbackSawPng.load());
    CHECK(blockPreview.status == 404);
    CHECK(missingPreview.status == 404);
    host.stop();
}
