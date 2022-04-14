// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/rgb_keyboard_controller_impl.h"

#include <utility>

namespace rgbkbd {

RgbKeyboardControllerImpl::RgbKeyboardControllerImpl(
    std::unique_ptr<RgbKeyboard> keyboard)
    : keyboard_(std::move(keyboard)),
      background_color_(kDefaultBackgroundColor) {}
RgbKeyboardControllerImpl::~RgbKeyboardControllerImpl() = default;

uint32_t RgbKeyboardControllerImpl::GetRgbKeyboardCapabilities() {
  return static_cast<uint32_t>(keyboard_capabilities_);
}

void RgbKeyboardControllerImpl::SetKeyColor(uint32_t key, const Color& color) {
  keyboard_->SetKeyColor(key, color.r, color.g, color.b);
}

void RgbKeyboardControllerImpl::SetCapsLockState(bool enabled) {
  caps_lock_enabled_ = enabled;
  const auto color = GetCurrentCapsLockColor();
  SetKeyColor(kLeftShiftKey, color);
  SetKeyColor(kRightShiftKey, color);
}

// TODO(jimmyxgong): Implement this stub.
void RgbKeyboardControllerImpl::SetStaticBackgroundColor(uint32_t r,
                                                         uint32_t g,
                                                         uint32_t b) {}

// TODO(jimmyxgong): Implement this stub.
void RgbKeyboardControllerImpl::SetRainbowMode() {}

}  // namespace rgbkbd
