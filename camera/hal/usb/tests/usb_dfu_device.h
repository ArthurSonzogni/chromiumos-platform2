/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_TESTS_USB_DFU_DEVICE_H_
#define CAMERA_HAL_USB_TESTS_USB_DFU_DEVICE_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include <base/containers/span.h>
#include <base/optional.h>

struct libusb_context;
struct libusb_device;
struct libusb_device_handle;
struct libusb_device_descriptor;
struct libusb_interface_descriptor;

namespace cros {

// DFU attribute bit definitions in the DFU functional descriptor.
enum DfuAttributeBits : uint8_t {
  kCanDownload = 1 << 0,
  kCanUpload = 1 << 1,
  kManifestationTolerant = 1 << 2,
  kWillDetach = 1 << 3,
};

// Data from DFU_GETSTATUS request.
struct DfuStatus {
  uint8_t status;
  uint8_t state;
  uint32_t poll_timeout;
};

// Wrapper over USB DFU operations on a device with libusb.
class UsbDfuDevice {
 public:
  explicit UsbDfuDevice(libusb_device_handle* handle,
                        const libusb_device_descriptor& dev_desc,
                        const libusb_interface_descriptor& intf_desc);
  ~UsbDfuDevice();

  UsbDfuDevice(const UsbDfuDevice&) = delete;
  UsbDfuDevice(UsbDfuDevice&&) = delete;
  UsbDfuDevice& operator=(const UsbDfuDevice&) = delete;
  UsbDfuDevice& operator=(UsbDfuDevice&&) = delete;

  // Send DFU_DETACH request to the device, and reset the device if
  // bitWillDetach attribute is not set. On success, the underlying device
  // handle becomes invalid.
  bool Detach() const;

  // Send DFU_DNLOAD requests repeatedly until |firmware| is sent, and wait for
  // manifestation phase completed.
  bool Download(base::span<const unsigned char> firmware) const;

  // Send DFU_UPLOAD requests repeatedly until all the firmware blocks are
  // transferred. Return the firmware image.
  std::vector<unsigned char> Upload() const;

  // Issue a USB bus reset to the device. On success, the underlying device
  // handle becomes invalid.
  bool Reset() const;

  uint16_t bcd_device() const { return bcd_device_; }
  bool is_dfu_mode() const { return is_dfu_mode_; }
  uint8_t attributes() const { return attributes_; }

 private:
  // Send DFU_GETSTATUS request to device.
  base::Optional<DfuStatus> GetStatus() const;

  // Send DFU_GETSTATE request to device.
  base::Optional<uint8_t> GetState() const;

  // In dfuDNLOAD-SYNC state (after a DFU_DNLOAD request is sent), send
  // DFU_GETSTATUS requests repeatedly until device enters dfuDNLOAD-IDLE state.
  bool SyncDownload() const;

  // In dfuMANIFEST-SYNC state (after the final zero-length DFU_DNLOAD request
  // is sent), send DFU_GETSTATUS requests repeatedly until device enters
  // dfuIDLE state.
  bool SyncManifest() const;

  libusb_device_handle* handle_;
  uint16_t bcd_device_;
  bool is_dfu_mode_;
  uint16_t interface_number_;
  uint8_t attributes_;
  uint16_t detach_timeout_;
  uint16_t transfer_size_;
};

// Wrapper over libusb context operations.
class UsbContext {
 public:
  static std::unique_ptr<UsbContext> Create();

  explicit UsbContext(libusb_context* ctx) : ctx_(ctx) {}
  ~UsbContext();

  UsbContext(const UsbContext&) = delete;
  UsbContext(UsbContext&&) = delete;
  UsbContext& operator=(const UsbContext&) = delete;
  UsbContext& operator=(UsbContext&&) = delete;

  // Create a UsbDfuDevice with matching VID:PID if present.
  std::unique_ptr<UsbDfuDevice> CreateUsbDfuDevice(uint16_t vid, uint16_t pid);

 private:
  libusb_context* ctx_;
};

}  // namespace cros

#endif  // CAMERA_HAL_USB_TESTS_USB_DFU_DEVICE_H_
