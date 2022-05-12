// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
#define RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_

#include <cstdint>
#include <memory>
#include <optional>

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
    Color(/*r=*/255, /*g=*/255, /*b=*/210);
// Default background color.
static constexpr Color kDefaultBackgroundColor =
    Color(/*r=*/255, /*g=*/255, /*b=*/255);
// TODO(michaelcheco): Update values for the left and right shift keys.
static constexpr uint32_t kLeftShiftKey = 1;
static constexpr uint32_t kRightShiftKey = 2;

// Rainbow mode constants.
static constexpr Color kRainbowRed = Color(/*r=*/197, /*g=*/34, /*b=*/31);
static constexpr Color kRainbowYellow = Color(/*r=*/236, /*g=*/106, /*b=*/8);
static constexpr Color kRainbowGreen = Color(/*r=*/51, /*g=*/128, /*b=*/28);
static constexpr Color kRainbowLightBlue =
    Color(/*r=*/32, /*g=*/177, /*b=*/137);
static constexpr Color kRainbowIndigo = Color(/*r=*/25, /*g=*/55, /*b=*/210);
static constexpr Color kRainbowPurple = Color(/*r=*/132, /*g=*/32, /*b=*/180);

// TODO(michaelcheco): Update values for keys.
const KeyColor kRainbowMode[] = {
    {kLeftShiftKey, kCapsLockHighlightDefault},
    {kRightShiftKey, kCapsLockHighlightDefault},
    {3, kRainbowRed},
    {4, kRainbowRed},
    {5, kRainbowYellow},
    {6, kRainbowYellow},
    {7, kRainbowGreen},
    {8, kRainbowGreen},
    {9, kRainbowLightBlue},
    {10, kRainbowLightBlue},
    {11, kRainbowIndigo},
    {12, kRainbowIndigo},
    {13, kRainbowPurple},
    {14, kRainbowPurple},
};

enum class BackgroundType {
  kStaticSingleColor,
  kStaticRainbow,
};

class RgbKeyboardControllerImpl : public RgbKeyboardController {
 public:
  explicit RgbKeyboardControllerImpl(RgbKeyboard* keyboard);
  ~RgbKeyboardControllerImpl();
  RgbKeyboardControllerImpl(const RgbKeyboardControllerImpl&) = delete;
  RgbKeyboardControllerImpl& operator=(const RgbKeyboardControllerImpl&) =
      delete;

  uint32_t GetRgbKeyboardCapabilities() override;
  void SetCapsLockState(bool enabled) override;
  void SetStaticBackgroundColor(uint8_t r, uint8_t g, uint8_t b) override;
  void SetRainbowMode() override;
  void SetAnimationMode(RgbAnimationMode mode) override;
  void SetKeyboardClient(RgbKeyboard* keyboard) override;

  bool IsCapsLockEnabledForTesting() const { return caps_lock_enabled_; }

 private:
  Color GetCapsLockHighlightColor() const {
    // TODO(michaelcheco): Choose color based on background.
    return kCapsLockHighlightDefault;
  }

  void SetKeyColor(const KeyColor& key_color);
  void SetAllKeyColors(const Color& color);

  bool IsShiftKey(uint32_t key) const {
    return key == kLeftShiftKey || key == kRightShiftKey;
  }

  Color GetColorForBackgroundType() const;
  Color GetCurrentCapsLockColor() const;

  std::optional<RgbKeyboardCapabilities> capabilities_;
  RgbKeyboard* keyboard_;
  Color background_color_;
  bool caps_lock_enabled_ = false;
  // Helps determine which color to highlight the caps locks keys when
  // disabling caps lock.
  BackgroundType background_type_ = BackgroundType::kStaticSingleColor;
};

}  // namespace rgbkbd

#endif  // RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
