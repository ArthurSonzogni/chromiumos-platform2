// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_USB_USB_DEVICE_H_
#define LORGNETTE_USB_USB_DEVICE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <libusb.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

namespace lorgnette {

using VidPid = std::pair<uint16_t, uint16_t>;

class UsbDevice {
 public:
  UsbDevice();
  UsbDevice(const UsbDevice&) = delete;
  UsbDevice& operator=(const UsbDevice&) = delete;
  UsbDevice(UsbDevice&&) = default;
  UsbDevice& operator=(UsbDevice&&) = default;
  virtual ~UsbDevice() = default;

  using ScopedConfigDescriptor =
      std::unique_ptr<libusb_config_descriptor,
                      void (*)(libusb_config_descriptor*)>;

  // Equivalent of `libusb_get_device_descriptor`.
  virtual std::optional<libusb_device_descriptor> GetDeviceDescriptor()
      const = 0;

  // Equivalent of `libusb_get_config_descriptor`.  The returned object will
  // clean itself up and must not be passed to `libusb_free_config_descriptor`.
  virtual ScopedConfigDescriptor GetConfigDescriptor(uint8_t config) const = 0;

  // Equivalent of `libusb_get_string_descriptor_ascii`.
  virtual std::optional<std::string> GetStringDescriptor(uint8_t index) = 0;

  // Equivalent of `libusb_get_bus_number`.
  virtual uint8_t GetBusNumber() const = 0;

  // Equivalent of `libusb_get_device_address`.
  virtual uint8_t GetDeviceAddress() const = 0;

  // Returns `vid_`.
  uint16_t GetVid() const;

  // Returns `pid_`.
  uint16_t GetPid() const;

  // Returns a description of this device that can be used for logging.
  std::string Description() const;

  // Returns the device serial number if available.  Returns a blank string if
  // the serial number is empty or an error occurs.
  std::string GetSerialNumber();

  // Constructors can't call virtual functions.  This does equivalent setup, but
  // can be called after the object is created.  Must be called before calling
  // the other non-virtual functions.
  void Init();

  // Returns true if this device contains a printer class interface that
  // supports the appropriate IPP-USB protocol.
  bool SupportsIppUsb() const;

  // Returns a populated ScannerInfo struct as if this device were an eSCL over
  // IPP-USB scanner.  The returned name will only work if the device actually
  // does support eSCL through its IPP-USB interface.
  std::optional<ScannerInfo> IppUsbScannerInfo();

  // Returns true if this device needs to have a backend downloaded with DLC
  // before it will be recognized by `sane_get_devices`.
  bool NeedsNonBundledBackend() const;

  // Setter for `dlc_backend_scanners_`
  // Pointer passed in can not be nullptr and needs to outlive this object
  void SetDlcBackendScanners(std::set<VidPid>* dlc_backend_scanners);

  // Returns the current set of VidPid pairs that need DLC for their backend
  std::set<VidPid>* GetDlcBackendScanners();

 private:
  uint16_t vid_;
  uint16_t pid_;
  std::string vid_pid_;  // Cached copy of formatted VID:PID for logging.

  // Pointer to the list of all scanners that need a the DLC backend.
  // Default value is set to the real scanners that need the DLC backend.
  // Not owned.
  std::set<VidPid>* dlc_backend_scanners_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_USB_USB_DEVICE_H_
