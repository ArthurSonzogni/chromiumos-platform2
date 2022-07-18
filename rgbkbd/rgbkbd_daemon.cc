// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/rgbkbd_daemon.h"

#include <memory>
#include <utility>

#include <base/threading/thread_task_runner_handle.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/rgbkbd/dbus-constants.h>

#include "base/check.h"
#include "base/files/file_path.h"
#include "rgbkbd/internal_rgb_keyboard.h"
#include "rgbkbd/keyboard_backlight_logger.h"

namespace {

constexpr char kLogFilePathForTesting[] = "/run/rgbkbd/log";

}  // namespace

namespace rgbkbd {
DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus, RgbkbdDaemon* daemon)
    : org::chromium::RgbkbdAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kRgbkbdServicePath)),
      internal_keyboard_(std::make_unique<InternalRgbKeyboard>()),
      rgb_keyboard_controller_(internal_keyboard_.get()),
      daemon_(daemon) {}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

void DBusAdaptor::GetRgbKeyboardCapabilities(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<uint32_t>>
        response) {
  const uint32_t capabilities =
      rgb_keyboard_controller_.GetRgbKeyboardCapabilities();
  response->Return(capabilities);

  // After we return capabilities we want to schedule the Daemon to quit.
  // DBusServiceDaemon runs tasks based on a sequential message loop so it is
  // guaranteed RgbkbdDaemon will exit only after all tasks are completed.
  if (capabilities == static_cast<uint32_t>(RgbKeyboardCapabilities::kNone)) {
    base::SequencedTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&RgbkbdDaemon::Quit, base::Unretained(daemon_)));
  }
}

void DBusAdaptor::SetCapsLockState(bool enabled) {
  rgb_keyboard_controller_.SetCapsLockState(enabled);
}

void DBusAdaptor::SetStaticBackgroundColor(uint8_t r, uint8_t g, uint8_t b) {
  rgb_keyboard_controller_.SetStaticBackgroundColor(r, g, b);
}

void DBusAdaptor::SetRainbowMode() {
  rgb_keyboard_controller_.SetRainbowMode();
}

void DBusAdaptor::SetTestingMode(bool enable_testing, uint32_t capability) {
  if (enable_testing) {
    if (!logger_keyboard_) {
      logger_keyboard_ = std::make_unique<KeyboardBacklightLogger>(
          base::FilePath(kLogFilePathForTesting));
    }
    rgb_keyboard_controller_.SetKeyboardClient(logger_keyboard_.get());
    rgb_keyboard_controller_.SetKeyboardCapabilityForTesting(
        static_cast<RgbKeyboardCapabilities>(capability));
    SendCapabilityUpdatedForTestingSignal(capability);
  } else {
    DCHECK(internal_keyboard_);
    rgb_keyboard_controller_.SetKeyboardClient(internal_keyboard_.get());
  }
}

// TODO(jimmyxgong): Implement switch case for different modes.
void DBusAdaptor::SetAnimationMode(uint32_t mode) {
  rgb_keyboard_controller_.SetAnimationMode(
      RgbAnimationMode::kBasicTestPattern);
}

RgbkbdDaemon::RgbkbdDaemon() : DBusServiceDaemon(kRgbkbdServiceName) {}

void RgbkbdDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_.reset(new DBusAdaptor(bus_, this));
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

}  // namespace rgbkbd
