// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_USB_USB_DEVICE_FAKE_H_
#define LORGNETTE_USB_USB_DEVICE_FAKE_H_

#include <optional>
#include <vector>

#include <libusb.h>

#include "lorgnette/usb/usb_device.h"

namespace lorgnette {

class UsbDeviceFake : public UsbDevice {
 public:
  UsbDeviceFake();
  ~UsbDeviceFake() override;

  std::optional<libusb_device_descriptor> GetDeviceDescriptor() const override;
  ScopedConfigDescriptor GetConfigDescriptor(uint8_t config) const override;

  void SetDeviceDescriptor(const libusb_device_descriptor& descriptor);
  void SetConfigDescriptors(std::vector<libusb_config_descriptor> descriptors);

 private:
  std::optional<libusb_device_descriptor> device_descriptor_;
  std::vector<libusb_config_descriptor> config_descriptors_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_USB_USB_DEVICE_FAKE_H_
