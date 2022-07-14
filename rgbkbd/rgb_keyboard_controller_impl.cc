// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/rgb_keyboard_controller_impl.h"

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/logging.h"
#include "base/notreached.h"

namespace rgbkbd {

RgbKeyboardControllerImpl::RgbKeyboardControllerImpl(RgbKeyboard* keyboard)
    : keyboard_(keyboard), background_color_(kWhiteBackgroundColor) {}
RgbKeyboardControllerImpl::~RgbKeyboardControllerImpl() = default;

uint32_t RgbKeyboardControllerImpl::GetRgbKeyboardCapabilities() {
  if (!capabilities_.has_value()) {
    capabilities_ = keyboard_->GetRgbKeyboardCapabilities();
    if (capabilities_.value() == RgbKeyboardCapabilities::kIndividualKey) {
      PopulateRainbowModeMap();
    }
  }

  return static_cast<uint32_t>(capabilities_.value());
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
  // Per zone keyboards can not independently set left/right shift RGB colors.
  // TODO(michaelcheco): Prevent this call from happening for per zone keyboards
  // higher up in the stack.
  if (IsZonedKeyboard()) {
    LOG(ERROR) << "Attempted to set caps lock color for a per zone keyboard";
    return;
  }

  SetKeyColor({kLeftShiftKey, GetCurrentCapsLockColor(kLeftShiftKey)});
  SetKeyColor({kRightShiftKey, GetCurrentCapsLockColor(kRightShiftKey)});
}

void RgbKeyboardControllerImpl::SetStaticBackgroundColor(uint8_t r,
                                                         uint8_t g,
                                                         uint8_t b) {
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

void RgbKeyboardControllerImpl::SetKeyboardCapabilityForTesting(
    RgbKeyboardCapabilities capability) {
  DCHECK(!capabilities_.has_value());
  capabilities_ = capability;
  if (capabilities_.value() == RgbKeyboardCapabilities::kIndividualKey) {
    PopulateRainbowModeMap();
  } else {
    individual_key_rainbow_mode_map_.clear();
  }
}

const base::span<const KeyColor>
RgbKeyboardControllerImpl::GetRainbowModeForKeyboard() const {
  DCHECK(capabilities_.has_value());
  switch (capabilities_.value()) {
    case RgbKeyboardCapabilities::kNone:
      NOTREACHED();
      return base::span<const KeyColor>();
    case RgbKeyboardCapabilities::kIndividualKey:
      return base::span<const KeyColor>(kRainbowModeIndividualKey,
                                        std::size(kRainbowModeIndividualKey));
    case RgbKeyboardCapabilities::kFourZoneFortyLed:
      return base::span<const KeyColor>(
          kRainbowModeFourZoneFortyLed,
          std::size(kRainbowModeFourZoneFortyLed));
    case RgbKeyboardCapabilities::kFourZoneTwelveLed:
      return base::span<const KeyColor>(
          kRainbowModeFourZoneTwelveLed,
          std::size(kRainbowModeFourZoneTwelveLed));
    case RgbKeyboardCapabilities::kFourZoneFifteenLed:
      return base::span<const KeyColor>(
          kRainbowModeFourZoneFifteenLed,
          std::size(kRainbowModeFourZoneFifteenLed));
  }
}

void RgbKeyboardControllerImpl::SetRainbowMode() {
  background_type_ = BackgroundType::kStaticRainbow;
  for (const auto& entry : GetRainbowModeForKeyboard()) {
    // Check if caps lock is enabled to avoid overriding the caps lock
    // highlight keys.
    if (caps_lock_enabled_ && IsShiftKey(entry.key)) {
      continue;
    }
    keyboard_->SetKeyColor(entry.key, entry.color.r, entry.color.g,
                           entry.color.b);
  }
}

// TODO(jimmyxgong): Implement this stub.
void RgbKeyboardControllerImpl::SetAnimationMode(RgbAnimationMode mode) {
  NOTIMPLEMENTED();
}

const std::vector<KeyColor>
RgbKeyboardControllerImpl::GetRainbowModeColorsWithoutShiftKeysForTesting() {
  DCHECK(capabilities_ == RgbKeyboardCapabilities::kIndividualKey);
  std::vector<KeyColor> vec;
  for (const auto& entry : kRainbowModeIndividualKey) {
    if (entry.key == kLeftShiftKey || entry.key == kRightShiftKey) {
      continue;
    }
    vec.push_back(entry);
  }
  return vec;
}

Color RgbKeyboardControllerImpl::GetCurrentCapsLockColor(
    uint32_t key_idx) const {
  if (caps_lock_enabled_) {
    return GetCapsLockHighlightColor();
  }

  if (background_type_ == BackgroundType::kStaticRainbow) {
    return GetRainbowColorForKey(key_idx);
  }

  return background_color_;
}

Color RgbKeyboardControllerImpl::GetCapsLockHighlightColor() const {
  return (background_color_ == kWhiteBackgroundColor)
             ? kCapsLockHighlightAlternate
             : kCapsLockHighlightDefault;
}

Color RgbKeyboardControllerImpl::GetRainbowColorForKey(uint32_t key_idx) const {
  DCHECK(capabilities_ == RgbKeyboardCapabilities::kIndividualKey);
  return individual_key_rainbow_mode_map_.at(key_idx);
}

void RgbKeyboardControllerImpl::PopulateRainbowModeMap() {
  std::vector<std::pair<uint32_t, Color>> vec;
  vec.reserve(std::size(kRainbowModeIndividualKey));
  for (auto i = 0; i < std::size(kRainbowModeIndividualKey); i++) {
    vec.push_back(std::make_pair(kRainbowModeIndividualKey[i].key,
                                 kRainbowModeIndividualKey[i].color));
  }
  individual_key_rainbow_mode_map_ =
      base::flat_map<uint32_t, Color>(std::move(vec));
}

bool RgbKeyboardControllerImpl::IsZonedKeyboard() const {
  DCHECK(capabilities_.has_value());
  return capabilities_.value() != RgbKeyboardCapabilities::kIndividualKey;
}
}  // namespace rgbkbd
