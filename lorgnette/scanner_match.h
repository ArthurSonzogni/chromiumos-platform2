// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SCANNER_MATCH_H_
#define LORGNETTE_SCANNER_MATCH_H_

#include <optional>
#include <string>
#include <utility>

#include <base/containers/flat_map.h>
#include <base/containers/flat_set.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/usb/usb_device.h"

namespace lorgnette {

// Parses the VID and PID or BUS and DEV properties from a usb device name
// if it matches `regex_pattern`. Returns `std::nullopt` if there is no
// match, or a pair of the identifiers in the format {VID, PID} or
// {BUS, DEV} if there is a match.
std::optional<std::pair<std::string, std::string>>
ExtractIdentifiersFromDeviceName(const std::string& device_name,
                                 const std::string& regex_pattern);

// Returns true if `scanner_name` has a vid and pid present in `seen_vidpid`
// or a bus and device address present in `seen_busdev`.
bool DuplicateScannerExists(const std::string& scanner_name,
                            const base::flat_set<std::string>& seen_vidpid,
                            const base::flat_set<std::string>& seen_busdev);

// Detect whether `scanner` represents a USB or network connection based on the
// SANE connection string.
lorgnette::ConnectionType ConnectionTypeForScanner(const ScannerInfo& scanner);

// Create a human-readable display name from the info in `scanner`.
std::string DisplayNameForScanner(const ScannerInfo& scanner);

// Associate a string value with known USB devices and allow looking up stored
// values based on a ScannerInfo.  This is conceptually similar to a map that
// has multiple keys for each value.
class ScannerMatcher {
 public:
  ScannerMatcher() = default;
  ~ScannerMatcher() = default;

  // Associate `device` with `id`, overwriting any previous keys derived from
  // `device` if they exist.
  void AddUsbDevice(UsbDevice& device, const std::string& id);

  // Try to extract USB info from `scanner` and look up a previously saved
  // value.  Returns an empty string if no match can be found.
  std::string LookupScanner(const ScannerInfo& scanner);

 private:
  // The key is vid:pid:serial, in the format vvvv:pppp:serial. The value is
  // whatever string the caller supplied.
  base::flat_map<std::string, std::string> by_vid_pid_;

  // The key is bus:dev, in the format bbb:ddd.  The value is whatever string
  // the caller supplied.
  base::flat_map<std::string, std::string> by_bus_dev_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SCANNER_MATCH_H_
