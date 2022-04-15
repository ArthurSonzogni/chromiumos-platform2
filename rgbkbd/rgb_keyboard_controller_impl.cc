// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/rgb_keyboard_controller_impl.h"

#include <utility>

#include "base/check.h"

namespace rgbkbd {

RgbKeyboardControllerImpl::RgbKeyboardControllerImpl(RgbKeyboard* keyboard)
    : keyboard_(keyboard), background_color_(kDefaultBackgroundColor) {}
RgbKeyboardControllerImpl::~RgbKeyboardControllerImpl() = default;

uint32_t RgbKeyboardControllerImpl::GetRgbKeyboardCapabilities() {
  return static_cast<uint32_t>(keyboard_capabilities_);
}

void RgbKeyboardControllerImpl::SetKeyColor(const KeyColor& key_color) {
  keyboard_->SetKeyColor(key_color.key, key_color.color.r, key_color.color.g,
                         key_color.color.b);
}

void RgbKeyboardControllerImpl::SetAllKeyColors(const Color& color) {
  keyboard_->SetAllKeyColors(color.r, color.g, color.b);
}

void RgbKeyboardControllerImpl::SetCapsLockState(bool enabled) {
  caps_lock_enabled_ = enabled;
  const auto color = GetCurrentCapsLockColor();
  SetKeyColor({kLeftShiftKey, color});
  SetKeyColor({kRightShiftKey, color});
}

void RgbKeyboardControllerImpl::SetStaticBackgroundColor(uint32_t r,
                                                         uint32_t g,
                                                         uint32_t b) {
  background_type_ = BackgroundType::kStaticSingleColor;
  background_color_ = Color(r, g, b);
  SetAllKeyColors(background_color_);

  // If Capslock was enabled, re-color the highlight keys.
  if (caps_lock_enabled_) {
    SetCapsLockState(/*caps_lock_enabled_=*/true);
  }
}

void RgbKeyboardControllerImpl::SetKeyboardClient(RgbKeyboard* keyboard) {
  DCHECK(keyboard);
  keyboard_ = keyboard;
}

void RgbKeyboardControllerImpl::SetRainbowMode() {
  background_type_ = BackgroundType::kStaticRainbow;
  for (const auto& entry : kRainbowMode) {
    // Check if caps lock is enabled to avoid overriding the caps lock
    // highlight keys.
    if (caps_lock_enabled_ && IsShiftKey(entry.key)) {
      continue;
    }
    keyboard_->SetKeyColor(entry.key, entry.color.r, entry.color.g,
                           entry.color.b);
  }
}

Color RgbKeyboardControllerImpl::GetColorForBackgroundType() const {
  switch (background_type_) {
    case BackgroundType::kStaticSingleColor:
      return background_color_;
    case BackgroundType::kStaticRainbow:
      // TODO(michaelcheco): Determine colors for caps lock keys when rainbow
      // mode is enabled.
      return kCapsLockHighlightDefault;
  }
}

Color RgbKeyboardControllerImpl::GetCurrentCapsLockColor() const {
  return caps_lock_enabled_ ? GetCapsLockHighlightColor()
                            : GetColorForBackgroundType();
}

}  // namespace rgbkbd
