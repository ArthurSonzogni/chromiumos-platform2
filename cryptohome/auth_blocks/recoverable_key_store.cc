// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/recoverable_key_store.h"

#include <optional>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <cryptohome/proto_bindings/recoverable_key_store.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/error/cryptohome_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/recoverable_key_store/backend_cert_provider.h"
#include "cryptohome/recoverable_key_store/generate.h"
#include "cryptohome/recoverable_key_store/type.h"

namespace cryptohome {

namespace {

using ::cryptohome::error::CryptohomeError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::hwsec_foundation::CreateRandomBlob;
using ::hwsec_foundation::status::MakeStatus;

// Android uses a random 8-byte long int as the label. As the numeric value of
// the label isn't meaningful, and the label will eventually be serialized to
// bytes, we treat the label as bytes directly.
constexpr size_t kWrongAttemptLabelSize = 8;

}  // namespace

CryptohomeStatusOr<RecoverableKeyStoreState> CreateRecoverableKeyStoreState(
    LockScreenKnowledgeFactorType lskf_type,
    const AuthInput& auth_input,
    const RecoverableKeyStoreBackendCertProvider& cert_provider) {
  if (!auth_input.user_input.has_value() ||
      !auth_input.user_input_hash_algorithm.has_value() ||
      !auth_input.user_input_hash_salt.has_value() ||
      !auth_input.security_domain_keys.has_value()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocRecoverableKeyStoreCreateInvalidParams),
        ErrorActionSet(), user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
  }

  LockScreenKnowledgeFactor lskf = {
      .lskf_type = lskf_type,
      .algorithm = *auth_input.user_input_hash_algorithm,
      .salt = *auth_input.user_input_hash_salt,
      .hash = *auth_input.user_input,
  };

  std::optional<RecoverableKeyStoreBackendCert> backend_cert =
      cert_provider.GetBackendCert();
  if (!backend_cert.has_value()) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocRecoverableKeyStoreCreateGetCertFailed),
        ErrorActionSet({PossibleAction::kReboot, PossibleAction::kRetry}),
        user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE);
  }

  brillo::Blob wrong_attempt_label = CreateRandomBlob(kWrongAttemptLabelSize);

  CryptohomeStatusOr<RecoverableKeyStore> key_store_proto =
      GenerateRecoverableKeyStore(lskf, wrong_attempt_label,
                                  *auth_input.security_domain_keys,
                                  *backend_cert);
  if (!key_store_proto.ok()) {
    return MakeStatus<CryptohomeError>(
               CRYPTOHOME_ERR_LOC(kLocRecoverableKeyStoreCreateGenerateFailed),
               ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}))
        .Wrap(std::move(key_store_proto).err_status());
  }

  std::string key_store_proto_string;
  if (!key_store_proto->SerializeToString(&key_store_proto_string)) {
    return MakeStatus<CryptohomeError>(
        CRYPTOHOME_ERR_LOC(kLocRecoverableKeyStoreCreateSerializeFailed),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        user_data_auth::CRYPTOHOME_TOKEN_SERIALIZATION_FAILED);
  }

  return RecoverableKeyStoreState{
      .key_store_proto = brillo::BlobFromString(key_store_proto_string)};
}

}  // namespace cryptohome
