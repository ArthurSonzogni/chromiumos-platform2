// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_RGBKBD_DAEMON_H_
#define RGBKBD_RGBKBD_DAEMON_H_

#include <cstdint>
#include <memory>

#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_method_response.h>
#include <dbus/rgbkbd/dbus-constants.h>
#include <libec/ec_usb_device_monitor.h>

#include "rgbkbd/dbus_adaptors/org.chromium.Rgbkbd.h"
#include "rgbkbd/internal_rgb_keyboard.h"
#include "rgbkbd/keyboard_backlight_logger.h"
#include "rgbkbd/rgb_keyboard_controller_impl.h"

namespace rgbkbd {

class RgbkbdDaemon;

class DBusAdaptor : public org::chromium::RgbkbdInterface,
                    public org::chromium::RgbkbdAdaptor,
                    public ec::EcUsbDeviceMonitor::Observer {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus, RgbkbdDaemon* daemon);
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;

  ~DBusAdaptor() override = default;

  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  void GetRgbKeyboardCapabilities(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<uint32_t>>
          response) override;
  void SetCapsLockState(bool enabled) override;
  void SetStaticBackgroundColor(uint8_t r, uint8_t g, uint8_t b) override;
  void SetRainbowMode() override;
  void SetTestingMode(bool enable_testing, uint32_t capability) override;
  void SetAnimationMode(uint32_t mode) override;

  // From ec::EcUsbDeviceMonitor::Observer
  void OnDeviceReconnected() override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;
  std::unique_ptr<InternalRgbKeyboard> internal_keyboard_;
  std::unique_ptr<KeyboardBacklightLogger> logger_keyboard_;
  RgbKeyboardControllerImpl rgb_keyboard_controller_;
  RgbkbdDaemon* daemon_;
};

class RgbkbdDaemon : public brillo::DBusServiceDaemon {
 public:
  RgbkbdDaemon();
  RgbkbdDaemon(const RgbkbdDaemon&) = delete;
  RgbkbdDaemon& operator=(const RgbkbdDaemon&) = delete;
  ~RgbkbdDaemon() override;

  void RegisterUsbDeviceMonitor();

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  std::unique_ptr<ec::EcUsbDeviceMonitor> ec_usb_device_monitor_;
  std::unique_ptr<DBusAdaptor> adaptor_;
};

}  // namespace rgbkbd

#endif  // RGBKBD_RGBKBD_DAEMON_H_
