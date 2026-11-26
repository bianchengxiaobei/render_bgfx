#pragma once

#include "app/sg_preview.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace hbg {

class SgEditorHost {
  public:
    struct Options {
        std::filesystem::path documentPath;
        std::filesystem::path root;
        std::filesystem::path webRoot;
        std::filesystem::path projectRoot;
        std::filesystem::path shaderc;
        std::filesystem::path bgfxSource;
        std::string previewBackend;
        std::string token;
        std::uint16_t port = 8777;
    };

    SgEditorHost();
    ~SgEditorHost();

    SgEditorHost(const SgEditorHost&) = delete;
    SgEditorHost& operator=(const SgEditorHost&) = delete;

    bool start(const Options& options, std::string& error);
    void stop();
    void poll();
    bool running() const;
    std::uint16_t port() const;
    const std::string& token() const;
    void setPreviewCallback(SgPreviewCallback callback);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hbg
