// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "cryptohome/auth_factor/auth_factor_type.h"

#include <optional>
#include <string>
#include <utility>

namespace cryptohome {
// Note: The string values in this constant must stay stable, as they're used in
// file names.
constexpr std::pair<AuthFactorType, const char*> kAuthFactorTypeStrings[] = {
    {AuthFactorType::kPassword, "password"},
    {AuthFactorType::kPin, "pin"},
    {AuthFactorType::kSmartCard, "smart_card"},
    {AuthFactorType::kCryptohomeRecovery, "cryptohome_recovery"},
    {AuthFactorType::kLegacyFingerprint, "legacy_fingerprint"},
    {AuthFactorType::kKiosk, "kiosk"}};

// Converts the auth factor type enum into a string.
std::string AuthFactorTypeToString(AuthFactorType type) {
  for (const auto& type_and_string : kAuthFactorTypeStrings) {
    if (type_and_string.first == type) {
      return type_and_string.second;
    }
  }
  return std::string();
}

std::optional<AuthFactorType> AuthFactorTypeFromString(
    const std::string& type_string) {
  for (const auto& type_and_string : kAuthFactorTypeStrings) {
    if (type_and_string.second == type_string) {
      return type_and_string.first;
    }
  }
  return std::nullopt;
}

}  // namespace cryptohome
