// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/ippusb_device.h"

#include <memory>

#include <libusb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>
#include <re2/re2.h>

namespace lorgnette {

namespace {

const char kIppUsbSocketDir[] = "/run/ippusb";
const base::TimeDelta kSocketCreationTimeout = base::Seconds(3);
const char kScannerTypeMFP[] = "multi-function peripheral";  // Matches SANE.
const uint8_t kIppUsbInterfaceProtocol = 0x04;

// Wait for |sock_name| to appear in kIppUsbSocketDir.  Return true if that
// happens, or false if the socket doesn't appear within |timeout|.
bool WaitForSocket(const std::string& sock_name, base::TimeDelta timeout) {
  base::FilePath socket_path(kIppUsbSocketDir);
  socket_path = socket_path.Append(sock_name);
  LOG(INFO) << "Waiting for socket " << socket_path;

  base::ElapsedTimer timer;
  while (!base::PathExists(socket_path)) {
    if (timer.Elapsed() > timeout) {
      LOG(ERROR) << "Timed out waiting for socket " << socket_path;
      return false;
    }

    base::PlatformThread::Sleep(base::Milliseconds(10));
  }

  return true;
}

std::string VidPid(const libusb_device_descriptor& descriptor) {
  return base::StringPrintf("%04x:%04x", descriptor.idVendor,
                            descriptor.idProduct);
}

// Loop through all altsettings for all interfaces in |config| and return true
// if any is a printer interface class that implements the IPP-USB protocol.
// Also sets |isPrinter| to true if any interface has the printer class
// regardless of whether it supports IPP-USB.
bool ContainsIppUsbInterface(const libusb_config_descriptor* config,
                             bool* isPrinter) {
  for (uint8_t i = 0; i < config->bNumInterfaces; i++) {
    for (uint8_t j = 0; j < config->interface[i].num_altsetting; j++) {
      const libusb_interface_descriptor* interface =
          &config->interface[i].altsetting[j];

      if (interface->bInterfaceClass != LIBUSB_CLASS_PRINTER) {
        continue;
      }

      *isPrinter = true;
      if (interface->bInterfaceProtocol == kIppUsbInterfaceProtocol) {
        return true;
      }
    }
  }
  return false;
}

// Create a ScannerInfo protobuf describing |device|, which is presumed to be an
// IPP-USB capable printer.  The resulting |device_name| member will claim escl
// support through the ippusb backend, but this function will not check for
// proper support.  The caller must connect to the device and probe it before
// attempting to scan.
base::Optional<ScannerInfo> ScannerInfoForDevice(
    libusb_device* device, const libusb_device_descriptor& descriptor) {
  const std::string vid_pid = VidPid(descriptor);

  libusb_device_handle* h;
  int status = libusb_open(device, &h);
  if (status < 0) {
    LOG(ERROR) << "Failed to open device " << vid_pid << ": "
               << libusb_error_name(status);
    return base::nullopt;
  }
  auto handle = std::unique_ptr<libusb_device_handle, decltype(&libusb_close)>(
      h, libusb_close);

  std::vector<uint8_t> buf(256);
  int bytes = libusb_get_string_descriptor_ascii(
      handle.get(), descriptor.iManufacturer, buf.data(), buf.size());
  if (bytes < 0) {
    LOG(ERROR) << "Failed to read manufacturer from device " << vid_pid << ": "
               << libusb_error_name(bytes);
    return base::nullopt;
  }
  std::string mfgr_name((const char*)buf.data(), bytes);

  bytes = libusb_get_string_descriptor_ascii(handle.get(), descriptor.iProduct,
                                             buf.data(), buf.size());
  if (bytes < 0) {
    LOG(ERROR) << "Failed to read product name from device " << vid_pid << ": "
               << libusb_error_name(bytes);
    return base::nullopt;
  }
  std::string model_name((const char*)buf.data(), bytes);

  std::string printer_name;
  if (base::StartsWith(model_name, mfgr_name,
                       base::CompareCase::INSENSITIVE_ASCII)) {
    printer_name = model_name;
  } else {
    printer_name = mfgr_name + " " + model_name;
  }

  std::string device_name =
      base::StringPrintf("ippusb:escl:%s:%04x_%04x/eSCL/", printer_name.c_str(),
                         descriptor.idVendor, descriptor.idProduct);
  LOG(INFO) << "Adding " << device_name << " to possible IPP-USB scanners.";
  ScannerInfo info;
  info.set_name(device_name);
  info.set_manufacturer(mfgr_name);
  info.set_model(model_name);
  info.set_type(kScannerTypeMFP);  // Printer that can scan == MFP.
  return info;
}

// Check if |device| is a printer that supports IPP-USB and return a ScannerInfo
// proto if it is.
base::Optional<ScannerInfo> CheckUsbDevice(libusb_device* device) {
  libusb_device_descriptor descriptor;
  int status = libusb_get_device_descriptor(device, &descriptor);
  if (status < 0) {
    LOG(WARNING) << "Failed to get device descriptor: "
                 << libusb_error_name(status);
    return base::nullopt;
  }
  const std::string vid_pid = VidPid(descriptor);

  // Printers always have a printer class interface defined.  They don't define
  // a top-level device class.
  if (descriptor.bDeviceClass != LIBUSB_CLASS_PER_INTERFACE) {
    return base::nullopt;
  }

  bool isPrinter = false;
  bool isIppUsb = false;
  for (uint8_t c = 0; c < descriptor.bNumConfigurations; c++) {
    libusb_config_descriptor* config;
    status = libusb_get_config_descriptor(device, c, &config);
    if (status < 0) {
      LOG(ERROR) << "Failed to get config descriptor " << c << " for device "
                 << vid_pid << ": " << libusb_error_name(status);
      continue;
    }

    isIppUsb = ContainsIppUsbInterface(config, &isPrinter);

    libusb_free_config_descriptor(config);
    if (isIppUsb) {
      break;
    }
  }
  if (isPrinter && !isIppUsb) {
    LOG(INFO) << "Device " << vid_pid << " is a printer without IPP-USB";
  }
  if (!isIppUsb) {
    return base::nullopt;
  }

  return ScannerInfoForDevice(device, descriptor);
}

}  // namespace

base::Optional<std::string> BackendForDevice(const std::string& device_name) {
  LOG(INFO) << "Finding real backend for device: " << device_name;
  std::string protocol, name, vid, pid, path;
  if (!RE2::FullMatch(
          device_name,
          "ippusb:([^:]+):([^:]+):([0-9A-Fa-f]{4})_([0-9A-Fa-f]{4})(/.*)",
          &protocol, &name, &vid, &pid, &path)) {
    return base::nullopt;
  }

  std::string socket =
      base::StringPrintf("%s-%s.sock", vid.c_str(), pid.c_str());
  if (!WaitForSocket(socket, kSocketCreationTimeout)) {
    return base::nullopt;
  }

  std::string real_device =
      base::StringPrintf("airscan:%s:%s:unix://%s%s", protocol.c_str(),
                         name.c_str(), socket.c_str(), path.c_str());
  return real_device;
}

std::vector<ScannerInfo> FindIppUsbDevices() {
  libusb_context* ctx;
  int status = libusb_init(&ctx);
  if (status != 0) {
    LOG(ERROR) << "Failed to initialize libusb: " << libusb_error_name(status);
    return {};
  }
  auto context =
      std::unique_ptr<libusb_context, decltype(&libusb_exit)>(ctx, libusb_exit);

  libusb_device** dev_list;
  ssize_t num_devices = libusb_get_device_list(context.get(), &dev_list);
  if (num_devices < 0) {
    LOG(ERROR) << "Failed to enumerate USB devices: "
               << libusb_error_name(num_devices);
    return {};
  }

  std::vector<ScannerInfo> scanners;
  for (ssize_t i = 0; i < num_devices; i++) {
    base::Optional<ScannerInfo> info = CheckUsbDevice(dev_list[i]);
    if (info.has_value()) {
      scanners.push_back(info.value());
    }
  }

  libusb_free_device_list(dev_list, 1);
  return scanners;
}

}  // namespace lorgnette
