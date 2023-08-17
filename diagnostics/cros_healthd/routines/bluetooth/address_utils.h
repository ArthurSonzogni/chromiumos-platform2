// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_ADDRESS_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_ADDRESS_UTILS_H_

#include <optional>
#include <string>
#include <utility>

namespace diagnostics {

// Bluetooth device address is a 6-octet, 48-bit identifier. For the "public"
// address, the first 3 octets are the publicly assigned portion by the
// Institute of Electrical and Electronics Engineers (IEEE).
//
// To validate an IEEE administered address, we check whether the first 3 octets
// of the address are an OUI or CID identifier. These 3 octets are also the
// manufacturer ID.
//
// OUI (Organizationally Unique Identifier):
//   The last two bits of the first octet should be 00.
// CID (Company ID):
//   The last four bits of the first octet should be 1010.
//
// The |address_type| should be either "public" or "private".
//
// The return value is the validation result and manufacturer ID. If validation
// passes or address parsing fails, the manufacturer ID will be null.
std::pair<bool, std::optional<std::string>> ValidatePeripheralAddress(
    const std::string& address, const std::string& address_type);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_ADDRESS_UTILS_H_
