// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/usb_device_info.h"

#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "cros-disks/file_reader.h"

using base::FilePath;
using std::map;
using std::string;
using std::vector;

namespace cros_disks {

// A data structure for holding information of a USB device.
struct USBDeviceEntry {
  DeviceMediaType media_type;
};

USBDeviceInfo::USBDeviceInfo() {
}

USBDeviceInfo::~USBDeviceInfo() {
}

DeviceMediaType USBDeviceInfo::GetDeviceMediaType(
    const string& vendor_id, const string& product_id) const {
  CHECK(!vendor_id.empty()) << "Invalid vendor ID";
  CHECK(!product_id.empty()) << "Invalid product ID";

  string id = vendor_id + ":" + product_id;
  map<string, USBDeviceEntry>::const_iterator map_iterator = entries_.find(id);
  if (map_iterator != entries_.end())
    return map_iterator->second.media_type;
  return DEVICE_MEDIA_USB;
}

bool USBDeviceInfo::RetrieveFromFile(const string& path) {
  entries_.clear();

  FileReader reader;
  if (!reader.Open(FilePath(path))) {
    LOG(ERROR) << "Failed to retrieve USB device info from '" << path << "'";
    return false;
  }

  string line;
  while (reader.ReadLine(&line)) {
    if (IsLineSkippable(line))
      continue;

    vector<string> tokens;
    base::SplitString(line, ' ', &tokens);
    if (tokens.size() >= 2) {
      USBDeviceEntry& entry = entries_[tokens[0]];
      entry.media_type = ConvertToDeviceMediaType(tokens[1]);
    }
  }
  return true;
}

bool USBDeviceInfo::GetVendorAndProductName(
    const string& ids_file, const string& vendor_id, const string& product_id,
    string* vendor_name, string* product_name) {
  vendor_name->clear();
  product_name->clear();

  FileReader reader;
  if (!reader.Open(FilePath(ids_file))) {
    LOG(ERROR) << "Failed to retrieve USB identifier database at '"
               << ids_file << "'";
    return false;
  }

  bool found_vendor = false;
  string line;
  while (reader.ReadLine(&line)) {
    if (IsLineSkippable(line))
      continue;

    string id, name;
    // If the target vendor ID is found, search for a matching product ID.
    if (found_vendor) {
      if (line[0] == '\t' &&
          ExtractIdAndName(line.substr(1), &id, &name)) {
        if (id == product_id) {
          *product_name = name;
          break;
        }
        continue;
      }

      // If the line does not contain any product info, assume a new
      // section has started and no product info will be found for the
      // target ID. Return immediately.
      break;
    }

    // Skip forward until the target vendor ID is found.
    if (ExtractIdAndName(line, &id, &name)) {
      if (id == vendor_id) {
        *vendor_name = name;
        found_vendor = true;
      }
    }
  }

  return found_vendor;
}

DeviceMediaType USBDeviceInfo::ConvertToDeviceMediaType(
    const string& str) const {
  if (str == "sd") {
    return DEVICE_MEDIA_SD;
  } else if (str == "mobile") {
    return DEVICE_MEDIA_MOBILE;
  } else {
    return DEVICE_MEDIA_USB;
  }
}

bool USBDeviceInfo::IsLineSkippable(const string& line) const {
  string trimmed_line;
  // Trim only ASCII whitespace for now.
  TrimWhitespaceASCII(line, TRIM_ALL, &trimmed_line);
  return trimmed_line.empty() || StartsWithASCII(trimmed_line, "#", true);
}

bool USBDeviceInfo::ExtractIdAndName(
    const string& line, string* id, string* name) const {
  if ((line.length() > 6) &&
      IsHexDigit(line[0]) && IsHexDigit(line[1]) &&
      IsHexDigit(line[2]) && IsHexDigit(line[3]) &&
      (line[4] == ' ') && (line[5] == ' ')) {
    *id = StringToLowerASCII(line.substr(0, 4));
    *name = line.substr(6);
    return true;
  }
  return false;
}

}  // namespace cros_disks
