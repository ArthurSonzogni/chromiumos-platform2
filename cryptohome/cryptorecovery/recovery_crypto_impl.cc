// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/ecdh_hkdf.h>
#include <libhwsec-foundation/crypto/error_util.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <openssl/ec.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"

using ::hwsec_foundation::AesGcmDecrypt;
using ::hwsec_foundation::AesGcmEncrypt;
using ::hwsec_foundation::BigNumToSecureBlob;
using ::hwsec_foundation::CreateBigNum;
using ::hwsec_foundation::CreateBigNumContext;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::EllipticCurve;
using ::hwsec_foundation::HkdfHash;
using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::ScopedBN_CTX;

namespace cryptohome {
namespace cryptorecovery {

namespace {

brillo::SecureBlob GetRecoveryKeyHkdfInfo() {
  return brillo::SecureBlob("CryptoHome Wrapping Key");
}

brillo::SecureBlob GetMediatorShareHkdfInfo() {
  return brillo::SecureBlob(RecoveryCrypto::kMediatorShareHkdfInfoValue);
}

brillo::SecureBlob GetRequestPayloadPlainTextHkdfInfo() {
  return brillo::SecureBlob(
      RecoveryCrypto::kRequestPayloadPlainTextHkdfInfoValue);
}

brillo::SecureBlob GetResponsePayloadPlainTextHkdfInfo() {
  return brillo::SecureBlob(
      RecoveryCrypto::kResponsePayloadPlainTextHkdfInfoValue);
}

bool GenerateRecoveryRequestAssociatedData(
    const HsmPayload& hsm_payload,
    const RequestMetadata& request_meta_data,
    const CryptoRecoveryEpochResponse& epoch_response,
    RecoveryRequestAssociatedData* request_ad) {
  request_ad->hsm_payload = hsm_payload;
  request_ad->request_meta_data = request_meta_data;
  if (!epoch_response.has_epoch_meta_data()) {
    LOG(ERROR) << "Epoch response doesn't have epoch metadata";
    return false;
  }
  brillo::SecureBlob epoch_meta_data(epoch_response.epoch_meta_data().begin(),
                                     epoch_response.epoch_meta_data().end());
  if (!DeserializeEpochMetadataFromCbor(epoch_meta_data,
                                        &request_ad->epoch_meta_data)) {
    LOG(ERROR) << "Failed to deserialize epoch metadata from cbor";
    return false;
  }
  if (!epoch_response.has_epoch_pub_key()) {
    LOG(ERROR) << "Epoch response doesn't have epoch public key";
    return false;
  }
  request_ad->epoch_pub_key.assign(epoch_response.epoch_pub_key().begin(),
                                   epoch_response.epoch_pub_key().end());
  request_ad->request_payload_salt =
      CreateSecureRandomBlob(RecoveryCrypto::kHkdfSaltLength);
  return true;
}

bool GenerateRecoveryRequestProto(const RecoveryRequest& request,
                                  CryptoRecoveryRpcRequest* recovery_request) {
  brillo::SecureBlob request_cbor;
  if (!SerializeRecoveryRequestToCbor(request, &request_cbor)) {
    LOG(ERROR) << "Failed to serialize Recovery Request to CBOR";
    return false;
  }
  recovery_request->set_protocol_version(1);
  recovery_request->set_cbor_cryptorecoveryrequest(request_cbor.data(),
                                                   request_cbor.size());
  return true;
}

bool GetRecoveryResponseFromProto(
    const CryptoRecoveryRpcResponse& recovery_response_proto,
    RecoveryResponse* recovery_response) {
  if (!recovery_response_proto.has_cbor_cryptorecoveryresponse()) {
    LOG(ERROR)
        << "No cbor_cryptorecoveryresponse field in recovery_response_proto";
    return false;
  }
  brillo::SecureBlob recovery_response_cbor(
      recovery_response_proto.cbor_cryptorecoveryresponse().begin(),
      recovery_response_proto.cbor_cryptorecoveryresponse().end());
  if (!DeserializeRecoveryResponseFromCbor(recovery_response_cbor,
                                           recovery_response)) {
    LOG(ERROR) << "Unable to deserialize Recovery Response from CBOR";
    return false;
  }
  return true;
}

}  // namespace

std::unique_ptr<RecoveryCryptoImpl> RecoveryCryptoImpl::Create(
    RecoveryCryptoTpmBackend* tpm_backend) {
  DCHECK(tpm_backend);
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return nullptr;
  }
  std::optional<EllipticCurve> ec =
      EllipticCurve::Create(kCurve, context.get());
  if (!ec) {
    LOG(ERROR) << "Failed to create EllipticCurve";
    return nullptr;
  }
  // using wrapUnique because the constructor is private and make_unique calls
  // the constructor externally
  return base::WrapUnique(new RecoveryCryptoImpl(std::move(*ec), tpm_backend));
}

RecoveryCryptoImpl::RecoveryCryptoImpl(EllipticCurve ec,
                                       RecoveryCryptoTpmBackend* tpm_backend)
    : ec_(std::move(ec)), tpm_backend_(tpm_backend) {
  DCHECK(tpm_backend_);
}

RecoveryCryptoImpl::~RecoveryCryptoImpl() = default;

bool RecoveryCryptoImpl::EncryptMediatorShare(
    const brillo::SecureBlob& mediator_pub_key,
    const brillo::SecureBlob& mediator_share,
    RecoveryCrypto::EncryptedMediatorShare* encrypted_ms,
    BN_CTX* context) const {
  brillo::SecureBlob ephemeral_priv_key;
  if (!ec_.GenerateKeysAsSecureBlobs(&encrypted_ms->ephemeral_pub_key,
                                     &ephemeral_priv_key, context)) {
    LOG(ERROR) << "Failed to generate EC keys for mediator share encryption";
    return false;
  }

  brillo::SecureBlob shared_secret_point;
  if (!ComputeEcdhSharedSecretPoint(ec_, mediator_pub_key, ephemeral_priv_key,
                                    &shared_secret_point)) {
    LOG(ERROR) << "Failed to compute shared point from mediator_pub_key and "
                  "ephemeral_priv_key";
    return false;
  }
  brillo::SecureBlob aes_gcm_key;
  // |hkdf_salt| can be empty here because the input already has a high entropy.
  // Bruteforce attacks are not an issue here and as we generate an ephemeral
  // key as input to HKDF the output will already be non-deterministic.
  if (!GenerateEcdhHkdfSymmetricKey(
          ec_, shared_secret_point, encrypted_ms->ephemeral_pub_key,
          GetMediatorShareHkdfInfo(), /*hkdf_salt=*/brillo::SecureBlob(),
          kHkdfHash, kAesGcm256KeySize, &aes_gcm_key)) {
    LOG(ERROR) << "Failed to generate ECDH+HKDF sender keys for mediator share "
                  "encryption";
    return false;
  }

  // Dispose shared_secret_point after used
  shared_secret_point.clear();
  // Dispose private key.
  ephemeral_priv_key.clear();

  if (!AesGcmEncrypt(mediator_share, /*ad=*/std::nullopt, aes_gcm_key,
                     &encrypted_ms->iv, &encrypted_ms->tag,
                     &encrypted_ms->encrypted_data)) {
    LOG(ERROR) << "Failed to perform AES-GCM encryption of the mediator share";
    return false;
  }

  return true;
}

bool RecoveryCryptoImpl::GenerateRecoveryKey(
    const crypto::ScopedEC_POINT& recovery_pub_point,
    const crypto::ScopedEC_KEY& dealer_key_pair,
    brillo::SecureBlob* recovery_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  const BIGNUM* dealer_priv_key =
      EC_KEY_get0_private_key(dealer_key_pair.get());
  if (!dealer_priv_key) {
    LOG(ERROR) << "Failed to get dealer_priv_key";
    return false;
  }
  crypto::ScopedEC_POINT point_dh =
      ec_.Multiply(*recovery_pub_point, *dealer_priv_key, context.get());
  if (!point_dh) {
    LOG(ERROR) << "Failed to perform point multiplication of "
                  "recovery_pub_point and dealer_priv_key";
    return false;
  }
  // Get point's affine X coordinate.
  crypto::ScopedBIGNUM recovery_dh_x = CreateBigNum();
  if (!recovery_dh_x) {
    LOG(ERROR) << "Failed to allocate BIGNUM";
    return false;
  }
  if (!ec_.GetAffineCoordinates(*point_dh, context.get(), recovery_dh_x.get(),
                                /*y=*/nullptr)) {
    LOG(ERROR) << "Failed to get point_dh x coordinate";
    return false;
  }

  brillo::SecureBlob hkdf_secret;
  // Convert X coordinate to fixed-size blob.
  if (!BigNumToSecureBlob(*recovery_dh_x, ec_.AffineCoordinateSizeInBytes(),
                          &hkdf_secret)) {
    LOG(ERROR) << "Failed to convert recovery_dh_x BIGNUM to SecureBlob";
    return false;
  }
  const EC_POINT* dealer_pub_point =
      EC_KEY_get0_public_key(dealer_key_pair.get());
  if (!dealer_pub_point) {
    LOG(ERROR) << "Failed to get dealer_pub_point";
    return false;
  }
  brillo::SecureBlob dealer_pub_key;
  if (!ec_.PointToSecureBlob(*dealer_pub_point, &dealer_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert dealer_pub_key to a SecureBlob";
    return false;
  }
  if (!ComputeHkdfWithInfoSuffix(hkdf_secret, GetRecoveryKeyHkdfInfo(),
                                 dealer_pub_key, /*salt=*/brillo::SecureBlob(),
                                 HkdfHash::kSha256, /*result_len=*/0,
                                 recovery_key)) {
    LOG(ERROR) << "Failed to compute HKDF of recovery_dh";
    return false;
  }
  return true;
}

bool RecoveryCryptoImpl::GenerateEphemeralKey(
    brillo::SecureBlob* ephemeral_pub_key,
    brillo::SecureBlob* ephemeral_inv_pub_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  // Generate ephemeral key pair {`ephemeral_secret`, `ephemeral_pub_key`} ({x,
  // G*x}), and the inverse public key G*-x.
  crypto::ScopedBIGNUM ephemeral_priv_key_bn =
      ec_.RandomNonZeroScalar(context.get());
  if (!ephemeral_priv_key_bn) {
    LOG(ERROR) << "Failed to generate ephemeral_priv_key_bn";
    return false;
  }
  crypto::ScopedEC_POINT ephemeral_pub_point =
      ec_.MultiplyWithGenerator(*ephemeral_priv_key_bn, context.get());
  if (!ephemeral_pub_point) {
    LOG(ERROR) << "Failed to perform MultiplyWithGenerator operation for "
                  "ephemeral_priv_key_bn";
    return false;
  }
  if (!ec_.PointToSecureBlob(*ephemeral_pub_point, ephemeral_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert ephemeral_pub_point to a SecureBlob";
    return false;
  }

  if (!ec_.InvertPoint(ephemeral_pub_point.get(), context.get())) {
    LOG(ERROR) << "Failed to invert the ephemeral_pub_point";
    return false;
  }
  if (!ec_.PointToSecureBlob(*ephemeral_pub_point, ephemeral_inv_pub_key,
                             context.get())) {
    LOG(ERROR)
        << "Failed to convert inverse ephemeral_pub_point to a SecureBlob";
    return false;
  }
  return true;
}

bool RecoveryCryptoImpl::GenerateRecoveryRequest(
    const HsmPayload& hsm_payload,
    const RequestMetadata& request_meta_data,
    const CryptoRecoveryEpochResponse& epoch_response,
    const brillo::SecureBlob& encrypted_rsa_priv_key,
    const brillo::SecureBlob& encrypted_channel_priv_key,
    const brillo::SecureBlob& channel_pub_key,
    CryptoRecoveryRpcRequest* recovery_request,
    brillo::SecureBlob* ephemeral_pub_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  RequestPayload request_payload;
  RecoveryRequestAssociatedData request_ad;
  if (!GenerateRecoveryRequestAssociatedData(hsm_payload, request_meta_data,
                                             epoch_response, &request_ad)) {
    LOG(ERROR) << "Failed to generate recovery request associated data";
    return false;
  }
  if (!SerializeRecoveryRequestAssociatedDataToCbor(
          request_ad, &request_payload.associated_data)) {
    LOG(ERROR)
        << "Failed to serialize recovery request associated data to cbor";
    return false;
  }

  brillo::SecureBlob epoch_pub_key;
  epoch_pub_key.assign(epoch_response.epoch_pub_key().begin(),
                       epoch_response.epoch_pub_key().end());

  crypto::ScopedEC_POINT epoch_pub_point =
      ec_.SecureBlobToPoint(epoch_pub_key, context.get());
  if (!epoch_pub_point) {
    LOG(ERROR) << "Failed to convert epoch_pub_key SecureBlob to EC_POINT";
    return false;
  }
  // Performs scalar multiplication of epoch_pub_point and channel_priv_key.
  // Here we don't use key_auth_value generated from GenerateKeyAuthValue()
  // because key_auth_value is unavailable and will only be recovered from the
  // decrypted response afterward
  crypto::ScopedEC_POINT shared_secret_point =
      tpm_backend_->GenerateDiffieHellmanSharedSecret(
          ec_, encrypted_channel_priv_key, /*auth_value=*/std::nullopt,
          *epoch_pub_point);
  if (!shared_secret_point) {
    LOG(ERROR) << "Failed to compute shared point from epoch_pub_point and "
                  "channel_priv_key";
    return false;
  }
  brillo::SecureBlob shared_secret_point_blob;
  if (!ec_.PointToSecureBlob(*shared_secret_point, &shared_secret_point_blob,
                             context.get())) {
    LOG(ERROR) << "Failed to convert shared_secret_point to a SecureBlob";
    return false;
  }
  brillo::SecureBlob aes_gcm_key;
  // The static nature of `channel_pub_key` (G*s) and `epoch_pub_key` (G*r)
  // requires the need to utilize a randomized salt value in the HKDF
  // computation.
  if (!GenerateEcdhHkdfSymmetricKey(
          ec_, shared_secret_point_blob, channel_pub_key,
          GetRequestPayloadPlainTextHkdfInfo(), request_ad.request_payload_salt,
          kHkdfHash, kAesGcm256KeySize, &aes_gcm_key)) {
    LOG(ERROR) << "Failed to generate ECDH+HKDF sender keys for recovery "
                  "request plain text encryption";
    return false;
  }

  // Dispose shared_secret_point_blob after used
  shared_secret_point.reset();
  shared_secret_point_blob.clear();

  brillo::SecureBlob ephemeral_inverse_pub_key;
  if (!GenerateEphemeralKey(ephemeral_pub_key, &ephemeral_inverse_pub_key)) {
    LOG(ERROR) << "Failed to generate Ephemeral keys";
    return false;
  }

  brillo::SecureBlob plain_text_cbor;
  RecoveryRequestPlainText plain_text;
  plain_text.ephemeral_pub_inv_key = ephemeral_inverse_pub_key;
  if (!SerializeRecoveryRequestPlainTextToCbor(plain_text, &plain_text_cbor)) {
    LOG(ERROR) << "Failed to generate Recovery Request plain text cbor";
    return false;
  }

  if (!AesGcmEncrypt(plain_text_cbor, request_payload.associated_data,
                     aes_gcm_key, &request_payload.iv, &request_payload.tag,
                     &request_payload.cipher_text)) {
    LOG(ERROR) << "Failed to perform AES-GCM encryption of plain_text_cbor for "
                  "recovery request";
    return false;
  }

  // Sign the request payload with the rsa private key
  brillo::SecureBlob request_payload_blob;
  if (!SerializeRecoveryRequestPayloadToCbor(request_payload,
                                             &request_payload_blob)) {
    LOG(ERROR) << "Failed to serialize Recovery Request payload";
    return false;
  }
  brillo::SecureBlob rsa_signature;
  if (!tpm_backend_->SignRequestPayload(encrypted_rsa_priv_key,
                                        request_payload_blob, &rsa_signature)) {
    LOG(ERROR) << "Failed to sign Recovery Request payload";
    return false;
  }

  RecoveryRequest request;
  request.request_payload = std::move(request_payload_blob);
  request.rsa_signature = std::move(rsa_signature);
  if (!GenerateRecoveryRequestProto(request, recovery_request)) {
    LOG(ERROR) << "Failed to generate Recovery Request proto";
    return false;
  }
  return true;
}

bool RecoveryCryptoImpl::GenerateHsmPayload(
    const brillo::SecureBlob& mediator_pub_key,
    const OnboardingMetadata& onboarding_metadata,
    HsmPayload* hsm_payload,
    brillo::SecureBlob* encrypted_rsa_priv_key,
    brillo::SecureBlob* encrypted_destination_share,
    brillo::SecureBlob* recovery_key,
    brillo::SecureBlob* channel_pub_key,
    brillo::SecureBlob* encrypted_channel_priv_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  // Generate dealer key pair.
  crypto::ScopedEC_KEY dealer_key_pair = ec_.GenerateKey(context.get());
  if (!dealer_key_pair) {
    LOG(ERROR) << "Failed to generate dealer key pair";
    return false;
  }
  // Generate two shares and a secret equal to the sum.
  // Loop until the sum of two shares is non-zero (modulo order).
  crypto::ScopedEC_KEY destination_share_key_pair =
      ec_.GenerateKey(context.get());
  if (!destination_share_key_pair) {
    LOG(ERROR) << "Failed to generate destination share key pair";
    return false;
  }
  const BIGNUM* destination_share_bn =
      EC_KEY_get0_private_key(destination_share_key_pair.get());
  if (!destination_share_bn) {
    LOG(ERROR) << "Failed to generate destination_share secret";
    return false;
  }
  crypto::ScopedBIGNUM secret;
  crypto::ScopedBIGNUM mediator_share_bn;
  do {
    mediator_share_bn = ec_.RandomNonZeroScalar(context.get());
    if (!mediator_share_bn) {
      LOG(ERROR) << "Failed to generate mediator_share secret";
      return false;
    }
    secret =
        ec_.ModAdd(*mediator_share_bn, *destination_share_bn, context.get());
    if (!secret) {
      LOG(ERROR) << "Failed to perform modulo addition of mediator_share "
                    "and destination_share";
      return false;
    }
  } while (BN_is_zero(secret.get()));

  brillo::SecureBlob key_auth_value = tpm_backend_->GenerateKeyAuthValue();
  if (!tpm_backend_->EncryptEccPrivateKey(ec_, destination_share_key_pair,
                                          key_auth_value,
                                          encrypted_destination_share)) {
    LOG(ERROR) << "Failed to encrypt destination share";
    return false;
  }

  crypto::ScopedEC_POINT recovery_pub_point =
      ec_.MultiplyWithGenerator(*secret, context.get());
  if (!recovery_pub_point) {
    LOG(ERROR)
        << "Failed to perform MultiplyWithGenerator operation for the secret";
    return false;
  }

  // Generate RSA key pair
  brillo::SecureBlob rsa_public_key_der;
  if (!tpm_backend_->GenerateRsaKeyPair(encrypted_rsa_priv_key,
                                        &rsa_public_key_der)) {
    LOG(ERROR) << "Error creating PCR bound signing key.";
    return false;
  }

  // Generate channel key pair.
  crypto::ScopedEC_KEY channel_key_pair = ec_.GenerateKey(context.get());
  if (!channel_key_pair) {
    LOG(ERROR) << "Failed to generate channel key pair";
    return false;
  }
  const EC_POINT* channel_pub_point =
      EC_KEY_get0_public_key(channel_key_pair.get());
  if (!channel_pub_point) {
    LOG(ERROR) << "Failed to get channel_pub_point";
    return false;
  }
  if (!ec_.PointToSecureBlob(*channel_pub_point, channel_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert channel_pub_key to a SecureBlob";
    return false;
  }
  // Here we don't use key_auth_value generated from GenerateKeyAuthValue()
  // because key_auth_value will be unavailable when encrypted_channel_priv_key
  // is unsealed from TPM1.2
  if (!tpm_backend_->EncryptEccPrivateKey(ec_, channel_key_pair,
                                          /*auth_value=*/std::nullopt,
                                          encrypted_channel_priv_key)) {
    LOG(ERROR) << "Failed to encrypt channel_priv_key";
    return false;
  }

  // Construct associated data for HSM payload: AD = CBOR({publisher_pub_key,
  // channel_pub_key, rsa_pub_key, onboarding_metadata}).
  brillo::SecureBlob publisher_priv_key;
  brillo::SecureBlob publisher_pub_key;
  if (!GenerateHsmAssociatedData(*channel_pub_key, rsa_public_key_der,
                                 onboarding_metadata,
                                 &hsm_payload->associated_data,
                                 &publisher_priv_key, &publisher_pub_key)) {
    LOG(ERROR) << "Failed to generate HSM associated data cbor";
    return false;
  }

  // Construct plain text for HSM payload PT = CBOR({dealer_pub_key,
  // mediator_share, kav}).
  const EC_POINT* dealer_pub_point =
      EC_KEY_get0_public_key(dealer_key_pair.get());
  if (!dealer_pub_point) {
    LOG(ERROR) << "Failed to get dealer_pub_point";
    return false;
  }
  brillo::SecureBlob dealer_pub_key;
  if (!ec_.PointToSecureBlob(*dealer_pub_point, &dealer_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert dealer_pub_key to a SecureBlob";
    return false;
  }
  brillo::SecureBlob mediator_share;
  if (!BigNumToSecureBlob(*mediator_share_bn, ec_.ScalarSizeInBytes(),
                          &mediator_share)) {
    LOG(ERROR) << "Failed to convert mediator_share to a SecureBlob";
    return false;
  }

  brillo::SecureBlob plain_text_cbor;
  HsmPlainText hsm_plain_text;
  hsm_plain_text.mediator_share = mediator_share;
  hsm_plain_text.dealer_pub_key = dealer_pub_key;
  hsm_plain_text.key_auth_value = key_auth_value;
  if (!SerializeHsmPlainTextToCbor(hsm_plain_text, &plain_text_cbor)) {
    LOG(ERROR) << "Failed to generate HSM plain text cbor";
    return false;
  }

  brillo::SecureBlob shared_secret_point;
  if (!ComputeEcdhSharedSecretPoint(ec_, mediator_pub_key, publisher_priv_key,
                                    &shared_secret_point)) {
    LOG(ERROR) << "Failed to compute shared point from mediator_pub_key and "
                  "publisher_priv_key";
    return false;
  }
  brillo::SecureBlob aes_gcm_key;
  // |hkdf_salt| can be empty here because the input already has a high entropy.
  // Bruteforce attacks are not an issue here and as we generate an ephemeral
  // key as input to HKDF the output will already be non-deterministic.
  if (!GenerateEcdhHkdfSymmetricKey(ec_, shared_secret_point, publisher_pub_key,
                                    GetMediatorShareHkdfInfo(),
                                    /*hkdf_salt=*/brillo::SecureBlob(),
                                    kHkdfHash, kAesGcm256KeySize,
                                    &aes_gcm_key)) {
    LOG(ERROR) << "Failed to generate ECDH+HKDF sender keys for HSM plain text "
                  "encryption";
    return false;
  }

  if (!AesGcmEncrypt(plain_text_cbor, hsm_payload->associated_data, aes_gcm_key,
                     &hsm_payload->iv, &hsm_payload->tag,
                     &hsm_payload->cipher_text)) {
    LOG(ERROR) << "Failed to perform AES-GCM encryption of HSM plain text";
    return false;
  }

  // Cleanup: all intermediate secrets must be securely disposed at the end of
  // HSM payload generation.
  aes_gcm_key.clear();
  shared_secret_point.clear();
  plain_text_cbor.clear();
  mediator_share.clear();
  dealer_pub_key.clear();
  publisher_pub_key.clear();
  publisher_priv_key.clear();

  GenerateRecoveryKey(recovery_pub_point, dealer_key_pair, recovery_key);
  return true;
}

bool RecoveryCryptoImpl::RecoverDestination(
    const brillo::SecureBlob& dealer_pub_key,
    const brillo::SecureBlob& key_auth_value,
    const brillo::SecureBlob& encrypted_destination_share,
    const brillo::SecureBlob& ephemeral_pub_key,
    const brillo::SecureBlob& mediated_publisher_pub_key,
    brillo::SecureBlob* destination_recovery_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  crypto::ScopedEC_POINT dealer_pub_point =
      ec_.SecureBlobToPoint(dealer_pub_key, context.get());
  if (!dealer_pub_point) {
    LOG(ERROR) << "Failed to convert dealer_pub_point SecureBlob to EC_POINT";
    return false;
  }
  crypto::ScopedEC_POINT mediated_point =
      ec_.SecureBlobToPoint(mediated_publisher_pub_key, context.get());
  if (!mediated_point) {
    LOG(ERROR) << "Failed to convert mediated_point SecureBlob to EC_POINT";
    return false;
  }
  // Performs addition of mediated_point and ephemeral_pub_point.
  crypto::ScopedEC_POINT ephemeral_pub_point =
      ec_.SecureBlobToPoint(ephemeral_pub_key, context.get());
  if (!ephemeral_pub_point) {
    LOG(ERROR)
        << "Failed to convert ephemeral_pub_point SecureBlob to EC_POINT";
    return false;
  }
  crypto::ScopedEC_POINT mediator_dh =
      ec_.Add(*mediated_point, *ephemeral_pub_point, context.get());
  if (!mediator_dh) {
    LOG(ERROR) << "Failed to add mediated_point and ephemeral_pub_point";
    return false;
  }
  // Performs scalar multiplication of dealer_pub_point and destination_share.
  crypto::ScopedEC_POINT point_dh =
      tpm_backend_->GenerateDiffieHellmanSharedSecret(
          ec_, encrypted_destination_share, key_auth_value, *dealer_pub_point);
  if (!point_dh) {
    LOG(ERROR) << "Failed to perform scalar multiplication of dealer_pub_point "
                  "and destination_share";
    return false;
  }
  crypto::ScopedEC_POINT point_dest =
      ec_.Add(*point_dh, *mediator_dh, context.get());
  if (!point_dest) {
    LOG(ERROR)
        << "Failed to perform point addition of point_dh and mediator_dh";
    return false;
  }
  // Get point's affine X coordinate.
  crypto::ScopedBIGNUM destination_dh_x = CreateBigNum();
  if (!destination_dh_x) {
    LOG(ERROR) << "Failed to allocate BIGNUM";
    return false;
  }
  if (!ec_.GetAffineCoordinates(*point_dest, context.get(),
                                destination_dh_x.get(), /*y=*/nullptr)) {
    LOG(ERROR) << "Failed to get point_dest x coordinate";
    return false;
  }
  brillo::SecureBlob hkdf_secret;
  // Convert X coordinate to fixed-size blob.
  if (!BigNumToSecureBlob(*destination_dh_x, ec_.AffineCoordinateSizeInBytes(),
                          &hkdf_secret)) {
    LOG(ERROR) << "Failed to convert destination_dh_x BIGNUM to SecureBlob";
    return false;
  }
  if (!ComputeHkdfWithInfoSuffix(hkdf_secret, GetRecoveryKeyHkdfInfo(),
                                 dealer_pub_key, /*salt=*/brillo::SecureBlob(),
                                 HkdfHash::kSha256, /*result_len=*/0,
                                 destination_recovery_key)) {
    LOG(ERROR) << "Failed to compute HKDF of destination_dh";
    return false;
  }
  return true;
}

bool RecoveryCryptoImpl::DecryptResponsePayload(
    const brillo::SecureBlob& encrypted_channel_priv_key,
    const CryptoRecoveryEpochResponse& epoch_response,
    const CryptoRecoveryRpcResponse& recovery_response_proto,
    HsmResponsePlainText* response_plain_text) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  RecoveryResponse recovery_response;
  if (!GetRecoveryResponseFromProto(recovery_response_proto,
                                    &recovery_response)) {
    LOG(ERROR) << "Unable to deserialize Recovery Response from CBOR";
    return false;
  }

  HsmResponseAssociatedData response_ad;
  if (!DeserializeHsmResponseAssociatedDataFromCbor(
          recovery_response.response_payload.associated_data, &response_ad)) {
    LOG(ERROR) << "Unable to deserialize Response payload associated data";
    return false;
  }

  if (!epoch_response.has_epoch_pub_key()) {
    LOG(ERROR) << "Epoch response doesn't have epoch public key";
    return false;
  }
  brillo::SecureBlob epoch_pub_key(epoch_response.epoch_pub_key());
  crypto::ScopedEC_POINT epoch_pub_point =
      ec_.SecureBlobToPoint(epoch_pub_key, context.get());
  if (!epoch_pub_point) {
    LOG(ERROR) << "Failed to convert epoch_pub_key SecureBlob to EC_POINT";
    return false;
  }
  // Performs scalar multiplication of epoch_pub_point and channel_priv_key.
  // Here we don't use key_auth_value generated from GenerateKeyAuthValue()
  // because key_auth_value is unavailable and will only be recovered from the
  // decrypted response afterward
  crypto::ScopedEC_POINT shared_secret_point =
      tpm_backend_->GenerateDiffieHellmanSharedSecret(
          ec_, encrypted_channel_priv_key, /*auth_value=*/std::nullopt,
          *epoch_pub_point);
  if (!shared_secret_point) {
    LOG(ERROR) << "Failed to compute shared point from epoch_pub_point and "
                  "channel_priv_key";
    return false;
  }
  brillo::SecureBlob shared_secret_point_blob;
  if (!ec_.PointToSecureBlob(*shared_secret_point, &shared_secret_point_blob,
                             context.get())) {
    LOG(ERROR) << "Failed to convert shared_secret_point to a SecureBlob";
    return false;
  }
  brillo::SecureBlob aes_gcm_key;
  if (!GenerateEcdhHkdfSymmetricKey(
          ec_, shared_secret_point_blob, epoch_pub_key,
          GetResponsePayloadPlainTextHkdfInfo(),
          response_ad.response_payload_salt, RecoveryCrypto::kHkdfHash,
          kAesGcm256KeySize, &aes_gcm_key)) {
    LOG(ERROR) << "Failed to generate ECDH+HKDF recipient key for response "
                  "plain text decryption";
    return false;
  }

  // Dispose shared_secret_point after used
  shared_secret_point.reset();
  shared_secret_point_blob.clear();

  brillo::SecureBlob response_plain_text_cbor;
  if (!AesGcmDecrypt(recovery_response.response_payload.cipher_text,
                     recovery_response.response_payload.associated_data,
                     recovery_response.response_payload.tag, aes_gcm_key,
                     recovery_response.response_payload.iv,
                     &response_plain_text_cbor)) {
    LOG(ERROR) << "Failed to perform AES-GCM decryption of response plain text";
    return false;
  }
  if (!DeserializeHsmResponsePlainTextFromCbor(response_plain_text_cbor,
                                               response_plain_text)) {
    LOG(ERROR) << "Failed to deserialize Response plain text";
    return false;
  }
  return true;
}

bool RecoveryCryptoImpl::GenerateHsmAssociatedData(
    const brillo::SecureBlob& channel_pub_key,
    const brillo::SecureBlob& rsa_pub_key,
    const OnboardingMetadata& onboarding_metadata,
    brillo::SecureBlob* hsm_associated_data,
    brillo::SecureBlob* publisher_priv_key,
    brillo::SecureBlob* publisher_pub_key) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  // Generate publisher key pair.
  crypto::ScopedEC_KEY publisher_key_pair = ec_.GenerateKey(context.get());
  if (!publisher_key_pair) {
    LOG(ERROR) << "Failed to generate publisher key pair";
    return false;
  }

  // Construct associated data for HSM payload: AD = CBOR({publisher_pub_key,
  // channel_pub_key, rsa_pub_key, onboarding_metadata}).
  const EC_POINT* publisher_pub_point =
      EC_KEY_get0_public_key(publisher_key_pair.get());
  if (!publisher_pub_point) {
    LOG(ERROR) << "Failed to get publisher_pub_point";
    return false;
  }
  if (!ec_.PointToSecureBlob(*publisher_pub_point, publisher_pub_key,
                             context.get())) {
    LOG(ERROR) << "Failed to convert publisher_pub_key to a SecureBlob";
    return false;
  }
  const BIGNUM* publisher_priv_secret =
      EC_KEY_get0_private_key(publisher_key_pair.get());
  if (!publisher_priv_secret) {
    LOG(ERROR) << "Failed to get publisher_priv_secret";
    return false;
  }
  if (!BigNumToSecureBlob(*publisher_priv_secret, ec_.ScalarSizeInBytes(),
                          publisher_priv_key)) {
    LOG(ERROR) << "Failed to convert publisher_priv_key to a SecureBlob";
    return false;
  }
  HsmAssociatedData hsm_ad;
  hsm_ad.publisher_pub_key = *publisher_pub_key;
  hsm_ad.channel_pub_key = channel_pub_key;
  hsm_ad.rsa_public_key = rsa_pub_key;
  hsm_ad.onboarding_meta_data = onboarding_metadata;
  if (!SerializeHsmAssociatedDataToCbor(hsm_ad, hsm_associated_data)) {
    LOG(ERROR) << "Failed to generate HSM associated data cbor";
    return false;
  }
  return true;
}

}  // namespace cryptorecovery
}  // namespace cryptohome
