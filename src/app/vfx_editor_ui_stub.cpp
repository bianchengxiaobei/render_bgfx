#include "app/vfx_editor_ui.h"

namespace hbg {

struct VfxEditorUI::Impl {};

VfxEditorUI::VfxEditorUI() : m_impl(std::make_unique<Impl>()) {}
VfxEditorUI::~VfxEditorUI() = default;

void VfxEditorUI::setCompileOptions(const VfxEditorCompileOptions&) {}
void VfxEditorUI::setReloadCallback(VfxEditorReloadCallback) {}
void VfxEditorUI::setPreviewCallback(VfxEditorPreviewCallback) {}
void VfxEditorUI::setPlaybackCallback(VfxEditorPlaybackCallback) {}
void VfxEditorUI::setPlaybackStatusProvider(VfxEditorPlaybackStatusProvider) {}
void VfxEditorUI::setDiagnosticsProvider(VfxEditorDiagnosticsProvider) {}

bool VfxEditorUI::open(const std::filesystem::path&, std::string& error) {
    error = "VFX editor is not available because HBG_BUILD_VFX=OFF";
    return false;
}

bool VfxEditorUI::compile(std::string& error) {
    error = "VFX editor is not available because HBG_BUILD_VFX=OFF";
    return false;
}

bool VfxEditorUI::preview(std::string& error) {
    error = "VFX editor is not available because HBG_BUILD_VFX=OFF";
    return false;
}

bool VfxEditorUI::saveRecovery(std::string& error) {
    error = "VFX editor is not available because HBG_BUILD_VFX=OFF";
    return false;
}

bool VfxEditorUI::valid() const {
    return false;
}
bool VfxEditorUI::dirty() const {
    return false;
}
void VfxEditorUI::requestClose() {}
bool VfxEditorUI::closeApproved() const {
    return true;
}
void VfxEditorUI::draw() {}

} // namespace hbg
