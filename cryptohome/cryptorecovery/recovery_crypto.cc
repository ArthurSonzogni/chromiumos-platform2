// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/recovery_crypto.h"

#include <utility>

#include <base/logging.h>
#include <base/stl_util.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/ecdh_hkdf.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>

using ::hwsec_foundation::EllipticCurve;
using ::hwsec_foundation::Hkdf;
using ::hwsec_foundation::HkdfHash;

namespace cryptohome::cryptorecovery {
namespace {

// Value must be kept in sync with the server.
constexpr char kMediatorShareHkdfInfoPrefix[] = "HSM-Payload Key";

}  // namespace

brillo::Blob RecoveryCrypto::GenerateMediatorShareHkdfInfo(
    const brillo::Blob& hsm_associated_data_cbor) {
  return brillo::CombineBlobs(
      {brillo::BlobFromString(kMediatorShareHkdfInfoPrefix),
       hsm_associated_data_cbor});
}

brillo::Blob RecoveryCrypto::GenerateLegacyMediatorShareHkdfInfo(
    const UserIdentifier& user_id) {
  return brillo::CombineBlobs(
      {brillo::BlobFromString(kMediatorShareHkdfInfoPrefix),
       brillo::Blob({std::to_underlying(user_id.type)}), user_id.value});
}

brillo::Blob RecoveryCrypto::GenerateLegacyMediatorShareHkdfInfo() {
  return brillo::BlobFromString(kMediatorShareHkdfInfoPrefix);
}

const char RecoveryCrypto::kRequestPayloadPlainTextHkdfInfoValue[] =
    "REQUEST-Payload Key";

const char RecoveryCrypto::kResponsePayloadPlainTextHkdfInfoValue[] =
    "RESPONSE-Payload Key";

const EllipticCurve::CurveType RecoveryCrypto::kCurve =
    EllipticCurve::CurveType::kPrime256;

const HkdfHash RecoveryCrypto::kHkdfHash = HkdfHash::kSha256;

const unsigned int RecoveryCrypto::kHkdfSaltLength = 32;

RecoveryCrypto::~RecoveryCrypto() = default;

}  // namespace cryptohome::cryptorecovery
