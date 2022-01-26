// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/key_objects.h"

#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto/hkdf.h"

namespace cryptohome {

namespace {
// !!!WARNING!!!: This value must stay unchanged, for backwards compatibility.
constexpr char kUssCredentialSecretHkdfInfo[] = "cryptohome USS credential";
}  // namespace

brillo::SecureBlob LibScryptCompatKeyObjects::derived_key() {
  return derived_key_;
}

brillo::SecureBlob LibScryptCompatKeyObjects::ConsumeSalt() {
  CHECK(salt_ != std::nullopt);

  // The salt may not be re-used.
  brillo::SecureBlob value = salt_.value();
  salt_.reset();
  return value;
}

std::optional<brillo::SecureBlob> KeyBlobs::DeriveUssCredentialSecret() const {
  if (!vkk_key.has_value() || vkk_key.value().empty()) {
    LOG(ERROR) << "Missing input secret for deriving a USS credential secret";
    return std::nullopt;
  }
  brillo::SecureBlob uss_credential_secret;
  if (!Hkdf(HkdfHash::kSha256, /*key=*/vkk_key.value(),
            /*info=*/brillo::SecureBlob(kUssCredentialSecretHkdfInfo),
            /*salt=*/brillo::SecureBlob(),
            /*result_len=*/0, &uss_credential_secret)) {
    LOG(ERROR) << "USS credential secret HKDF derivation failed";
    return std::nullopt;
  }
  CHECK(!uss_credential_secret.empty());
  return uss_credential_secret;
}

}  // namespace cryptohome
