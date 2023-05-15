// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/usb/usb_device_impl.h"

#include <memory>

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <libusb.h>

#include "lorgnette/ippusb_device.h"
#include "lorgnette/usb/usb_device.h"

namespace lorgnette {

UsbDeviceImpl::UsbDeviceImpl(libusb_device* device) : device_(device) {}

std::unique_ptr<UsbDeviceImpl> UsbDeviceImpl::Create(libusb_device* device) {
  UsbDeviceImpl* dev = new UsbDeviceImpl(device);
  dev->Init();
  return std::unique_ptr<UsbDeviceImpl>(dev);
}

std::optional<libusb_device_descriptor> UsbDeviceImpl::GetDeviceDescriptor()
    const {
  libusb_device_descriptor descriptor;
  int status = libusb_get_device_descriptor(device_, &descriptor);
  if (status < 0) {
    LOG(WARNING) << "Failed to get device descriptor: "
                 << libusb_error_name(status);
    return std::nullopt;
  }

  return descriptor;
}

UsbDevice::ScopedConfigDescriptor UsbDeviceImpl::GetConfigDescriptor(
    uint8_t num) const {
  libusb_config_descriptor* config;
  int status = libusb_get_config_descriptor(device_, num, &config);
  if (status < 0) {
    LOG(ERROR) << "Failed to get config descriptor " << num << " for device "
               << Description() << ": " << libusb_error_name(status);
    return ScopedConfigDescriptor(nullptr, &libusb_free_config_descriptor);
  }

  return ScopedConfigDescriptor(config, &libusb_free_config_descriptor);
}

}  // namespace lorgnette
