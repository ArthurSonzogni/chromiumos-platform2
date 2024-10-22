// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/usb/usb_device.h"

#include <map>
#include <string>

#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/constants/lorgnette_dlc.h>
#include <libusb.h>
#include <set>

#include "lorgnette/ippusb_device.h"
#include "lorgnette/scanner_match.h"

namespace lorgnette {

namespace {

const char kScannerTypeMFP[] = "multi-function peripheral";  // Matches SANE.

// Scanners requiring the sane-backends-pfu DLC.
std::set<VidPid> kScannersRequiringSaneBackendsPfuDlc = {
    {0x04c5, 0x132e}, {0x04c5, 0x15fc}, {0x04c5, 0x15ff}, {0x05ca, 0x0307}};

std::set<VidPid> kScannerRequiringSaneBackendsCanonDlc={{0x1083, 0x165f},{0x1083, 0x166d}};
// Creates a new key in `map` for each scanner in `scanners`, with the value
// `id`.
void SetScannerIds(const std::set<VidPid>& scanners,
                   const std::string& id,
                   std::map<VidPid, std::string>* map) {
  DCHECK(map);
  for (const auto& vidpid : scanners) {
    auto itr = map->find(vidpid);
    DCHECK(itr == map->end());
    (*map)[vidpid] = id;
  }
}

}  // namespace

UsbDevice::UsbDevice() {
  SetScannerIds(kScannersRequiringSaneBackendsPfuDlc, kSaneBackendsPfuDlcId,
                &default_dlc_backend_scanners_);
  SetScannerIds(kScannerRequiringSaneBackendsCanonDlc, kSaneBackendsCanonDlcId,
                &default_dlc_backend_scanners_);
  dlc_backend_scanners_ = &default_dlc_backend_scanners_;
}

uint16_t UsbDevice::GetVid() const {
  return vid_;
}

uint16_t UsbDevice::GetPid() const {
  return pid_;
}

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

std::string UsbDevice::GetSerialNumber() {
  auto maybe_descriptor = GetDeviceDescriptor();
  if (!maybe_descriptor.has_value()) {
    return "";
  }
  libusb_device_descriptor& descriptor = maybe_descriptor.value();

  if (descriptor.iSerialNumber == 0) {
    // A valid serial number string descriptor must be at index 1 or later.
    return "";
  }

  auto serial = GetStringDescriptor(descriptor.iSerialNumber);
  if (!serial.has_value() || serial->empty()) {
    LOG(ERROR) << "Device " << Description() << " is missing serial number";
    return "";
  }

  return serial.value();
}

std::optional<ScannerInfo> UsbDevice::IppUsbScannerInfo() {
  auto maybe_descriptor = GetDeviceDescriptor();
  if (!maybe_descriptor.has_value()) {
    return std::nullopt;
  }
  libusb_device_descriptor& descriptor = maybe_descriptor.value();

  auto mfgr_name = GetStringDescriptor(descriptor.iManufacturer);
  if (!mfgr_name.has_value() || mfgr_name->empty()) {
    LOG(ERROR) << "Device " << Description() << " is missing manufacturer";
    return std::nullopt;
  }

  auto model_name = GetStringDescriptor(descriptor.iProduct);
  if (!model_name.has_value() || model_name->empty()) {
    LOG(ERROR) << "Device " << Description() << " is missing product";
    return std::nullopt;
  }

  std::string printer_name;
  if (base::StartsWith(*model_name, *mfgr_name,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    printer_name = *model_name;
  } else {
    printer_name = *mfgr_name + " " + *model_name;
  }

  std::string device_name = base::StringPrintf(
      "ippusb:escl:%s:%04x_%04x/eSCL/", printer_name.c_str(), vid_, pid_);
  ScannerInfo info;
  info.set_name(device_name);
  info.set_manufacturer(*mfgr_name);
  info.set_model(*model_name);
  info.set_type(kScannerTypeMFP);  // Printer that can scan == MFP.
  info.set_connection_type(lorgnette::CONNECTION_USB);
  info.set_secure(true);
  info.set_protocol_type(ProtocolTypeForScanner(info));
  info.set_display_name(DisplayNameForScanner(info));
  return info;
}

std::map<VidPid, std::string>* UsbDevice::GetDlcBackendScanners() {
  return dlc_backend_scanners_;
}

void UsbDevice::SetDlcBackendScanners(
    std::map<VidPid, std::string>* dlc_backend_scanners) {
  CHECK(dlc_backend_scanners);
  dlc_backend_scanners_ = dlc_backend_scanners;
}

std::optional<std::string> UsbDevice::GetNonBundledBackendId() const {
  VidPid curr_device = {GetVid(), GetPid()};
  auto itr = dlc_backend_scanners_->find(curr_device);
  if (itr == dlc_backend_scanners_->end()) {
    return std::nullopt;
  }
  return itr->second;
}

}  // namespace lorgnette
