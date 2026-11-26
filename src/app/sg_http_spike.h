#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hbg {

class SgMjpegServer {
  public:
    SgMjpegServer();
    ~SgMjpegServer();

    SgMjpegServer(const SgMjpegServer&) = delete;
    SgMjpegServer& operator=(const SgMjpegServer&) = delete;

    bool start(std::uint16_t port, std::string token, std::string& error);
    void stop();
    void publish(std::vector<std::uint8_t> jpeg);
    bool running() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hbg
