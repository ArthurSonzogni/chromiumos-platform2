// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_USB_MONITOR_H_
#define TYPECD_USB_MONITOR_H_

#include <map>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>

#include "typecd/metrics.h"
#include "typecd/udev_monitor.h"
#include "typecd/usb_device.h"

namespace typecd {

// This class is used to manage connected USB devices.
class UsbMonitor : public UdevMonitor::UsbObserver {
 public:
  UsbMonitor() = default;

  void SetMetrics(Metrics* metrics) { metrics_ = metrics; }

 private:
  friend class UsbMonitorFuzzer;
  friend class UsbMonitorTest;
  FRIEND_TEST(UsbMonitorTest, TestDeviceAddAndRemove);
  FRIEND_TEST(UsbMonitorTest, TestInvalidUeventPath);
  FRIEND_TEST(UsbMonitorTest, TestNoTypecPort);
  FRIEND_TEST(UsbMonitorTest, TestDeviceTree);
  FRIEND_TEST(UsbMonitorTest, TestInvalidVersion);

  // UdevMonitor::UsbObserver overrides
  void OnDeviceAddedOrRemoved(const base::FilePath& path, bool added) override;

  // Given sysfs directory name as a key, returns corresponding UsbDevice from
  // devices_. If there is none, return nullptr.
  UsbDevice* GetDevice(std::string key);

  // Central function to perform metrics reporting
  void ReportMetrics(const base::FilePath& path, std::string key);

  // key: USB root hub and hub port numbers in the final path component of the
  // sysfs directory path. (e.g. 2-1 if sysfs path is /sys/bus/usb/devices/2-1)
  // value: USB device associated with the sysfs directory.
  std::map<std::string, std::unique_ptr<UsbDevice>> devices_;

  // Pointer to the metrics reporting class. NOTE: This is owned by the parent
  // Daemon, and not UsbMonitor.
  Metrics* metrics_;
};

}  // namespace typecd

#endif  // TYPECD_USB_MONITOR_H_
