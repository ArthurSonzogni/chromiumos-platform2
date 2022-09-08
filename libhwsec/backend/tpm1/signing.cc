// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/signing.h"

#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/backend/tpm1/backend.h"
#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"
#include "libhwsec/tss_utils/scoped_tss_type.h"

using brillo::BlobFromString;
using hwsec_foundation::Sha256;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

// The DER encoding of SHA-256 DigestInfo as defined in PKCS #1.
constexpr uint8_t kSha256DigestInfo[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};

}  // namespace

StatusOr<brillo::Blob> SigningTpm1::Sign(const OperationPolicy& policy,
                                         Key key,
                                         const brillo::Blob& data) {
  if (policy.device_configs.any() || policy.permission.auth_value.has_value()) {
    return MakeStatus<TPMError>("Unsupported operation policy",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(const KeyTpm1& key_data,
                   backend_.GetKeyManagementTpm1().GetKeyData(key),
                   _.WithStatus<TPMError>("Failed to get the key data"));

  overalls::Overalls& overalls = backend_.GetOverall().overalls;

  ASSIGN_OR_RETURN(TSS_HCONTEXT context, backend_.GetTssContext());

  // Create a hash object to hold the input.
  ScopedTssObject<TSS_HHASH> hash_handle(overalls, context);

  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Ospi_Context_CreateObject(
          context, TSS_OBJECT_TYPE_HASH, TSS_HASH_OTHER, hash_handle.ptr())))
      .WithStatus<TPMError>("Failed to create hash object");

  // Create the DER encoded input.
  brillo::Blob der_header(std::begin(kSha256DigestInfo),
                          std::end(kSha256DigestInfo));
  brillo::Blob der_encoded_input =
      brillo::CombineBlobs({der_header, Sha256(data)});

  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Ospi_Hash_SetHashValue(
          hash_handle, der_encoded_input.size(), der_encoded_input.data())))
      .WithStatus<TPMError>("Failed to set hash data");

  uint32_t length = 0;
  ScopedTssMemory buffer(overalls, context);

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Hash_Sign(
                      hash_handle, key_data.key_handle, &length, buffer.ptr())))
      .WithStatus<TPMError>("Failed to generate signature");

  return brillo::Blob(buffer.value(), buffer.value() + length);
}

Status SigningTpm1::Verify(const OperationPolicy& policy,
                           Key key,
                           const brillo::Blob& signed_data) {
  return MakeStatus<TPMError>("Unimplemented", TPMRetryAction::kNoRetry);
}

}  // namespace hwsec
