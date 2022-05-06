// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_RGBKBD_DAEMON_H_
#define RGBKBD_RGBKBD_DAEMON_H_

#include <cstdint>
#include <memory>

#include <brillo/daemons/dbus_daemon.h>
#include <dbus/rgbkbd/dbus-constants.h>

#include "rgbkbd/dbus_adaptors/org.chromium.Rgbkbd.h"
#include "rgbkbd/internal_rgb_keyboard.h"
#include "rgbkbd/keyboard_backlight_logger.h"
#include "rgbkbd/rgb_keyboard_controller_impl.h"

namespace rgbkbd {

class DBusAdaptor : public org::chromium::RgbkbdInterface,
                    public org::chromium::RgbkbdAdaptor {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  ~DBusAdaptor() override = default;

  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

  uint32_t GetRgbKeyboardCapabilities() override;
  void SetCapsLockState(bool enabled) override;
  void SetStaticBackgroundColor(uint32_t r, uint32_t g, uint32_t b) override;
  void SetRainbowMode() override;
  void SetTestingMode(bool enable_testing) override;
  void SetAnimationMode(uint32_t mode) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
  std::unique_ptr<InternalRgbKeyboard> internal_keyboard_;
  std::unique_ptr<KeyboardBacklightLogger> logger_keyboard_;
  RgbKeyboardControllerImpl rgb_keyboard_controller_;
};

class RgbkbdDaemon : public brillo::DBusServiceDaemon {
 public:
  RgbkbdDaemon();
  RgbkbdDaemon(const RgbkbdDaemon&) = delete;
  RgbkbdDaemon& operator=(const RgbkbdDaemon&) = delete;

  ~RgbkbdDaemon() override = default;

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  std::unique_ptr<DBusAdaptor> adaptor_;
};

}  // namespace rgbkbd

#endif  // RGBKBD_RGBKBD_DAEMON_H_
