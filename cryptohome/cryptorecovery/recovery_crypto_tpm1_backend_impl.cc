// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/recovery_crypto_tpm1_backend_impl.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

#include <base/check.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec/error/error.h>
#include <libhwsec/error/tpm1_error.h>
#include <libhwsec/error/tpm_retry_handler.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/ecdh_hkdf.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha) - needs tss.h

#include "cryptohome/tpm.h"

namespace cryptohome {
namespace cryptorecovery {

using hwsec::StatusChain;
using hwsec::TPMErrorBase;
using hwsec_foundation::BigNumToSecureBlob;
using hwsec_foundation::CreateBigNumContext;
using hwsec_foundation::CreateSecureRandomBlob;
using hwsec_foundation::EllipticCurve;
using hwsec_foundation::ScopedBN_CTX;

namespace {
// Size of the auth_value blob to be randomly generated.
//
// The choice of this constant is dictated by the desire to provide sufficient
// amount of entropy as the authorization secret for the TPM_Seal command (but
// with taking into account that this authorization value is hashed by SHA-1
// by Trousers anyway).
constexpr int kAuthValueSizeBytes = 32;

// Creates a DER encoded RSA public key using SubjectPublicKeyInfo structure.
//
// Parameters
//   rsa_public_key_pkcs1_der - A serialized PKCS#1 RSAPublicKey in DER format.
//   rsa_public_key_spki_der - The same public key using SubjectPublicKeyInfo
//   structure in DER encoded form.
bool ConvertPkcs1DerToSpkiDer(
    const brillo::SecureBlob& rsa_public_key_pkcs1_der,
    brillo::SecureBlob* rsa_public_key_spki_der) {
  const unsigned char* rsa_public_key_pkcs1_der_data =
      rsa_public_key_pkcs1_der.data();
  crypto::ScopedRSA rsa(d2i_RSAPublicKey(/*RSA=*/nullptr,
                                         &rsa_public_key_pkcs1_der_data,
                                         rsa_public_key_pkcs1_der.size()));
  if (!rsa.get()) {
    LOG(ERROR) << "Failed to decode public key.";
    return false;
  }

  int der_length = i2d_RSA_PUBKEY(rsa.get(), NULL);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to DER-encode public key using SubjectPublicKeyInfo.";
    return false;
  }
  rsa_public_key_spki_der->resize(der_length);
  unsigned char* der_buffer = rsa_public_key_spki_der->data();
  der_length = i2d_RSA_PUBKEY(rsa.get(), &der_buffer);
  if (der_length < 0) {
    LOG(ERROR) << "Failed to DER-encode public key using SubjectPublicKeyInfo.";
    return false;
  }
  rsa_public_key_spki_der->resize(der_length);
  return true;
}
}  // namespace

RecoveryCryptoTpm1BackendImpl::RecoveryCryptoTpm1BackendImpl(Tpm* tpm_impl)
    : tpm_impl_(tpm_impl) {
  DCHECK(tpm_impl_);
}

RecoveryCryptoTpm1BackendImpl::~RecoveryCryptoTpm1BackendImpl() = default;

brillo::SecureBlob RecoveryCryptoTpm1BackendImpl::GenerateKeyAuthValue() {
  return CreateSecureRandomBlob(kAuthValueSizeBytes);
}

bool RecoveryCryptoTpm1BackendImpl::EncryptEccPrivateKey(
    const EllipticCurve& ec,
    const crypto::ScopedEC_KEY& own_key_pair,
    const std::optional<brillo::SecureBlob>& auth_value,
    brillo::SecureBlob* encrypted_own_priv_key) {
  const BIGNUM* own_priv_key_bn = EC_KEY_get0_private_key(own_key_pair.get());
  if (!own_priv_key_bn || !ec.IsScalarValid(*own_priv_key_bn)) {
    LOG(ERROR) << "Scalar is not valid";
    return false;
  }
  // Convert one's own private key to blob.
  brillo::SecureBlob own_priv_key;
  if (!BigNumToSecureBlob(*own_priv_key_bn, ec.ScalarSizeInBytes(),
                          &own_priv_key)) {
    LOG(ERROR) << "Failed to convert BIGNUM to SecureBlob";
    return false;
  }

  // If auth_value is not provided, one's own private key will not be sealed
  // and if auth_value is provided, one's own private key will be sealed.
  if (!auth_value.has_value()) {
    *encrypted_own_priv_key = own_priv_key;
  } else if (StatusChain<TPMErrorBase> err =
                 tpm_impl_->SealToPcrWithAuthorization(
                     own_priv_key, auth_value.value(), /*pcr_map=*/{{}},
                     encrypted_own_priv_key)) {
    LOG(ERROR) << "Error sealing the blob: " << err;
    return false;
  }
  return true;
}

crypto::ScopedEC_POINT
RecoveryCryptoTpm1BackendImpl::GenerateDiffieHellmanSharedSecret(
    const EllipticCurve& ec,
    const brillo::SecureBlob& encrypted_own_priv_key,
    const std::optional<brillo::SecureBlob>& auth_value,
    const EC_POINT& others_pub_point) {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return nullptr;
  }
  // Unseal crypto secret with auth_value
  brillo::SecureBlob unencrypted_own_priv_key;

  // If auth_value is not provided, one's own private key will not be unsealed
  // and if auth_value is provided, one's own private key will be unsealed.
  if (!auth_value.has_value()) {
    unencrypted_own_priv_key = encrypted_own_priv_key;
  } else if (StatusChain<TPMErrorBase> err = tpm_impl_->UnsealWithAuthorization(
                 /*preload_handle=*/std::nullopt, encrypted_own_priv_key,
                 auth_value.value(),
                 /* pcr_map=*/{}, &unencrypted_own_priv_key)) {
    LOG(ERROR) << "Failed to unseal the secret value: " << err;
    return nullptr;
  }

  // Convert the other party's public key from EC_POINT to SecureBlob
  brillo::SecureBlob others_pub_point_blob;
  if (!ec.PointToSecureBlob(others_pub_point, &others_pub_point_blob,
                            context.get())) {
    LOG(ERROR) << "Failed to convert others_pub_point_blob to a SecureBlob";
    return nullptr;
  }
  // Calculate the shared secret from one's own private key and the other
  // party's public key
  brillo::SecureBlob point_dh_blob;
  if (!ComputeEcdhSharedSecretPoint(ec, others_pub_point_blob,
                                    unencrypted_own_priv_key, &point_dh_blob)) {
    LOG(ERROR) << "Failed to compute shared point from mediator_pub_key and "
                  "publisher_priv_key";
    return nullptr;
  }
  crypto::ScopedEC_POINT point_dh =
      ec.SecureBlobToPoint(point_dh_blob, context.get());
  if (!point_dh) {
    LOG(ERROR) << "Failed to convert point_dh_blob to EC_POINT";
    return nullptr;
  }

  return point_dh;
}

bool RecoveryCryptoTpm1BackendImpl::GenerateRsaKeyPair(
    brillo::SecureBlob* encrypted_rsa_private_key,
    brillo::SecureBlob* rsa_public_key_spki_der) {
  CHECK(encrypted_rsa_private_key);
  CHECK(rsa_public_key_spki_der);
  brillo::SecureBlob creation_blob;
  brillo::SecureBlob rsa_public_key_pkcs1_der;

  // Generate RSA key pair
  // TODO(b/196191918): Get the real pcr_map
  if (!tpm_impl_->CreatePCRBoundKey(
          /*pcr_map=*/{{}}, AsymmetricKeyUsage::kSignKey,
          encrypted_rsa_private_key, &rsa_public_key_pkcs1_der,
          /*creation_blob=*/&creation_blob)) {
    LOG(ERROR) << "Error creating PCR bound signing key.";
    return false;
  }

  if (!ConvertPkcs1DerToSpkiDer(rsa_public_key_pkcs1_der,
                                rsa_public_key_spki_der)) {
    LOG(ERROR) << "Error convert RSA public key from PKCS#1 to "
                  "SubjectPublicKeyInfo structure.";
    return false;
  }

  return true;
}

bool RecoveryCryptoTpm1BackendImpl::SignRequestPayload(
    const brillo::SecureBlob& encrypted_rsa_private_key,
    const brillo::SecureBlob& request_payload,
    brillo::SecureBlob* signature) {
  CHECK(signature);
  // TODO(b/196191918): Get the real bound_pcr_index
  if (!tpm_impl_->Sign(encrypted_rsa_private_key, request_payload,
                       /*bound_pcr_index=*/kNotBoundToPCR, signature)) {
    LOG(ERROR) << "Error signing with PCR bound key.";
    return false;
  }
  return true;
}

}  // namespace cryptorecovery
}  // namespace cryptohome
