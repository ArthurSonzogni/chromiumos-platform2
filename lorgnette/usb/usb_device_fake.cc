// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/usb/usb_device_fake.h"

#include <cstdlib>
#include <cstring>
#include <utility>

#include <base/check.h>

namespace lorgnette {

UsbDeviceFake::UsbDeviceFake() = default;

UsbDeviceFake::~UsbDeviceFake() = default;

std::optional<libusb_device_descriptor> UsbDeviceFake::GetDeviceDescriptor()
    const {
  return device_descriptor_;
}

UsbDevice::ScopedConfigDescriptor UsbDeviceFake::GetConfigDescriptor(
    uint8_t config) const {
  if (config > config_descriptors_.size()) {
    return ScopedConfigDescriptor(nullptr, nullptr);
  }

  // The caller will expect to have a non-const copy, so return a copy of the
  // struct instead of a pointer.  Don't deep copy any of the inner pointers
  // because the free function doesn't clean them up.
  const libusb_config_descriptor& in = config_descriptors_[config];
  CHECK(in.wTotalLength >= sizeof(libusb_config_descriptor));
  libusb_config_descriptor* out =
      reinterpret_cast<libusb_config_descriptor*>(malloc(in.wTotalLength));
  memcpy(out, &in, in.wTotalLength);
  return ScopedConfigDescriptor(out,
                                [](libusb_config_descriptor* d) { free(d); });
}

void UsbDeviceFake::SetDeviceDescriptor(
    const libusb_device_descriptor& descriptor) {
  device_descriptor_ = descriptor;
}

void UsbDeviceFake::SetConfigDescriptors(
    std::vector<libusb_config_descriptor> descriptors) {
  config_descriptors_ = std::move(descriptors);
}

}  // namespace lorgnette
