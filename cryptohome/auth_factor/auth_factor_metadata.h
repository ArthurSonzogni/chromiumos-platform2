// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_
#define CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_

#include <string>
#include <variant>

#include <brillo/cryptohome.h>
#include <libhwsec/structures/no_default_init.h>

namespace cryptohome {

struct CommonAuthFactorMetadata {
  std::string chromeos_version_last_updated;
  std::string chrome_version_last_updated;
};

struct PasswordAuthFactorMetadata {};

struct PinAuthFactorMetadata {};

struct CryptohomeRecoveryAuthFactorMetadata {};

struct KioskAuthFactorMetadata {};

struct SmartCardAuthFactorMetadata {
  hwsec::NoDefault<brillo::Blob> public_key_spki_der;
};

struct AuthFactorMetadata {
  CommonAuthFactorMetadata common;

  // Use `std::monostate` as the first alternative, in order to make the
  // default constructor create an empty metadata.
  std::variant<std::monostate,
               PasswordAuthFactorMetadata,
               PinAuthFactorMetadata,
               CryptohomeRecoveryAuthFactorMetadata,
               KioskAuthFactorMetadata,
               SmartCardAuthFactorMetadata>
      metadata;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_FACTOR_AUTH_FACTOR_METADATA_H_
