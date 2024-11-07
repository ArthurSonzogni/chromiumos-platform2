// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/key_objects.h"

#include <optional>
#include <string>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/hkdf.h>
#include <libhwsec-foundation/status/status_chain.h>

#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/locations.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::Hkdf;
using ::hwsec_foundation::HkdfHash;
using ::hwsec_foundation::status::MakeStatus;

// !!!WARNING!!!: This value must stay unchanged, for backwards compatibility.
constexpr char kUssCredentialSecretHkdfInfo[] = "cryptohome USS credential";

}  // namespace

CryptohomeStatusOr<brillo::SecureBlob> KeyBlobs::DeriveUssCredentialSecret()
    const {
  if (!vkk_key.has_value() || vkk_key.value().empty()) {
    LOG(ERROR) << "Missing input secret for deriving a USS credential secret";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocKeyBlobsDeriveUssSecretMissingInput),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}));
  }
  brillo::SecureBlob uss_credential_secret;
  if (!Hkdf(HkdfHash::kSha256, /*key=*/vkk_key.value(),
            /*info=*/brillo::BlobFromString(kUssCredentialSecretHkdfInfo),
            /*salt=*/brillo::Blob(),
            /*result_len=*/0, &uss_credential_secret)) {
    LOG(ERROR) << "USS credential secret HKDF derivation failed";
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocKeyBlobsDeriveUssSecretHkdfDerivationFailed),
        ErrorActionSet({PossibleAction::kReboot, PossibleAction::kRetry,
                        PossibleAction::kDeleteVault}));
  }
  CHECK(!uss_credential_secret.empty());
  return uss_credential_secret;
}

}  // namespace cryptohome
