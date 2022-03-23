// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_device.h"

#include <base/logging.h>

namespace typecd {

UsbDevice::UsbDevice(
    int busnum, int devnum, std::string hub, UsbSpeed speed, UsbVersion version)
    : busnum_(busnum),
      devnum_(devnum),
      typec_port_num_(-1),
      hub_(hub),
      speed_(speed),
      device_class_(UsbDeviceClass::kOther),
      version_(version),
      metrics_reported_(false) {
  LOG(INFO) << "USB device " << hub_ << " enumerated.";
}

void UsbDevice::ReportMetrics(Metrics* metrics) {
  if (!metrics || metrics_reported_)
    return;

  // Report metrics only on USB devices that is connected to a Type C ports.
  if (typec_port_num_ == -1)
    return;

  // Do not collect speed metrics on a hub since we want to collect speed data
  // on the USB devices that connect to a hub.
  if (device_class_ != UsbDeviceClass::kHub)
    metrics->ReportUsbDeviceSpeed(GetUsbDeviceSpeedMetric());

  metrics_reported_ = true;
}

UsbDeviceSpeedMetric UsbDevice::GetUsbDeviceSpeedMetric() {
  switch (speed_) {
    case UsbSpeed::k1_5:
      return UsbDeviceSpeedMetric::k1_5;
    case UsbSpeed::k12:
      return UsbDeviceSpeedMetric::k12;
    case UsbSpeed::k480:
      if (version_ == UsbVersion::k2_1)
        return UsbDeviceSpeedMetric::k480Fallback;
      else
        return UsbDeviceSpeedMetric::k480;
    case UsbSpeed::k5000:
      return UsbDeviceSpeedMetric::k5000;
    case UsbSpeed::k10000:
      return UsbDeviceSpeedMetric::k10000;
    case UsbSpeed::k20000:
      return UsbDeviceSpeedMetric::k20000;
    default:
      return UsbDeviceSpeedMetric::kOther;
  }
}

}  // namespace typecd
