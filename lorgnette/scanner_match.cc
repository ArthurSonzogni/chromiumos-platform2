// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/scanner_match.h"

#include <optional>
#include <string>
#include <utility>

#include <base/containers/map_util.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include <base/logging.h>

namespace lorgnette {

std::optional<std::pair<std::string, std::string>>
ExtractIdentifiersFromDeviceName(const std::string& device_name,
                                 const std::string& regex_pattern) {
  std::string vid, pid;
  if (!RE2::FullMatch(device_name, regex_pattern, &vid, &pid)) {
    return std::nullopt;
  }
  return std::make_pair(vid, pid);
}

bool DuplicateScannerExists(const std::string& scanner_name,
                            const base::flat_set<std::string>& seen_vidpid,
                            const base::flat_set<std::string>& seen_busdev) {
  // Currently pixma only uses 'pixma' as scanner name
  // while epson has multiple formats (i.e. epsonds and epson2)
  std::optional<std::pair<std::string, std::string>> vid_pid_result =
      ExtractIdentifiersFromDeviceName(
          scanner_name, "pixma:([0-9a-fA-F]{4})([0-9a-fA-F]{4})_[0-9a-fA-F]*");

  if (vid_pid_result.has_value()) {
    std::string vid = base::ToLowerASCII(vid_pid_result.value().first);
    std::string pid = base::ToLowerASCII(vid_pid_result.value().second);
    return seen_vidpid.contains(vid + ":" + pid);
  }

  auto bus_dev_result = ExtractIdentifiersFromDeviceName(
      scanner_name, "epson(?:2|ds)?:libusb:([0-9]{3}):([0-9]{3})");

  if (bus_dev_result.has_value()) {
    std::string bus = bus_dev_result.value().first;
    std::string dev = bus_dev_result.value().second;
    return seen_busdev.contains(bus + ":" + dev);
  }
  return false;
}

lorgnette::ConnectionType ConnectionTypeForScanner(const ScannerInfo& scanner) {
  if (base::StartsWith(scanner.name(), "epson2:net:") ||
      base::StartsWith(scanner.name(), "epsonds:net:")) {
    return lorgnette::CONNECTION_NETWORK;
  }

  if (base::StartsWith(scanner.name(), "pixma:") &&
      !RE2::PartialMatch(scanner.name(), "^pixma:(?i)04A9[0-9A-F]{4}")) {
    return lorgnette::CONNECTION_NETWORK;
  }

  // Most SANE scanners are USB unless they match a specific network pattern.
  return lorgnette::CONNECTION_USB;
}

std::string DisplayNameForScanner(const ScannerInfo& scanner) {
  std::string scanner_name;
  if (base::StartsWith(scanner.model(), scanner.manufacturer(),
                       base::CompareCase::INSENSITIVE_ASCII)) {
    scanner_name = scanner.model();
  } else {
    scanner_name = base::StrCat({scanner.manufacturer(), " ", scanner.model()});
  }

  if (scanner.connection_type() == lorgnette::CONNECTION_USB) {
    scanner_name += " (USB)";
  }
  return scanner_name;
}

void ScannerMatcher::AddUsbDevice(UsbDevice& device, const std::string& id) {
  std::string bus_dev = base::StringPrintf("%03d:%03d", device.GetBusNumber(),
                                           device.GetDeviceAddress());
  by_bus_dev_[bus_dev] = id;

  std::string vid_pid =
      base::StringPrintf("%04x:%04x:%s", device.GetVid(), device.GetPid(),
                         device.GetSerialNumber().c_str());
  by_vid_pid_[base::ToLowerASCII(vid_pid)] = id;
}

std::string ScannerMatcher::LookupScanner(const ScannerInfo& scanner) {
  std::string device_name = base::ToLowerASCII(scanner.name());

  // Backends that use the sanei libusb helper contain libusb:BBB:DDD.
  std::string bus, dev;
  if (RE2::FullMatch(device_name, "[^:]+:libusb:([0-9]{3}):([0-9]{3})", &bus,
                     &dev)) {
    std::string key = base::StringPrintf("%s:%s", bus.c_str(), dev.c_str());
    std::string* id = base::FindOrNull(by_bus_dev_, key);
    return id ? *id : "";

    // TODO(b/311196232): If there isn't a match, use BUS:DEV to open the device
    // and try to look up its VID:PID:SERIAL.  This will allow matching back
    // devices that get reset or moved to a different USB port.
  }

  // Some backends use VID:PID as their identifier.
  std::string vid, pid, serial;
  if (RE2::FullMatch(device_name,
                     "pixma:([0-9a-f]{4})([0-9a-f]{4})(?:_([0-9a-z]*))?", &vid,
                     &pid, &serial)) {
    std::string key = base::StringPrintf("%s:%s:%s", vid.c_str(), pid.c_str(),
                                         serial.c_str());
    std::string* id = base::FindOrNull(by_vid_pid_, key);
    return id ? *id : "";
  }

  // Unknown scheme.  Don't try to match it back.
  return "";
}

}  // namespace lorgnette
