// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/address_utils.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <re2/re2.h>

namespace diagnostics {
namespace {

// Regex to check the address format and get the first octet.
constexpr auto kBluetoothAddressRegex =
    R"(^(([0-9A-F]{2})(:[0-9A-F]{2}){2})(:[0-9A-F]{2}){3}$)";

// List of known public identifiers that are neither OUI nor CID identifiers.
// Reference: file |manuf| in https://gitlab.com/wireshark/wireshark.
const std::set<std::string> exceptions = {
    "01:0E:CF" /* PN-MC */,    "02:04:06" /* BbnInter */,
    "02:07:01" /* Racal-Da */, "02:1C:7C" /* Perq */,
    "02:20:48" /* Marconi */,  "02:60:60" /* 3com */,
    "02:60:86" /* LogicRep */, "02:60:8C" /* 3comIbmP */,
    "02:70:01" /* Racal-Da */, "02:70:B0" /* MA-ComCo */,
    "02:70:B3" /* DataReca */, "02:9D:8E" /* CardiacR */,
    "02:A0:C9" /* Intel */,    "02:AA:3C" /* Olivetti */,
    "02:BB:01" /* Octothor */, "02:C0:8C" /* 3Com */,
    "02:CF:1C" /* Communic */, "02:CF:1F" /* CMC */,
    "02:E0:3B" /* Prominet */, "02:E6:D3" /* NixdorfC */,
    "09:00:6A" /* AT&T */,     "11:00:AA" /* Private */,
    "11:11:11" /* Private */,  "2E:2E:2E" /* LaaLocal */,
    "47:54:43" /* GtcNotRe */, "52:54:00" /* RealtekU */,
    "52:54:4C" /* Novell20 */, "52:54:AB" /* RealtekA */,
    "56:58:57" /* AculabPl */, "E2:0C:0F" /* Kingston */,
};

bool IsOui(uint32_t first_octet) {
  return (first_octet & 0b11) == 0b00;
}

bool IsCid(uint32_t first_octet) {
  return (first_octet & 0b1111) == 0b1010;
}

// Check if the first 3 octets of the address is in |exceptions|.
bool IsKnownAuthorizedAddress(const std::string& manufacturer_id) {
  return exceptions.count(manufacturer_id);
}

// Check if the public address is an IEEE Registration Authorized address.
bool ValidatePublicPeripheralAddress(const std::string& address) {
  std::string manufacturer_id, raw_first_octet;
  if (!RE2::FullMatch(address, kBluetoothAddressRegex, &manufacturer_id,
                      &raw_first_octet)) {
    LOG(ERROR) << "Failed to parse the address: " << address;
    return false;
  }

  uint32_t first_octet;
  if (!base::HexStringToUInt(raw_first_octet, &first_octet)) {
    LOG(ERROR) << "Failed to convert the first octet of address: " << address;
    return false;
  }

  return IsOui(first_octet) || IsCid(first_octet) ||
         IsKnownAuthorizedAddress(manufacturer_id);
}

}  // namespace

bool ValidatePeripheralAddress(const std::string& address,
                               const std::string& address_type) {
  if (address_type == "public") {
    return ValidatePublicPeripheralAddress(address);
  } else if (address_type == "random") {
    return RE2::FullMatch(address, kBluetoothAddressRegex);
  } else {
    LOG(ERROR) << "Unexpected address type: " << address_type;
    return false;
  }
}

}  // namespace diagnostics
