// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/static_utils.h"

#include <cinttypes>
#include <memory>
#include <utility>

#include <base/memory/free_deleter.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <openssl/bn.h>
#include <openssl/rsa.h>

#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::SecureBlob;
using hwsec_foundation::FillRsaPrivateKeyFromSecretPrime;
using hwsec_foundation::kWellKnownExponent;
using hwsec_foundation::RsaOaepDecrypt;
using hwsec_foundation::Sha1;
using hwsec_foundation::status::MakeStatus;

using ScopedByteArray = std::unique_ptr<BYTE, base::FreeDeleter>;

namespace hwsec {

StatusOr<crypto::ScopedRSA> ParseRsaFromTpmPubkeyBlob(
    overalls::Overalls& overalls, const brillo::Blob& pubkey) {
  // Parse the serialized TPM_PUBKEY.
  brillo::Blob pubkey_copy = pubkey;
  uint64_t offset = 0;
  TPM_PUBKEY parsed = {};  // Zero initialize.

  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Orspi_UnloadBlob_PUBKEY_s(
          &offset, pubkey_copy.data(), pubkey_copy.size(), &parsed)))
      .WithStatus<TPMError>("Failed to call Orspi_UnloadBlob_PUBKEY_s");

  ScopedByteArray scoped_key(parsed.pubKey.key);
  ScopedByteArray scoped_parms(parsed.algorithmParms.parms);

  if (offset != pubkey.size()) {
    return MakeStatus<TPMError>("Found garbage data after the TPM_PUBKEY",
                                TPMRetryAction::kNoRetry);
  }

  uint64_t parms_offset = 0;
  TPM_RSA_KEY_PARMS parms = {};  // Zero initialize.

  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Orspi_UnloadBlob_RSA_KEY_PARMS_s(
          &parms_offset, parsed.algorithmParms.parms,
          parsed.algorithmParms.parmSize, &parms)))
      .WithStatus<TPMError>("Failed to call Orspi_UnloadBlob_RSA_KEY_PARMS_s");

  ScopedByteArray scoped_exponent(parms.exponent);

  if (parms_offset != parsed.algorithmParms.parmSize) {
    return MakeStatus<TPMError>(
        "Found garbage data after the TPM_PUBKEY algorithm params",
        TPMRetryAction::kNoRetry);
  }

  // Get the public exponent.
  crypto::ScopedRSA rsa(RSA_new());
  crypto::ScopedBIGNUM e(BN_new()), n(BN_new());
  if (!rsa || !e || !n) {
    return MakeStatus<TPMError>("Failed to create RSA or BIGNUM",
                                TPMRetryAction::kNoRetry);
  }
  if (!parms.exponentSize) {
    if (!BN_set_word(e.get(), kWellKnownExponent)) {
      return MakeStatus<TPMError>(
          "Failed to set BN exponent to WellKnownExponent",
          TPMRetryAction::kNoRetry);
    }
  } else {
    if (!BN_bin2bn(parms.exponent, parms.exponentSize, e.get())) {
      return MakeStatus<TPMError>("Failed to load BN exponent from TPM_PUBKEY",
                                  TPMRetryAction::kNoRetry);
    }
  }

  // Get the modulus.
  if (!BN_bin2bn(parsed.pubKey.key, parsed.pubKey.keyLength, n.get())) {
    return MakeStatus<TPMError>("Failed to load BN modulus from TPM_PUBKEY",
                                TPMRetryAction::kNoRetry);
  }

  if (!RSA_set0_key(rsa.get(), n.release(), e.release(), nullptr)) {
    return MakeStatus<TPMError>("Failed to set parameters for RSA",
                                TPMRetryAction::kNoRetry);
  }

  return rsa;
}

}  // namespace hwsec
