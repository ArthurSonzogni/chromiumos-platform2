// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
#define RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_

#include <cstdint>
#include <memory>
#include <dbus/rgbkbd/dbus-constants.h>

#include "rgbkbd/rgb_keyboard.h"
#include "rgbkbd/rgb_keyboard_controller.h"

namespace rgbkbd {
struct Color {
  constexpr Color(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

struct KeyColor {
  constexpr KeyColor(uint32_t key, Color color) : key(key), color(color) {}
  uint32_t key;
  Color color;
};

// Default color for caps lock highlight color.
static constexpr Color kCapsLockHighlightDefault =
    Color(/*r=*/255, /*g=*/0, /*b=*/0);
// Default background color.
static constexpr Color kDefaultBackgroundColor =
    Color(/*r=*/255, /*g=*/255, /*b=*/255);
// TODO(michaelcheco): Update values for the left and right shift keys.
static constexpr uint32_t kLeftShiftKey = 10;
static constexpr uint32_t kRightShiftKey = 20;

class RgbKeyboardControllerImpl : public RgbKeyboardController {
 public:
  explicit RgbKeyboardControllerImpl(std::unique_ptr<RgbKeyboard> keyboard);
  ~RgbKeyboardControllerImpl();
  RgbKeyboardControllerImpl(const RgbKeyboardControllerImpl&) = delete;
  RgbKeyboardControllerImpl& operator=(const RgbKeyboardControllerImpl&) =
      delete;

  uint32_t GetRgbKeyboardCapabilities() override;
  void SetCapsLockState(bool enabled) override;
  void SetStaticBackgroundColor(uint32_t r, uint32_t g, uint32_t b) override;
  void SetRainbowMode() override;

  bool IsCapsLockEnabledForTesting() const { return caps_lock_enabled_; }

 private:
  Color GetCapsLockHighlightColor() const {
    // TODO(michaelcheco): Choose color based on background.
    return kCapsLockHighlightDefault;
  }

  Color GetCurrentCapsLockColor() const {
    return caps_lock_enabled_ ? GetCapsLockHighlightColor() : background_color_;
  }

  void SetKeyColor(uint32_t key, const Color& color);
  void SetAllKeyColors(const Color& color);

  RgbKeyboardCapabilities keyboard_capabilities_ =
      RgbKeyboardCapabilities::kNone;
  // TODO(michaelcheco): Add setter to switch between interface implementations.
  std::unique_ptr<RgbKeyboard> keyboard_;
  Color background_color_;
  bool caps_lock_enabled_ = false;
};

}  // namespace rgbkbd

#endif  // RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
