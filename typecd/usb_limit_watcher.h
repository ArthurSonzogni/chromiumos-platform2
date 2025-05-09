// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_USB_LIMIT_WATCHER_H_
#define TYPECD_USB_LIMIT_WATCHER_H_

#include "typecd/dbus_manager.h"
#include "typecd/udev_monitor.h"

namespace typecd {

constexpr char kUsbDeviceDir[] = "sys/bus/usb/devices";
constexpr char kMTk8196UsbDeviceRe[] = "[2-3]\\-[\\d\\.]+";
constexpr int kMTk8196DeviceLimit = 15;

class UsbLimitWatcher : public UdevMonitor::UsbObserver {
 public:
  UsbLimitWatcher();
  UsbLimitWatcher(const UsbLimitWatcher&) = delete;
  UsbLimitWatcher& operator=(const UsbLimitWatcher&) = delete;

  void SetDBusManager(DBusManager* mgr) { dbus_mgr_ = mgr; }

 private:
  friend class UsbLimitWatcherTest;
  FRIEND_TEST(UsbLimitWatcherTest, UsbDeviceLimitReached);

  void OnUsbDeviceAdded() override;
  DBusManager* dbus_mgr_;
};

}  // namespace typecd

#endif  // TYPECD_USB_LIMIT_WATCHER_H_
