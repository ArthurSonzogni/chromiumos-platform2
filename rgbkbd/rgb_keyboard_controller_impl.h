// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
#define RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <dbus/rgbkbd/dbus-constants.h>

#include "rgbkbd/rgb_keyboard.h"
#include "rgbkbd/rgb_keyboard_controller.h"

namespace rgbkbd {
struct Color {
  constexpr Color(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  bool operator==(const Color& rhs) const {
    return (r == rhs.r) && (g == rhs.g) && (b == rhs.b);
  }
};

struct KeyColor {
  constexpr KeyColor(uint32_t key, Color color) : key(key), color(color) {}
  uint32_t key;
  Color color;
};

// Default color for caps lock highlight color.
static constexpr Color kCapsLockHighlightDefault =
    Color(/*r=*/255, /*g=*/255, /*b=*/210);

static constexpr Color kCapsLockHighlightAlternate =
    Color(/*r=*/25, /*g=*/55, /*b=*/210);

// Default background color.
static constexpr Color kWhiteBackgroundColor =
    Color(/*r=*/255, /*g=*/255, /*b=*/210);

static constexpr uint32_t kLeftShiftKey = 44;
static constexpr uint32_t kRightShiftKey = 57;

// Rainbow mode constants.
static constexpr Color kRainbowRed = Color(/*r=*/0xc5, /*g=*/0x22, /*b=*/0x1f);
static constexpr Color kRainbowYellow =
    Color(/*r=*/0xec, /*g=*/0x6a, /*b=*/0x08);
static constexpr Color kRainbowGreen =
    Color(/*r=*/0x1b, /*g=*/0xb3, /*b=*/0x19);
static constexpr Color kRainbowLightBlue =
    Color(/*r=*/0x20, /*g=*/0xb1, /*b=*/0x89);
static constexpr Color kRainbowIndigo =
    Color(/*r=*/0x19, /*g=*/0x37, /*b=*/0xd2);
static constexpr Color kRainbowPurple =
    Color(/*r=*/0x84, /*g=*/0x20, /*b=*/0xb4);

const KeyColor kRainbowModeIndividualKey[] = {
    {1, kRainbowRed},      // ~
    {2, kRainbowRed},      // 1
    {3, kRainbowYellow},   // 2
    {4, kRainbowYellow},   // 3
    {5, kRainbowYellow},   // 4
    {6, kRainbowGreen},    // 5
    {7, kRainbowGreen},    // 6
    {8, kRainbowGreen},    // 7
    {9, kRainbowGreen},    // 8
    {10, kRainbowIndigo},  // 9
    {11, kRainbowIndigo},  // 0
    {12, kRainbowIndigo},  // -
    {13, kRainbowPurple},  // =
    // Key 14 not present in this layout.
    {15, kRainbowPurple},  // Backspace
    {16, kRainbowRed},     // Tab
    {17, kRainbowRed},     // Q
    {18, kRainbowYellow},  // W
    {19, kRainbowYellow},  // E
    {20, kRainbowYellow},  // R
    {21, kRainbowGreen},   // T
    {22, kRainbowGreen},   // Y
    {23, kRainbowGreen},   // U
    {24, kRainbowIndigo},  // I
    {25, kRainbowIndigo},  // O
    {26, kRainbowIndigo},  // P
    {27, kRainbowPurple},  // [
    {28, kRainbowPurple},  // ]
    {29, kRainbowPurple},  // Backslash
    {30, kRainbowRed},     // Search/Launcher
    {31, kRainbowRed},     // A
    {32, kRainbowYellow},  // S
    {33, kRainbowYellow},  // D
    {34, kRainbowYellow},  // F
    {35, kRainbowGreen},   // G
    {36, kRainbowGreen},   // H
    {37, kRainbowIndigo},  // J
    {38, kRainbowIndigo},  // K
    {39, kRainbowIndigo},  // L
    {40, kRainbowPurple},  // ;
    {41, kRainbowPurple},  // '
    // Key 42 not present in this layout.
    {43, kRainbowPurple},          // Enter
    {kLeftShiftKey, kRainbowRed},  // Left Shift
    // Key 45 not present in this layout.
    {46, kRainbowRed},     // Z
    {47, kRainbowYellow},  // X
    {48, kRainbowYellow},  // C
    {49, kRainbowYellow},  // V
    {50, kRainbowGreen},   // B
    {51, kRainbowIndigo},  // N
    {52, kRainbowIndigo},  // M
    {53, kRainbowIndigo},  // ,
    {54, kRainbowPurple},  // .
    {55, kRainbowPurple},  // /
    // Key 56 not present in this layout.
    {kRightShiftKey, kRainbowPurple},  // Right Shift
    {58, kRainbowRed},                 // Ctrl
    {59, kRainbowPurple},              // Power
    {60, kRainbowRed},                 // Left Alt
    {61, kRainbowGreen},               // Space Bar
    {62, kRainbowPurple},              // Right Alt
    // Key 63 not present in this layout.
    {64, kRainbowPurple},  // Right Ctrl
    // Keys [65-78] not present in this layout.
    {79, kRainbowPurple},  // Left Arrow
    // Keys [80-82] not present in this layout.
    {83, kRainbowPurple},  // Top Arrow
    {84, kRainbowPurple},  // Bottom Arrow
    // Keys [85-88] not present in this layout.
    {89, kRainbowPurple},  // Right Arrow
    // Keys [90-109] not present in this layout.
    {110, kRainbowRed},     // Escape
    {111, kRainbowRed},     // T1: Back
    {112, kRainbowYellow},  // T2: Refresh
    {113, kRainbowYellow},  // T3: Full Screen
    {114, kRainbowYellow},  // T4: Overview
    {115, kRainbowGreen},   // T5: Snapshot
    {116, kRainbowGreen},   // T6: Brightness Down
    {117, kRainbowGreen},   // T7: Brightness Up
    {118, kRainbowGreen},   // T8: RGB Backlight Off
    {119, kRainbowGreen},   // T9: Play/Pause
    {120, kRainbowIndigo},  // T10: Mic Mute
    {121, kRainbowIndigo},  // T1: Volume Mute
    {122, kRainbowIndigo},  // T9: Play/Pause
    {123, kRainbowIndigo},  // T10: Mic Mute
};

const KeyColor kRainbowModeFiveZone[] = {
    {1, kRainbowRed},        {2, kRainbowRed},        {3, kRainbowRed},
    {4, kRainbowRed},        {5, kRainbowRed},        {6, kRainbowRed},
    {7, kRainbowRed},        {8, kRainbowRed},        {9, kRainbowRed},
    {10, kRainbowRed},       {11, kRainbowYellow},    {12, kRainbowYellow},
    {13, kRainbowYellow},    {14, kRainbowYellow},    {15, kRainbowYellow},
    {16, kRainbowYellow},    {17, kRainbowYellow},    {18, kRainbowYellow},
    {19, kRainbowYellow},    {20, kRainbowYellow},    {21, kRainbowGreen},
    {22, kRainbowGreen},     {23, kRainbowGreen},     {24, kRainbowGreen},
    {25, kRainbowGreen},     {26, kRainbowGreen},     {27, kRainbowGreen},
    {28, kRainbowGreen},     {29, kRainbowGreen},     {30, kRainbowGreen},
    {31, kRainbowLightBlue}, {32, kRainbowLightBlue}, {33, kRainbowLightBlue},
    {34, kRainbowLightBlue}, {35, kRainbowLightBlue}, {36, kRainbowLightBlue},
    {37, kRainbowLightBlue}, {38, kRainbowLightBlue}, {39, kRainbowLightBlue},
    {40, kRainbowLightBlue},
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
  void SetKeyboardCapabilityForTesting(RgbKeyboardCapabilities capability);

  bool IsCapsLockEnabledForTesting() const { return caps_lock_enabled_; }
  void SetCapabilitiesForTesting(RgbKeyboardCapabilities capabilities) {
    capabilities_ = capabilities;
  }

  const std::vector<KeyColor> GetRainbowModeColorsWithoutShiftKeysForTesting();

 private:
  void SetKeyColor(const KeyColor& key_color);
  void SetAllKeyColors(const Color& color);

  bool IsShiftKey(uint32_t key) const {
    return key == kLeftShiftKey || key == kRightShiftKey;
  }

  Color GetColorForBackgroundType() const;
  Color GetCurrentCapsLockColor() const;
  Color GetCapsLockHighlightColor() const;

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
