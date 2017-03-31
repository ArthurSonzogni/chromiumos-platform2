// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/input_device_controller.h"

#include <base/logging.h>
#include <vector>

#include "power_manager/common/prefs.h"
#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/system/acpi_wakeup_helper.h"
#include "power_manager/powerd/system/ec_wakeup_helper.h"
#include "power_manager/powerd/system/tagged_device.h"
#include "power_manager/powerd/system/udev.h"

namespace power_manager {
namespace policy {

namespace {

// Returns a string describing |mode|.
const char* ModeToString(InputDeviceController::Mode mode) {
  switch (mode) {
    case InputDeviceController::Mode::CLOSED:
      return "closed";
    case InputDeviceController::Mode::DOCKED:
      return "docked";
    case InputDeviceController::Mode::DISPLAY_OFF:
      return "display_off";
    case InputDeviceController::Mode::LAPTOP:
      return "laptop";
    case InputDeviceController::Mode::TABLET:
      return "tablet";
  }
  NOTREACHED() << "Invalid mode " << static_cast<int>(mode);
  return "unknown";
}

// Returns true if |device| has a "usable_when_[mode]" tag corresponding to
// |mode|.
bool IsUsableInMode(const system::TaggedDevice& device,
                    InputDeviceController::Mode mode) {
  switch (mode) {
    case InputDeviceController::Mode::CLOSED:
      return false;
    case InputDeviceController::Mode::DOCKED:
      return device.HasTag(InputDeviceController::kTagUsableWhenDocked);
    case InputDeviceController::Mode::DISPLAY_OFF:
      return device.HasTag(InputDeviceController::kTagUsableWhenDisplayOff);
    case InputDeviceController::Mode::LAPTOP:
      return device.HasTag(InputDeviceController::kTagUsableWhenLaptop);
    case InputDeviceController::Mode::TABLET:
      return device.HasTag(InputDeviceController::kTagUsableWhenTablet);
  }
  NOTREACHED() << "Invalid mode " << static_cast<int>(mode);
  return false;
}

// Returns true if |device| has any "wakeup_when_[mode]" tags.
bool HasModeWakeupTags(const system::TaggedDevice& device) {
  return device.HasTag(InputDeviceController::kTagWakeupWhenDocked) ||
         device.HasTag(InputDeviceController::kTagWakeupWhenDisplayOff) ||
         device.HasTag(InputDeviceController::kTagWakeupWhenLaptop) ||
         device.HasTag(InputDeviceController::kTagWakeupWhenTablet);
}

// Returns true if |device| has a "wakeup_when_[mode]" tag corresponding to
// |mode|.
bool IsWakeupEnabledInMode(const system::TaggedDevice& device,
                           InputDeviceController::Mode mode) {
  switch (mode) {
    case InputDeviceController::Mode::CLOSED:
      return false;
    case InputDeviceController::Mode::DOCKED:
      return device.HasTag(InputDeviceController::kTagWakeupWhenDocked);
    case InputDeviceController::Mode::DISPLAY_OFF:
      return device.HasTag(InputDeviceController::kTagWakeupWhenDisplayOff);
    case InputDeviceController::Mode::LAPTOP:
      return device.HasTag(InputDeviceController::kTagWakeupWhenLaptop);
    case InputDeviceController::Mode::TABLET:
      return device.HasTag(InputDeviceController::kTagWakeupWhenTablet);
  }
  NOTREACHED() << "Invalid mode " << static_cast<int>(mode);
  return false;
}

}  // namespace

const char InputDeviceController::kTagInhibit[] = "inhibit";
const char InputDeviceController::kTagUsableWhenDocked[] = "usable_when_docked";
const char InputDeviceController::kTagUsableWhenDisplayOff[] =
    "usable_when_display_off";
const char InputDeviceController::kTagUsableWhenLaptop[] = "usable_when_laptop";
const char InputDeviceController::kTagUsableWhenTablet[] = "usable_when_tablet";
const char InputDeviceController::kTagWakeup[] = "wakeup";
const char InputDeviceController::kTagWakeupWhenDocked[] = "wakeup_when_docked";
const char InputDeviceController::kTagWakeupWhenDisplayOff[] =
    "wakeup_when_display_off";
const char InputDeviceController::kTagWakeupWhenLaptop[] = "wakeup_when_laptop";
const char InputDeviceController::kTagWakeupWhenTablet[] = "wakeup_when_tablet";
const char InputDeviceController::kTagWakeupOnlyWhenUsable[] =
    "wakeup_only_when_usable";
const char InputDeviceController::kTagWakeupDisabled[] = "wakeup_disabled";

const char InputDeviceController::kPowerWakeup[] = "power/wakeup";
const char InputDeviceController::kEnabled[] = "enabled";
const char InputDeviceController::kDisabled[] = "disabled";
const char InputDeviceController::kUSBDevice[] = "usb_device";

const char InputDeviceController::kInhibited[] = "inhibited";

const char InputDeviceController::kTPAD[] = "TPAD";
const char InputDeviceController::kTSCR[] = "TSCR";

InputDeviceController::InputDeviceController() {}

InputDeviceController::~InputDeviceController() {
  if (udev_)
    udev_->RemoveTaggedDeviceObserver(this);
  if (backlight_controller_)
    backlight_controller_->RemoveObserver(this);
}

void InputDeviceController::Init(
    BacklightController* backlight_controller,
    system::UdevInterface* udev,
    system::AcpiWakeupHelperInterface* acpi_wakeup_helper,
    system::EcWakeupHelperInterface* ec_wakeup_helper,
    LidState lid_state,
    TabletMode tablet_mode,
    DisplayMode display_mode,
    PrefsInterface* prefs) {
  backlight_controller_ = backlight_controller;
  udev_ = udev;
  acpi_wakeup_helper_ = acpi_wakeup_helper;
  ec_wakeup_helper_ = ec_wakeup_helper;

  if (backlight_controller_)
    backlight_controller_->AddObserver(this);
  udev_->AddTaggedDeviceObserver(this);

  // Trigger initial configuration.
  prefs_ = prefs;
  lid_state_ = lid_state;
  tablet_mode_ = tablet_mode;
  display_mode_ = display_mode;
  backlight_enabled_ = true;
  prefs_->GetBool(kAllowDockedModePref, &allow_docked_mode_);

  UpdatePolicy();

  initialized_ = true;
}

void InputDeviceController::SetLidState(LidState lid_state) {
  lid_state_ = lid_state;
  UpdatePolicy();
}

void InputDeviceController::SetTabletMode(TabletMode tablet_mode) {
  tablet_mode_ = tablet_mode;
  UpdatePolicy();
}

void InputDeviceController::SetDisplayMode(DisplayMode display_mode) {
  display_mode_ = display_mode;
  UpdatePolicy();
}

void InputDeviceController::OnBrightnessChange(
    double brightness_percent,
    BacklightController::BrightnessChangeCause cause,
    BacklightController* source) {
  // Ignore if the brightness is turned *off* automatically (before suspend),
  // but do care if it's automatically turned *on* (unplugging ext. display).
  if (brightness_percent == 0.0 &&
      cause != BacklightController::BrightnessChangeCause::USER_INITIATED) {
    return;
  }
  backlight_enabled_ = brightness_percent != 0.0;
  UpdatePolicy();
}

void InputDeviceController::OnTaggedDeviceChanged(
    const system::TaggedDevice& device) {
  ConfigureInhibit(device);
  ConfigureWakeup(device);
}

void InputDeviceController::OnTaggedDeviceRemoved(
    const system::TaggedDevice& device) {}

void InputDeviceController::SetWakeupFromS3(const system::TaggedDevice& device,
                                            bool enabled) {
  // For USB devices, the input device does not have a power/wakeup property
  // itself, but the corresponding USB device does. If the matching device does
  // not have a power/wakeup property, we thus fall back to the first ancestor
  // that has one. Conflicts should not arise, since real-world USB input
  // devices typically only expose one input interface anyway. However,
  // crawling up sysfs should only reach the first "usb_device" node, because
  // higher-level nodes include USB hubs, and enabling wakeups on those isn't
  // a good idea.
  std::string parent_syspath;
  if (!udev_->FindParentWithSysattr(
          device.syspath(), kPowerWakeup, kUSBDevice, &parent_syspath)) {
    LOG(WARNING) << "No " << kPowerWakeup << " sysattr available for "
                 << device.syspath();
    return;
  }
  LOG(INFO) << (enabled ? "Enabling" : "Disabling") << " wakeup for "
            << device.syspath() << " through " << parent_syspath;
  udev_->SetSysattr(
      parent_syspath, kPowerWakeup, enabled ? kEnabled : kDisabled);
}

void InputDeviceController::ConfigureInhibit(
    const system::TaggedDevice& device) {
  // Should this device be inhibited when it is not usable?
  if (!device.HasTag(kTagInhibit))
    return;
  bool inhibit = !IsUsableInMode(device, mode_);
  LOG(INFO) << (inhibit ? "Inhibiting " : "Un-inhibiting ") << device.syspath();
  udev_->SetSysattr(device.syspath(), kInhibited, inhibit ? "1" : "0");
}

void InputDeviceController::ConfigureWakeup(
    const system::TaggedDevice& device) {
  // Do we manage wakeup for this device?
  if (!device.HasTag(kTagWakeup))
    return;

  bool wakeup = true;
  if (device.HasTag(kTagWakeupDisabled))
    wakeup = false;
  else if (device.HasTag(kTagWakeupOnlyWhenUsable))
    wakeup = IsUsableInMode(device, mode_);
  else if (HasModeWakeupTags(device))
    wakeup = IsWakeupEnabledInMode(device, mode_);

  SetWakeupFromS3(device, wakeup);
}

void InputDeviceController::ConfigureEcWakeup() {
  // Force the EC to do keyboard wakeups even in tablet mode when display off.
  if (!ec_wakeup_helper_->IsSupported())
    return;

  ec_wakeup_helper_->AllowWakeupAsTablet(mode_ == Mode::DISPLAY_OFF);
}

void InputDeviceController::ConfigureAcpiWakeup() {
  // On x86 systems, setting power/wakeup in sysfs is not enough, we also need
  // to go through /proc/acpi/wakeup.

  if (!acpi_wakeup_helper_->IsSupported())
    return;

  acpi_wakeup_helper_->SetWakeupEnabled(kTPAD, mode_ == Mode::LAPTOP);
  acpi_wakeup_helper_->SetWakeupEnabled(kTSCR, false);
}

InputDeviceController::Mode InputDeviceController::GetMode() const {
  if (allow_docked_mode_ && display_mode_ == DisplayMode::PRESENTATION &&
      lid_state_ == LidState::CLOSED)
    return Mode::DOCKED;

  // Prioritize DISPLAY_OFF over TABLET so that the keyboard won't be disabled
  // if a device in tablet mode is used as a "smart keyboard" (e.g.
  // panel-side-down with an external display connected).
  if (!backlight_enabled_ && display_mode_ == DisplayMode::PRESENTATION &&
      lid_state_ == LidState::OPEN)
    return Mode::DISPLAY_OFF;

  if (tablet_mode_ == TabletMode::ON)
    return Mode::TABLET;
  else if (lid_state_ == LidState::CLOSED)
    return Mode::CLOSED;
  else
    return Mode::LAPTOP;
}

void InputDeviceController::UpdatePolicy() {
  DCHECK(udev_);

  Mode new_mode = GetMode();
  if (initialized_ && mode_ == new_mode)
    return;

  mode_ = new_mode;

  LOG(INFO) << "Configuring devices for mode \"" << ModeToString(mode_) << "\"";
  std::vector<system::TaggedDevice> devices = udev_->GetTaggedDevices();
  // Configure inhibit first, as it is somewhat time-critical (we want to block
  // events as fast as possible), and wakeup takes a few milliseconds to set.
  for (const system::TaggedDevice& device : devices)
    ConfigureInhibit(device);
  for (const system::TaggedDevice& device : devices)
    ConfigureWakeup(device);

  ConfigureAcpiWakeup();
  ConfigureEcWakeup();
}

}  // namespace policy
}  // namespace power_manager
