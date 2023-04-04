// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_

#include <optional>
#include <string>
#include <variant>

#include <brillo/cryptohome.h>
#include <libhwsec/structures/explicit_init.h>

namespace cryptohome {

enum class LockoutPolicy {
  kNoLockout = 0,
  kAttemptLimited = 1,
  kTimeLimited = 2
};

struct CommonAuthFactorMetadata {
  std::string chromeos_version_last_updated;
  std::string chrome_version_last_updated;
  std::optional<LockoutPolicy> lockout_policy;
};

struct PasswordAuthFactorMetadata {};

struct PinAuthFactorMetadata {};

struct CryptohomeRecoveryAuthFactorMetadata {};

struct KioskAuthFactorMetadata {};

struct SmartCardAuthFactorMetadata {
  hwsec::ExplicitInit<brillo::Blob> public_key_spki_der;
};

struct FingerprintAuthFactorMetadata {};

struct AuthFactorMetadata {
  CommonAuthFactorMetadata common;

  // Use `std::monostate` as the first alternative, in order to make the
  // default constructor create an empty metadata.
  std::variant<std::monostate,
               PasswordAuthFactorMetadata,
               PinAuthFactorMetadata,
               CryptohomeRecoveryAuthFactorMetadata,
               KioskAuthFactorMetadata,
               SmartCardAuthFactorMetadata,
               FingerprintAuthFactorMetadata>
      metadata;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_
