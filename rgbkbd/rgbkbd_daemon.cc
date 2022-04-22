// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/rgbkbd_daemon.h"

#include <memory>
#include <utility>

#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/rgbkbd/dbus-constants.h>

#include "base/check.h"
#include "rgbkbd/internal_rgb_keyboard.h"
#include "rgbkbd/keyboard_backlight_logger.h"

namespace rgbkbd {
DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::RgbkbdAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kRgbkbdServicePath)),
      internal_keyboard_(std::make_unique<InternalRgbKeyboard>()),
      rgb_keyboard_controller_(internal_keyboard_.get()) {}

void DBusAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

uint32_t DBusAdaptor::GetRgbKeyboardCapabilities() {
  // TODO(michaelcheco): Shutdown DBus service if the keyboard is not
  // supported.
  return rgb_keyboard_controller_.GetRgbKeyboardCapabilities();
}

void DBusAdaptor::SetCapsLockState(bool enabled) {
  rgb_keyboard_controller_.SetCapsLockState(enabled);
}

void DBusAdaptor::SetStaticBackgroundColor(uint32_t r, uint32_t g, uint32_t b) {
  rgb_keyboard_controller_.SetStaticBackgroundColor(r, g, b);
}

void DBusAdaptor::SetRainbowMode() {
  rgb_keyboard_controller_.SetRainbowMode();
}

void DBusAdaptor::SetTestingMode(bool enable_testing) {
  if (enable_testing) {
    if (!logger_keyboard_) {
      logger_keyboard_ = std::make_unique<KeyboardBacklightLogger>();
    }
    rgb_keyboard_controller_.SetKeyboardClient(logger_keyboard_.get());
  } else {
    DCHECK(internal_keyboard_);
    rgb_keyboard_controller_.SetKeyboardClient(internal_keyboard_.get());
  }
}

RgbkbdDaemon::RgbkbdDaemon() : DBusServiceDaemon(kRgbkbdServiceName) {}

void RgbkbdDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_.reset(new DBusAdaptor(bus_));
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

}  // namespace rgbkbd
