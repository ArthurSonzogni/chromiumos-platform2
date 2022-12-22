// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
#define RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/containers/flat_map.h>
#include <base/containers/span.h>
#include <brillo/usb/usb_device_event_observer.h>
#include <dbus/rgbkbd/dbus-constants.h>

#include "rgbkbd/constants.h"
#include "rgbkbd/rgb_keyboard.h"
#include "rgbkbd/rgb_keyboard_controller.h"

namespace rgbkbd {
enum class BackgroundType {
  kNone,
  kStaticSingleColor,
  kStaticRainbow,
};

class RgbKeyboardControllerImpl : public RgbKeyboardController,
                                  public brillo::UsbDeviceEventObserver {
 public:
  explicit RgbKeyboardControllerImpl(RgbKeyboard* keyboard);
  ~RgbKeyboardControllerImpl();
  RgbKeyboardControllerImpl(const RgbKeyboardControllerImpl&) = delete;
  RgbKeyboardControllerImpl& operator=(const RgbKeyboardControllerImpl&) =
      delete;

  uint32_t GetRgbKeyboardCapabilities() override;
  void SetCapsLockState(bool enabled) override;
  void SetStaticBackgroundColor(uint8_t r, uint8_t g, uint8_t b) override;
  void SetZoneColor(int zone, uint8_t r, uint8_t g, uint8_t b) override;
  void SetRainbowMode() override;
  void SetAnimationMode(RgbAnimationMode mode) override;
  void SetKeyboardClient(RgbKeyboard* keyboard) override;
  void SetKeyboardCapabilityForTesting(RgbKeyboardCapabilities capability);
  void ReinitializeOnDeviceReconnected() override;

  bool IsCapsLockEnabledForTesting() const { return caps_lock_enabled_; }

  std::vector<KeyColor>
  GetRainbowModeColorsWithShiftKeysHighlightedForTesting();
  const base::flat_map<uint32_t, Color>& GetRainbowModeMapForTesting() const {
    return individual_key_rainbow_mode_map_;
  }

  void SetKeyboardCapabilityAsIndividualKey();

  // brillo::UsbDeviceEventObserver:
  void OnUsbDeviceAdded(const std::string& sys_path,
                        uint8_t bus_number,
                        uint8_t device_address,
                        uint16_t vendor_id,
                        uint16_t product_id) override;
  void OnUsbDeviceRemoved(const std::string& sys_path) override;

 private:
  void SetKeyColor(const KeyColor& key_color);
  void SetAllKeyColors(const Color& color);

  bool IsShiftKey(uint32_t key) const {
    return key == kLeftShiftKey || key == kRightShiftKey;
  }

  const std::vector<uint32_t>& GetZone(int zone) const;
  int GetZoneCount() const;
  Color GetRainbowZoneColor(int zone) const;

  Color GetCurrentCapsLockColor(uint32_t key_idx) const;
  Color GetCapsLockHighlightColor() const;
  Color GetRainbowColorForKey(uint32_t key_idx) const;
  void PopulateRainbowModeMap();
  bool IsZonedKeyboard() const;

  base::flat_map<uint32_t, Color> individual_key_rainbow_mode_map_;
  std::optional<RgbKeyboardCapabilities> capabilities_;
  RgbKeyboard* keyboard_;
  Color background_color_;
  bool caps_lock_enabled_ = false;
  // Helps determine which color to highlight the caps locks keys when
  // disabling caps lock.
  BackgroundType background_type_ = BackgroundType::kNone;
  std::string prism_usb_sys_path_;
};

}  // namespace rgbkbd

#endif  // RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
