#pragma once

#include <memory>

namespace hbg::dawn::detail {

struct AtlasNodeRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class AtlasNode {
public:
    AtlasNode() = default;

    AtlasNode(int width, int height) : width_(width), height_(height) {}

    bool insert(int width, int height, AtlasNodeRect* out = nullptr) {
        if (left_ != nullptr || right_ != nullptr) {
            if (left_ != nullptr && left_->insert(width, height, out)) {
                return true;
            }
            if (right_ != nullptr) {
                return right_->insert(width, height, out);
            }
            return false;
        }
        if (occupied_ || width > width_ || height > height_) {
            return false;
        }
        if (width == width_ && height == height_) {
            occupied_ = true;
            if (out != nullptr) {
                *out = AtlasNodeRect{offsetX_, offsetY_, width_, height_};
            }
            return true;
        }

        left_ = std::make_unique<AtlasNode>();
        right_ = std::make_unique<AtlasNode>();
        const int dw = width_ - width;
        const int dh = height_ - height;
        if (dw > dh) {
            left_->width_ = width;
            left_->height_ = height_;
            left_->offsetX_ = offsetX_;
            left_->offsetY_ = offsetY_;

            right_->width_ = dw;
            right_->height_ = height_;
            right_->offsetX_ = offsetX_ + width;
            right_->offsetY_ = offsetY_;
        } else {
            left_->width_ = width_;
            left_->height_ = height;
            left_->offsetX_ = offsetX_;
            left_->offsetY_ = offsetY_;

            right_->width_ = width_;
            right_->height_ = dh;
            right_->offsetX_ = offsetX_;
            right_->offsetY_ = offsetY_ + height;
        }
        return left_->insert(width, height, out);
    }

private:
    int width_ = 0;
    int height_ = 0;
    int offsetX_ = 0;
    int offsetY_ = 0;
    bool occupied_ = false;
    std::unique_ptr<AtlasNode> left_;
    std::unique_ptr<AtlasNode> right_;
};

} // namespace hbg::dawn::detail
