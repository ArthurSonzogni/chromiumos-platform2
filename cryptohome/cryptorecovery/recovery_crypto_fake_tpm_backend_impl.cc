// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/recovery_crypto_fake_tpm_backend_impl.h"

#include <optional>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/bn.h>
#include <openssl/ec.h>

#include "cryptohome/crypto/big_num_util.h"

namespace cryptohome {
namespace cryptorecovery {

RecoveryCryptoFakeTpmBackendImpl::RecoveryCryptoFakeTpmBackendImpl() = default;

RecoveryCryptoFakeTpmBackendImpl::~RecoveryCryptoFakeTpmBackendImpl() = default;

brillo::SecureBlob RecoveryCryptoFakeTpmBackendImpl::GenerateKeyAuthValue() {
  return brillo::SecureBlob();
}

bool RecoveryCryptoFakeTpmBackendImpl::EncryptEccPrivateKey(
    const EllipticCurve& ec,
    const crypto::ScopedEC_KEY& own_key_pair,
    const std::optional<brillo::SecureBlob>& /*auth_value*/,
    brillo::SecureBlob* encrypted_own_priv_key) {
  const BIGNUM* own_priv_key_bn = EC_KEY_get0_private_key(own_key_pair.get());
  if (!own_priv_key_bn) {
    LOG(ERROR) << "Failed to get own_priv_key_bn";
    return false;
  }
  brillo::SecureBlob own_priv_key;
  if (!BigNumToSecureBlob(*own_priv_key_bn, ec.ScalarSizeInBytes(),
                          &own_priv_key)) {
    LOG(ERROR) << "Failed to convert BIGNUM to SecureBlob";
    return false;
  }
  *encrypted_own_priv_key = own_priv_key;
  return true;
}

crypto::ScopedEC_POINT
RecoveryCryptoFakeTpmBackendImpl::GenerateDiffieHellmanSharedSecret(
    const EllipticCurve& ec,
    const brillo::SecureBlob& encrypted_own_priv_key,
    const std::optional<brillo::SecureBlob>& /*auth_value*/,
    const EC_POINT& others_pub_point) {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return nullptr;
  }
  crypto::ScopedBIGNUM own_priv_key_bn =
      SecureBlobToBigNum(encrypted_own_priv_key);
  if (!own_priv_key_bn) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM";
    return nullptr;
  }
  crypto::ScopedEC_POINT point_dh =
      ec.Multiply(others_pub_point, *own_priv_key_bn, context.get());
  if (!point_dh) {
    LOG(ERROR) << "Failed to perform scalar multiplication";
    return nullptr;
  }
  return point_dh;
}

}  // namespace cryptorecovery
}  // namespace cryptohome
