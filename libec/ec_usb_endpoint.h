// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_EC_USB_ENDPOINT_H_
#define LIBEC_EC_USB_ENDPOINT_H_

#include <brillo/brillo_export.h>
#include <libusb-1.0/libusb.h>
#include <memory>
#include <utility>

#include "libec/libusb_wrapper.h"

namespace ec {

struct usb_endpoint {
  struct libusb_device_handle* dev_handle = nullptr;
  int interface_number = 0;
  uint8_t address = 0;
  int chunk_len = 0;
};

class EcUsbEndpointInterface {
 public:
  virtual ~EcUsbEndpointInterface() {}

  virtual bool Init(uint16_t vid, uint16_t pid) = 0;
  virtual const struct usb_endpoint& GetEndpointPtr() = 0;
  virtual bool ClaimInterface() = 0;
  virtual bool ReleaseInterface() = 0;

 private:
  std::unique_ptr<LibusbWrapper> libusb_;
  struct usb_endpoint endpoint_;
  virtual libusb_device_handle* CheckDevice(libusb_device* dev,
                                            uint16_t vid,
                                            uint16_t pid) = 0;
  virtual int FindInterfaceWithEndpoint(struct usb_endpoint* uep) = 0;
};

class BRILLO_EXPORT EcUsbEndpoint : public EcUsbEndpointInterface {
 public:
  EcUsbEndpoint() : EcUsbEndpoint(std::make_unique<LibusbWrapper>()) {}
  explicit EcUsbEndpoint(std::unique_ptr<LibusbWrapper> libusb)
      : libusb_(std::move(libusb)) {}
  ~EcUsbEndpoint();

  bool Init(uint16_t vid, uint16_t pid);
  const struct usb_endpoint& GetEndpointPtr();
  bool ClaimInterface();
  bool ReleaseInterface();

 private:
  std::unique_ptr<LibusbWrapper> libusb_;
  struct usb_endpoint endpoint_;
  libusb_device_handle* CheckDevice(libusb_device* dev,
                                    uint16_t vid,
                                    uint16_t pid);
  int FindInterfaceWithEndpoint(struct usb_endpoint* uep);
};

class BRILLO_EXPORT EcUsbEndpointStub : public EcUsbEndpointInterface {
 public:
  ~EcUsbEndpointStub() {}

  bool Init(uint16_t vid, uint16_t pid) { return true; }
  const struct usb_endpoint& GetEndpointPtr() { return endpoint_; }
  bool ClaimInterface() { return true; }
  bool ReleaseInterface() { return true; }

 private:
  std::unique_ptr<LibusbWrapper> libusb_;
  struct usb_endpoint endpoint_;
  libusb_device_handle* CheckDevice(libusb_device* dev,
                                    uint16_t vid,
                                    uint16_t pid) {
    return nullptr;
  }
  int FindInterfaceWithEndpoint(struct usb_endpoint* uep) { return 0; }
};

}  // namespace ec

#endif  // LIBEC_EC_USB_ENDPOINT_H_
