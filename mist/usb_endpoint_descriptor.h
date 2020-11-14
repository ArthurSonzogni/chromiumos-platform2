// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_USB_ENDPOINT_DESCRIPTOR_H_
#define MIST_USB_ENDPOINT_DESCRIPTOR_H_

#include <stdint.h>

#include <ostream>  // NOLINT(readability/streams)
#include <string>

#include <base/macros.h>

#include "mist/usb_constants.h"

struct libusb_endpoint_descriptor;

namespace mist {

// A USB endpoint descriptor, which wraps a libusb_endpoint_descriptor C struct
// from libusb 1.0 into a C++ object.
class UsbEndpointDescriptor {
 public:
  // Constructs a UsbEndpointDescriptor object by taking a raw pointer to a
  // libusb_endpoint_descriptor struct as |endpoint_descriptor|. The ownership
  // of |endpoint_descriptor| is not transferred, and thus it should outlive
  // this object.
  explicit UsbEndpointDescriptor(
      const libusb_endpoint_descriptor* endpoint_descriptor);
  UsbEndpointDescriptor(const UsbEndpointDescriptor&) = delete;
  UsbEndpointDescriptor& operator=(const UsbEndpointDescriptor&) = delete;

  ~UsbEndpointDescriptor() = default;

  // Getters for retrieving fields of the libusb_endpoint_descriptor struct.
  uint8_t GetLength() const;
  uint8_t GetDescriptorType() const;
  uint8_t GetEndpointAddress() const;
  uint8_t GetEndpointNumber() const;
  uint8_t GetAttributes() const;
  uint16_t GetMaxPacketSize() const;
  uint8_t GetInterval() const;
  UsbDirection GetDirection() const;
  UsbTransferType GetTransferType() const;

  // Returns a string describing the properties of this object for logging
  // purpose.
  std::string ToString() const;

 private:
  const libusb_endpoint_descriptor* const endpoint_descriptor_;
};

}  // namespace mist

// Output stream operator provided to facilitate logging.
std::ostream& operator<<(
    std::ostream& stream,
    const mist::UsbEndpointDescriptor& endpoint_descriptor);

#endif  // MIST_USB_ENDPOINT_DESCRIPTOR_H_
