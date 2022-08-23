// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/signing.h"

#include <string>

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using hwsec_foundation::Sha256;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

StatusOr<brillo::Blob> SigningTpm2::Sign(const OperationPolicy& policy,
                                         Key key,
                                         const brillo::Blob& data) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data,
                   backend_.GetKeyManagementTpm2().GetKeyData(key),
                   _.WithStatus<TPMError>("Failed to get the key data"));

  trunks::TPM_ALG_ID sign_algorithm;
  switch (key_data.cache.public_area.type) {
    case trunks::TPM_ALG_RSA:
      sign_algorithm = trunks::TPM_ALG_RSASSA;
      break;
    case trunks::TPM_ALG_ECC:
      sign_algorithm = trunks::TPM_ALG_ECDSA;
      break;
    default:
      return MakeStatus<TPMError>("Unknown TPM key type",
                                  TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      ConfigTpm2::TrunksSession session,
      backend_.GetConfigTpm2().GetTrunksSession(policy, true, false),
      _.WithStatus<TPMError>("Failed to get session for policy"));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::string signature;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->Sign(
                      key_data.key_handle, sign_algorithm,
                      trunks::TPM_ALG_SHA256, brillo::BlobToString(data),
                      /*generate_hash=*/true, session.delegate, &signature)))
      .WithStatus<TPMError>("Failed to sign the data");

  return brillo::BlobFromString(signature);
}

Status SigningTpm2::Verify(const OperationPolicy& policy,
                           Key key,
                           const brillo::Blob& signed_data) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec
