// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/fake_recovery_mediator_crypto.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/optional.h>
#include <base/stl_util.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/ecdh_hkdf.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/error_util.h"
#include "cryptohome/crypto/recovery_crypto.h"
#include "cryptohome/cryptolib.h"

namespace cryptohome {
namespace {

brillo::SecureBlob GetMediatorShareHkdfInfo() {
  return brillo::SecureBlob(RecoveryCrypto::kMediatorShareHkdfInfoValue);
}

}  // namespace

// Hardcoded fake mediator public and private keys. Do not use them in
// production! Keys were generated at random using
// EllipticCurve::GenerateKeysAsSecureBlobs method and converted to hex.
static const char kFakeMediatorPublicKeyHex[] =
    "041C66FD08151D1C34EA5003F7C24557D2E4802535AA4F65EDBE3CD495CFE060387D00D5D2"
    "5D859B26C5134F1AD00F2230EAB72A47F46DF23407CF68FB18C509DE";
static const char kFakeMediatorPrivateKeyHex[] =
    "B7A01DA624ECF448D9F7E1B07236EA2930A17C9A31AD60E43E01A8FEA934AB1C";

std::unique_ptr<FakeRecoveryMediatorCrypto>
FakeRecoveryMediatorCrypto::Create() {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return nullptr;
  }
  base::Optional<EllipticCurve> ec =
      EllipticCurve::Create(RecoveryCrypto::kCurve, context.get());
  if (!ec) {
    LOG(ERROR) << "Failed to create EllipticCurve";
    return nullptr;
  }
  return base::WrapUnique(new FakeRecoveryMediatorCrypto(std::move(*ec)));
}

FakeRecoveryMediatorCrypto::FakeRecoveryMediatorCrypto(EllipticCurve ec)
    : ec_(std::move(ec)) {}

bool FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
    brillo::SecureBlob* mediator_pub_key) {
  if (!brillo::SecureBlob::HexStringToSecureBlob(kFakeMediatorPublicKeyHex,
                                                 mediator_pub_key)) {
    LOG(ERROR) << "Failed to convert hex to SecureBlob";
    return false;
  }
  return true;
}

bool FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
    brillo::SecureBlob* mediator_priv_key) {
  if (!brillo::SecureBlob::HexStringToSecureBlob(kFakeMediatorPrivateKeyHex,
                                                 mediator_priv_key)) {
    LOG(ERROR) << "Failed to convert hex to SecureBlob";
    return false;
  }
  return true;
}

bool FakeRecoveryMediatorCrypto::DecryptMediatorShare(
    const brillo::SecureBlob& mediator_priv_key,
    const RecoveryCrypto::EncryptedMediatorShare& encrypted_mediator_share,
    brillo::SecureBlob* mediator_share) const {
  brillo::SecureBlob aes_gcm_key;
  if (!GenerateEcdhHkdfRecipientKey(
          ec_, mediator_priv_key, encrypted_mediator_share.ephemeral_pub_key,
          GetMediatorShareHkdfInfo(),
          /*hkdf_salt=*/brillo::SecureBlob(), RecoveryCrypto::kHkdfHash,
          kAesGcm256KeySize, &aes_gcm_key)) {
    LOG(ERROR) << "Failed to generate ECDH+HKDF recipient key";
    return false;
  }

  if (!CryptoLib::AesGcmDecrypt(encrypted_mediator_share.encrypted_data,
                                encrypted_mediator_share.tag, aes_gcm_key,
                                encrypted_mediator_share.iv, mediator_share)) {
    LOG(ERROR) << "Failed to perform AES-GCM decryption";
    return false;
  }

  return true;
}

bool FakeRecoveryMediatorCrypto::Mediate(
    const brillo::SecureBlob& mediator_priv_key,
    const brillo::SecureBlob& publisher_pub_key,
    const RecoveryCrypto::EncryptedMediatorShare& encrypted_mediator_share,
    brillo::SecureBlob* mediated_publisher_pub_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  brillo::SecureBlob mediator_share;
  if (!DecryptMediatorShare(mediator_priv_key, encrypted_mediator_share,
                            &mediator_share)) {
    LOG(ERROR) << "Failed to decrypt mediator share";
    return false;
  }
  crypto::ScopedBIGNUM mediator_share_bn = SecureBlobToBigNum(mediator_share);
  if (!mediator_share_bn) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT publisher_pub_point =
      ec_.SecureBlobToPoint(publisher_pub_key, context.get());
  if (!publisher_pub_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT";
    return false;
  }
  // Performs scalar multiplication of publisher_pub_key and mediator_share.
  crypto::ScopedEC_POINT point_dh =
      ec_.Multiply(*publisher_pub_point, *mediator_share_bn, context.get());
  if (!point_dh) {
    LOG(ERROR) << "Failed to perform scalar multiplication";
    return false;
  }
  if (!ec_.PointToSecureBlob(*point_dh, mediated_publisher_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob";
    return false;
  }
  return true;
}

}  // namespace cryptohome
