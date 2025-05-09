// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_limit_watcher.h"

#include <base/logging.h>

#include "typecd/utils.h"

namespace typecd {

UsbLimitWatcher::UsbLimitWatcher() : dbus_mgr_(nullptr), metrics_(nullptr) {}

void UsbLimitWatcher::OnUsbDeviceAdded() {
  // TODO(b/416716383): Add a check/notification for endpoint limit.
  if (dbus_mgr_ &&
      (GetUsbDeviceCount(base::FilePath(kUsbDeviceDir), kMTk8196UsbDeviceRe) >=
       kMTk8196DeviceLimit)) {
    LOG(WARNING) << "USB device limit reached.";
    dbus_mgr_->NotifyUsbLimit(UsbLimitType::kDeviceLimit);
    if (metrics_) {
      metrics_->ReportUsbLimit(UsbLimitMetric::kDeviceLimit);
    }
  }
}

}  // namespace typecd
