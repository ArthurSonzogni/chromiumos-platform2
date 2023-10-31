// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/scanner_match.h"

#include <optional>
#include <string>
#include <utility>

#include <base/strings/strcat.h>
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

}  // namespace lorgnette
