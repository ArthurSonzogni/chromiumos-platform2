// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/usb/usb_device.h"

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <libusb.h>

#include "lorgnette/ippusb_device.h"

namespace lorgnette {

std::string UsbDevice::Description() const {
  return vid_pid_;
}

void UsbDevice::Init() {
  auto descriptor = GetDeviceDescriptor();
  if (!descriptor) {
    return;
  }

  vid_ = descriptor->idVendor;
  pid_ = descriptor->idProduct;
  vid_pid_ = base::StringPrintf("%04x:%04x", vid_, pid_);
}

bool UsbDevice::SupportsIppUsb() const {
  auto maybe_descriptor = GetDeviceDescriptor();
  if (!maybe_descriptor.has_value()) {
    return false;
  }
  libusb_device_descriptor& descriptor = maybe_descriptor.value();

  // Printers always have a printer class interface defined.  They don't define
  // a top-level device class.
  if (descriptor.bDeviceClass != LIBUSB_CLASS_PER_INTERFACE) {
    return false;
  }

  bool isPrinter = false;
  bool isIppUsb = false;
  for (uint8_t c = 0; c < descriptor.bNumConfigurations; c++) {
    ScopedConfigDescriptor config = GetConfigDescriptor(c);
    if (!config) {
      continue;
    }

    isIppUsb = ContainsIppUsbInterface(config.get(), &isPrinter);

    if (isIppUsb) {
      break;
    }
  }
  if (isPrinter && !isIppUsb) {
    LOG(INFO) << "Device " << Description() << " is a printer without IPP-USB";
  }

  return isIppUsb;
}

bool UsbDevice::NeedsNonBundledBackend() const {
  // TODO(rishabhagr): Look up USB VID/PID somewhere and decide if this
  // device needs a DLC backend.
  return false;
}

}  // namespace lorgnette
