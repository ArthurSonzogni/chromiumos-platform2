// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_factor/flatbuffer.h"

#include "cryptohome/auth_factor/type.h"
#include "cryptohome/flatbuffer_schemas/enumerations.h"

namespace cryptohome {

std::optional<SerializedAuthFactorType> SerializeAuthFactorType(
    AuthFactorType type) {
  switch (type) {
    case AuthFactorType::kPassword:
      return SerializedAuthFactorType::kPassword;
    case AuthFactorType::kPin:
      return SerializedAuthFactorType::kPin;
    case AuthFactorType::kCryptohomeRecovery:
      return SerializedAuthFactorType::kCryptohomeRecovery;
    case AuthFactorType::kKiosk:
      return SerializedAuthFactorType::kKiosk;
    case AuthFactorType::kSmartCard:
      return SerializedAuthFactorType::kSmartCard;
    case AuthFactorType::kLegacyFingerprint:
      return SerializedAuthFactorType::kLegacyFingerprint;
    case AuthFactorType::kFingerprint:
      return SerializedAuthFactorType::kFingerprint;
    case AuthFactorType::kUnspecified:
      return std::nullopt;
  }
}

AuthFactorType DeserializeAuthFactorType(SerializedAuthFactorType type) {
  switch (type) {
    case SerializedAuthFactorType::kPassword:
      return AuthFactorType::kPassword;
    case SerializedAuthFactorType::kPin:
      return AuthFactorType::kPin;
    case SerializedAuthFactorType::kCryptohomeRecovery:
      return AuthFactorType::kCryptohomeRecovery;
    case SerializedAuthFactorType::kKiosk:
      return AuthFactorType::kKiosk;
    case SerializedAuthFactorType::kSmartCard:
      return AuthFactorType::kSmartCard;
    case SerializedAuthFactorType::kLegacyFingerprint:
      return AuthFactorType::kLegacyFingerprint;
    case SerializedAuthFactorType::kFingerprint:
      return AuthFactorType::kFingerprint;
  }
}

}  // namespace cryptohome
