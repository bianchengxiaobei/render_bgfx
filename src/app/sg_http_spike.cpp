#include "app/sg_http_spike.h"

#include "core/log.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "cpp-httplib/httplib.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace hbg {
namespace {

struct StreamFrame {
    std::shared_ptr<const std::vector<std::uint8_t>> data;
    std::uint64_t sequence = 0;
};

std::string urlEncode(const std::string& value) {
    std::string encoded;
    encoded.reserve(value.size());
    constexpr char kHex[] = "0123456789ABCDEF";
    for (const unsigned char character : value) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            encoded.push_back(static_cast<char>(character));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kHex[(character >> 4) & 0xf]);
        encoded.push_back(kHex[character & 0xf]);
    }
    return encoded;
}

} // namespace

struct SgMjpegServer::Impl {
    explicit Impl(std::string value) : token(std::move(value)) {}

    bool authorized(const httplib::Request& request) const {
        return token.empty() || request.get_param_value("token") == token;
    }

    bool waitNext(std::uint64_t lastSequence, StreamFrame& frame) {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::milliseconds(1000), [this, lastSequence] {
            return stopping || sequence != lastSequence;
        });
        if (stopping || sequence == lastSequence) {
            return false;
        }
        frame = latest;
        return true;
    }

    void publish(std::vector<std::uint8_t> jpeg) {
        auto data = std::make_shared<const std::vector<std::uint8_t>>(std::move(jpeg));
        {
            std::lock_guard<std::mutex> lock(mutex);
            latest.data = std::move(data);
            latest.sequence = ++sequence;
        }
        condition.notify_all();
    }

    std::string token;
    httplib::Server server;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable condition;
    StreamFrame latest;
    std::uint64_t sequence = 0;
    std::atomic<bool> stopping{false};
};

SgMjpegServer::SgMjpegServer() = default;

SgMjpegServer::~SgMjpegServer() {
    stop();
}

bool SgMjpegServer::start(std::uint16_t port, std::string token, std::string& error) {
    stop();
    auto impl = std::make_unique<Impl>(std::move(token));

    impl->server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("ok", "text/plain");
    });

    impl->server.Get("/", [implPtr = impl.get()](const httplib::Request& request,
                                                 httplib::Response& response) {
        if (!implPtr->authorized(request)) {
            response.status = 403;
            response.set_content("forbidden", "text/plain");
            return;
        }
        const std::string token = urlEncode(implPtr->token);
        const std::string html =
            "<!doctype html><html><head><meta charset=\"utf-8\"><title>Shader Graph spike</title>"
            "<style>html,body{margin:0;height:100%;background:#101018;color:#ddd;}"
            "body{display:flex;align-items:center;justify-content:center;font-family:sans-serif;}"
            "img{max-width:100vw;max-height:100vh;display:block;}"
            "p{position:fixed;left:12px;bottom:8px;margin:0;opacity:.66;font-size:12px;}</style>"
            "</head><body><img src=\"/stream.mjpeg?token=" +
            token + "\"><p>Shader Graph SG-0 MJPEG preview</p></body></html>";
        response.set_content(html, "text/html");
    });

    impl->server.Get("/frame.jpg", [implPtr = impl.get()](const httplib::Request& request,
                                                         httplib::Response& response) {
        if (!implPtr->authorized(request)) {
            response.status = 403;
            response.set_content("forbidden", "text/plain");
            return;
        }
        StreamFrame frame;
        {
            std::lock_guard<std::mutex> lock(implPtr->mutex);
            frame = implPtr->latest;
        }
        if (frame.data == nullptr || frame.data->empty()) {
            response.status = 503;
            response.set_content("preview frame is not ready", "text/plain");
            return;
        }
        response.set_header("Cache-Control", "no-store");
        response.set_content(reinterpret_cast<const char*>(frame.data->data()), frame.data->size(),
                             "image/jpeg");
    });

    impl->server.Get("/stream.mjpeg", [implPtr = impl.get()](const httplib::Request& request,
                                                           httplib::Response& response) {
        if (!implPtr->authorized(request)) {
            response.status = 403;
            response.set_content("forbidden", "text/plain");
            return;
        }
        response.set_header("Cache-Control", "no-store");
        response.set_chunked_content_provider(
            "multipart/x-mixed-replace; boundary=frame",
            [implPtr, lastSequence = std::uint64_t{0}](std::size_t,
                                                       httplib::DataSink& sink) mutable {
                if (!sink.is_writable()) {
                    return false;
                }
                StreamFrame frame;
                if (!implPtr->waitNext(lastSequence, frame)) {
                    return !implPtr->stopping.load();
                }
                lastSequence = frame.sequence;
                if (frame.data == nullptr) {
                    return true;
                }
                char header[160];
                const int headerLength = std::snprintf(
                    header, sizeof(header),
                    "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                    frame.data->size());
                if (headerLength <= 0) {
                    return false;
                }
                if (!sink.write(header, static_cast<std::size_t>(headerLength))) {
                    return false;
                }
                if (!sink.write(reinterpret_cast<const char*>(frame.data->data()),
                                frame.data->size())) {
                    return false;
                }
                return sink.write("\r\n", 2);
            },
            nullptr);
    });

    if (!impl->server.bind_to_port("127.0.0.1", port)) {
        error = "failed to bind 127.0.0.1:" + std::to_string(port);
        return false;
    }
    impl->thread = std::thread([server = &impl->server] { server->listen_after_bind(); });
    for (int attempt = 0; attempt < 100 && !impl->server.is_running(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!impl->server.is_running()) {
        impl->server.stop();
        if (impl->thread.joinable()) {
            impl->thread.join();
        }
        error = "preview server did not start";
        return false;
    }
    m_impl = std::move(impl);
    return true;
}

void SgMjpegServer::stop() {
    if (!m_impl) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->stopping = true;
    }
    m_impl->condition.notify_all();
    m_impl->server.stop();
    if (m_impl->thread.joinable()) {
        m_impl->thread.join();
    }
    m_impl.reset();
}

void SgMjpegServer::publish(std::vector<std::uint8_t> jpeg) {
    if (m_impl) {
        m_impl->publish(std::move(jpeg));
    }
}

bool SgMjpegServer::running() const {
    return m_impl != nullptr;
}

} // namespace hbg
