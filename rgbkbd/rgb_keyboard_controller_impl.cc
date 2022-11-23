// Copyright 2022 The ChromiumOS Authors
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
    SetCapsLockState(/*enabled=*/true);
  }
}

void RgbKeyboardControllerImpl::SetKeyboardClient(RgbKeyboard* keyboard) {
  DCHECK(keyboard);
  keyboard_ = keyboard;
}

void RgbKeyboardControllerImpl::SetKeyboardCapabilityForTesting(
    RgbKeyboardCapabilities capability) {
  capabilities_ = capability;
  if (capabilities_.value() == RgbKeyboardCapabilities::kIndividualKey) {
    PopulateRainbowModeMap();
  } else {
    individual_key_rainbow_mode_map_.clear();
  }
}

const std::vector<uint32_t>& RgbKeyboardControllerImpl::GetZone(
    int zone) const {
  DCHECK(capabilities_.has_value());
  DCHECK(zone >= 0 && zone < GetZoneCount());
  switch (capabilities_.value()) {
    case RgbKeyboardCapabilities::kNone:
      NOTREACHED();
      return kEmptyZone;
    case RgbKeyboardCapabilities::kIndividualKey:
      return GetIndividualKeyZones()[zone];
    case RgbKeyboardCapabilities::kFourZoneFortyLed:
      return GetFourtyLedZones()[zone];
    case RgbKeyboardCapabilities::kFourZoneTwelveLed:
      return GetTwelveLedZones()[zone];
    case RgbKeyboardCapabilities::kFourZoneFourLed:
      return GetFourLedZones()[zone];
  }
}

int RgbKeyboardControllerImpl::GetZoneCount() const {
  DCHECK(capabilities_.has_value());
  switch (capabilities_.value()) {
    case RgbKeyboardCapabilities::kNone:
      return 0;
    case RgbKeyboardCapabilities::kIndividualKey:
      return 5;
    case RgbKeyboardCapabilities::kFourZoneFortyLed:
    case RgbKeyboardCapabilities::kFourZoneTwelveLed:
    case RgbKeyboardCapabilities::kFourZoneFourLed:
      return 4;
  }
}

Color RgbKeyboardControllerImpl::GetRainbowZoneColor(int zone) const {
  DCHECK(capabilities_.has_value());
  DCHECK(zone >= 0 && zone < GetZoneCount());
  switch (capabilities_.value()) {
    case RgbKeyboardCapabilities::kNone:
      NOTREACHED();
      return kWhiteBackgroundColor;
    case RgbKeyboardCapabilities::kIndividualKey:
      return kIndividualKeyRainbowColors[zone];
    case RgbKeyboardCapabilities::kFourZoneFortyLed:
      return kFourZonesRainbowColors[zone];
    case RgbKeyboardCapabilities::kFourZoneTwelveLed:
      return kFourZonesRainbowColors[zone];
    case RgbKeyboardCapabilities::kFourZoneFourLed:
      return kFourZonesRainbowColors[zone];
  }
}

void RgbKeyboardControllerImpl::SetZoneColor(int zone,
                                             uint8_t r,
                                             uint8_t g,
                                             uint8_t b) {
  if (zone < 0 || zone >= GetZoneCount()) {
    LOG(ERROR) << "Attempted to set color for invalid zone: " << zone;
    return;
  }
  // TODO(swifton): fix Caps Lock handling.
  for (uint32_t led : GetZone(zone)) {
    // Check if caps lock is enabled to avoid overriding the caps lock
    // highlight keys.
    if (caps_lock_enabled_ && IsShiftKey(led)) {
      continue;
    }

    keyboard_->SetKeyColor(led, r, g, b);
  }
}

void RgbKeyboardControllerImpl::SetRainbowMode() {
  DCHECK(capabilities_.has_value());

  background_type_ = BackgroundType::kStaticRainbow;

  int zone_count = GetZoneCount();
  for (int zone = 0; zone < zone_count; ++zone) {
    Color color = GetRainbowZoneColor(zone);
    SetZoneColor(zone, color.r, color.g, color.b);
  }
}

// TODO(jimmyxgong): Implement this stub.
void RgbKeyboardControllerImpl::SetAnimationMode(RgbAnimationMode mode) {
  NOTIMPLEMENTED();
}

std::vector<KeyColor> RgbKeyboardControllerImpl::
    GetRainbowModeColorsWithShiftKeysHighlightedForTesting() {
  DCHECK(capabilities_ == RgbKeyboardCapabilities::kIndividualKey);
  std::vector<KeyColor> vec;

  vec.emplace_back(kLeftShiftKey, kCapsLockHighlightAlternate);
  vec.emplace_back(kRightShiftKey, kCapsLockHighlightAlternate);

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

void RgbKeyboardControllerImpl::ReinitializeOnDeviceReconnected() {
  SetKeyColor({kLeftShiftKey, GetCurrentCapsLockColor(kLeftShiftKey)});
  SetKeyColor({kRightShiftKey, GetCurrentCapsLockColor(kRightShiftKey)});

  switch (background_type_) {
    case BackgroundType::kStaticSingleColor:
      SetStaticBackgroundColor(background_color_.r, background_color_.g,
                               background_color_.b);
      break;
    case BackgroundType::kStaticRainbow:
      SetRainbowMode();
      break;
  }
}

}  // namespace rgbkbd
