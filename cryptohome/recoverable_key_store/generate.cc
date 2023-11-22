// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/recoverable_key_store/generate.h"

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {
namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::status::MakeStatus;

}  // namespace

CryptohomeStatusOr<RecoverableKeyStore> GenerateRecoverableKeyStore(
    const LockScreenKnowledgeFactor& lskf,
    const brillo::Blob& wrong_attempt_label,
    const SecurityDomainKeys& keys,
    const RecoverableKeyStoreBackendCert& cert) {
  // TODO(b/312628857): Implement GenerateRecoverableKeyStore.
  return MakeStatus<CryptohomeError>(
      CRYPTOHOME_ERR_LOC(kLocGenKeyStoreNotImplemented),
      ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
      user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED);
}

}  // namespace cryptohome
