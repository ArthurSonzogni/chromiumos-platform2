// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_USB_USB_DEVICE_FAKE_H_
#define LORGNETTE_USB_USB_DEVICE_FAKE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <libusb.h>

#include "lorgnette/usb/usb_device.h"

namespace lorgnette {

class UsbDeviceFake : public UsbDevice {
 public:
  UsbDeviceFake();
  UsbDeviceFake(const UsbDeviceFake&) = delete;
  UsbDeviceFake& operator=(const UsbDeviceFake&) = delete;
  UsbDeviceFake(UsbDeviceFake&&) = default;
  UsbDeviceFake& operator=(UsbDeviceFake&&) = default;
  ~UsbDeviceFake() override;

  static std::unique_ptr<UsbDeviceFake> Clone(UsbDevice& source);

  std::optional<libusb_device_descriptor> GetDeviceDescriptor() const override;
  ScopedConfigDescriptor GetConfigDescriptor(uint8_t config) const override;
  std::optional<std::string> GetStringDescriptor(uint8_t index) override;

  void SetDeviceDescriptor(const libusb_device_descriptor& descriptor);
  libusb_device_descriptor& MutableDeviceDescriptor();
  void SetConfigDescriptors(std::vector<libusb_config_descriptor> descriptors);
  libusb_config_descriptor& MutableConfigDescriptor(uint8_t index);
  void SetStringDescriptors(std::vector<std::string> strings);
  uint8_t GetBusNumber() const override;
  uint8_t GetDeviceAddress() const override;
  void SetBusNumber(uint8_t bus);
  void SetDeviceAddress(uint8_t device_address);

 private:
  std::optional<libusb_device_descriptor> device_descriptor_;
  std::vector<libusb_config_descriptor> config_descriptors_;
  std::vector<std::string> string_descriptors_{""};
  uint8_t bus_{0};
  uint8_t device_address_{0};
};

}  // namespace lorgnette

#endif  // LORGNETTE_USB_USB_DEVICE_FAKE_H_
