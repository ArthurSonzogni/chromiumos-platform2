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

#include "cryptohome/crypto/aes.h"
#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/ecdh_hkdf.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/error_util.h"
#include "cryptohome/crypto/recovery_crypto.h"
#include "cryptohome/crypto/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/crypto/recovery_crypto_util.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptohome_common.h"

namespace cryptohome {
namespace {

const char kFakeHsmMetaData[] = "fake-hsm-metadata";

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

}  // namespace

// Hardcoded fake mediator and epoch public and private keys. Do not use them in
// production! Keys were generated at random using
// EllipticCurve::GenerateKeysAsSecureBlobs method and converted to hex.
static const char kFakeMediatorPublicKeyHex[] =
    "041C66FD08151D1C34EA5003F7C24557D2E4802535AA4F65EDBE3CD495CFE060387D00D5D2"
    "5D859B26C5134F1AD00F2230EAB72A47F46DF23407CF68FB18C509DE";
static const char kFakeMediatorPrivateKeyHex[] =
    "B7A01DA624ECF448D9F7E1B07236EA2930A17C9A31AD60E43E01A8FEA934AB1C";
static const char kFakeEpochPrivateKeyHex[] =
    "2DC064DBE7473CE2E617C689E3D1D71568E1B09EA6CEC5CB4463A66C06F1B535";
static const char kFakeEpochPublicKeyHex[] =
    "045D8393CDEF671228CB0D8454BBB6F2AAA18E05834BB6DBBD05721FC81ED3BED33D08A8EF"
    "D44F6786CAE7ADEB8E26A355CD9714F59C78F063A3CA3A7D74877A8A";

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

// static
bool FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
    brillo::SecureBlob* mediator_pub_key) {
  if (!brillo::SecureBlob::HexStringToSecureBlob(kFakeMediatorPublicKeyHex,
                                                 mediator_pub_key)) {
    LOG(ERROR) << "Failed to convert hex to SecureBlob";
    return false;
  }
  return true;
}

// static
bool FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
    brillo::SecureBlob* mediator_priv_key) {
  if (!brillo::SecureBlob::HexStringToSecureBlob(kFakeMediatorPrivateKeyHex,
                                                 mediator_priv_key)) {
    LOG(ERROR) << "Failed to convert hex to SecureBlob";
    return false;
  }
  return true;
}

// static
bool FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(
    brillo::SecureBlob* epoch_pub_key) {
  if (!brillo::SecureBlob::HexStringToSecureBlob(kFakeEpochPublicKeyHex,
                                                 epoch_pub_key)) {
    LOG(ERROR) << "Failed to convert hex to SecureBlob";
    return false;
  }
  return true;
}

// static
bool FakeRecoveryMediatorCrypto::GetFakeEpochPrivateKey(
    brillo::SecureBlob* epoch_priv_key) {
  if (!brillo::SecureBlob::HexStringToSecureBlob(kFakeEpochPrivateKeyHex,
                                                 epoch_priv_key)) {
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
    LOG(ERROR) << "Failed to generate ECDH+HKDF recipient key for mediator "
                  "share decryption";
    return false;
  }

  if (!AesGcmDecrypt(encrypted_mediator_share.encrypted_data,
                     /*ad=*/base::nullopt, encrypted_mediator_share.tag,
                     aes_gcm_key, encrypted_mediator_share.iv,
                     mediator_share)) {
    LOG(ERROR) << "Failed to perform AES-GCM decryption";
    return false;
  }

  return true;
}

bool FakeRecoveryMediatorCrypto::DecryptHsmPayloadPlainText(
    const brillo::SecureBlob& mediator_priv_key,
    const cryptorecovery::HsmPayload& hsm_payload,
    brillo::SecureBlob* plain_text) const {
  brillo::SecureBlob publisher_pub_key;
  if (!GetHsmCborMapByKeyForTesting(hsm_payload.associated_data,
                                    kPublisherPublicKey, &publisher_pub_key)) {
    LOG(ERROR) << "Unable to deserialize publisher_pub_key from hsm_payload";
    return false;
  }
  brillo::SecureBlob aes_gcm_key;
  if (!GenerateEcdhHkdfRecipientKey(
          ec_, mediator_priv_key, publisher_pub_key, GetMediatorShareHkdfInfo(),
          /*hkdf_salt=*/brillo::SecureBlob(), RecoveryCrypto::kHkdfHash,
          kAesGcm256KeySize, &aes_gcm_key)) {
    LOG(ERROR) << "Failed to generate ECDH+HKDF recipient key for HSM "
                  "plaintext decryption";
    return false;
  }

  if (!AesGcmDecrypt(hsm_payload.cipher_text, hsm_payload.associated_data,
                     hsm_payload.tag, aes_gcm_key, hsm_payload.iv,
                     plain_text)) {
    LOG(ERROR) << "Failed to perform AES-GCM decryption";
    return false;
  }

  return true;
}

bool FakeRecoveryMediatorCrypto::DecryptRequestPayloadPlainText(
    const brillo::SecureBlob& epoch_priv_key,
    const cryptorecovery::RequestPayload& request_payload,
    brillo::SecureBlob* plain_text) const {
  brillo::SecureBlob salt;
  if (!GetHsmCborMapByKeyForTesting(request_payload.associated_data,
                                    kRequestPayloadSalt, &salt)) {
    LOG(ERROR) << "Unable to deserialize salt from request_payload";
    return false;
  }
  cryptorecovery::HsmPayload hsm_payload;
  if (!GetHsmPayloadFromRequestAdForTesting(request_payload.associated_data,
                                            &hsm_payload)) {
    LOG(ERROR) << "Unable to deserialize hsm_payload from request_payload";
    return false;
  }
  brillo::SecureBlob channel_pub_key;
  if (!GetHsmCborMapByKeyForTesting(hsm_payload.associated_data,
                                    kChannelPublicKey, &channel_pub_key)) {
    LOG(ERROR) << "Unable to deserialize channel_pub_key from "
                  "hsm_payload.associated_data";
    return false;
  }

  brillo::SecureBlob aes_gcm_key;
  if (!GenerateEcdhHkdfRecipientKey(ec_, epoch_priv_key, channel_pub_key,
                                    GetRequestPayloadPlainTextHkdfInfo(), salt,
                                    RecoveryCrypto::kHkdfHash,
                                    kAesGcm256KeySize, &aes_gcm_key)) {
    LOG(ERROR) << "Failed to generate ECDH+HKDF recipient key for request "
                  "payload decryption";
    return false;
  }

  if (!AesGcmDecrypt(request_payload.cipher_text,
                     request_payload.associated_data, request_payload.tag,
                     aes_gcm_key, request_payload.iv, plain_text)) {
    LOG(ERROR) << "Failed to perform AES-GCM decryption of request_payload";
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

bool FakeRecoveryMediatorCrypto::MediateHsmPayload(
    const brillo::SecureBlob& mediator_priv_key,
    const brillo::SecureBlob& epoch_pub_key,
    const brillo::SecureBlob& epoch_priv_key,
    const brillo::SecureBlob& ephemeral_pub_inv_key,
    const cryptorecovery::HsmPayload& hsm_payload,
    cryptorecovery::ResponsePayload* response_payload) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  brillo::SecureBlob hsm_plain_text_cbor;
  if (!DecryptHsmPayloadPlainText(mediator_priv_key, hsm_payload,
                                  &hsm_plain_text_cbor)) {
    LOG(ERROR) << "Unable to decrypt hsm_plain_text_cbor in hsm_payload";
    return false;
  }

  cryptorecovery::HsmPlainText hsm_plain_text;
  if (!DeserializeHsmPlainTextFromCbor(hsm_plain_text_cbor, &hsm_plain_text)) {
    LOG(ERROR) << "Unable to deserialize hsm_plain_text_cbor";
    return false;
  }

  crypto::ScopedBIGNUM mediator_share_bn =
      SecureBlobToBigNum(hsm_plain_text.mediator_share);
  if (!mediator_share_bn) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT dealer_pub_point =
      ec_.SecureBlobToPoint(hsm_plain_text.dealer_pub_key, context.get());
  if (!dealer_pub_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT";
    return false;
  }
  // Performs scalar multiplication of dealer_pub_key and mediator_share.
  brillo::SecureBlob mediator_dh;
  crypto::ScopedEC_POINT mediator_dh_point =
      ec_.Multiply(*dealer_pub_point, *mediator_share_bn, context.get());
  if (!mediator_dh_point) {
    LOG(ERROR) << "Failed to perform scalar multiplication";
    return false;
  }
  // Perform addition of mediator_dh_point and ephemeral_pub_inv_key.
  crypto::ScopedEC_POINT ephemeral_pub_inv_point =
      ec_.SecureBlobToPoint(ephemeral_pub_inv_key, context.get());
  if (!ephemeral_pub_inv_point) {
    LOG(ERROR) << "Failed to convert SecureBlob to EC_POINT";
    return false;
  }
  crypto::ScopedEC_POINT mediated_point =
      ec_.Add(*mediator_dh_point, *ephemeral_pub_inv_point, context.get());
  if (!mediated_point) {
    LOG(ERROR) << "Failed to add mediator_dh_point and ephemeral_pub_inv_point";
    return false;
  }
  if (!ec_.PointToSecureBlob(*mediated_point, &mediator_dh, context.get())) {
    LOG(ERROR) << "Failed to convert EC_POINT to SecureBlob";
    return false;
  }

  brillo::SecureBlob salt =
      CreateSecureRandomBlob(RecoveryCrypto::kHkdfSaltLength);
  cryptorecovery::HsmResponseAssociatedData response_ad;
  response_ad.response_meta_data = brillo::SecureBlob(kFakeHsmMetaData);
  response_ad.response_payload_salt = salt;
  if (!SerializeHsmResponseAssociatedDataToCbor(
          response_ad, &response_payload->associated_data)) {
    LOG(ERROR) << "Unable to serialize response payload associated data";
    return false;
  }

  brillo::SecureBlob response_plain_text_cbor;
  cryptorecovery::HsmResponsePlainText response_plain_text;
  response_plain_text.mediated_point = mediator_dh;
  response_plain_text.dealer_pub_key = hsm_plain_text.dealer_pub_key;
  response_plain_text.key_auth_value = brillo::SecureBlob();
  if (!SerializeHsmResponsePlainTextToCbor(response_plain_text,
                                           &response_plain_text_cbor)) {
    LOG(ERROR) << "Unable to serialize response plain text";
    return false;
  }

  brillo::SecureBlob channel_pub_key;
  if (!GetHsmCborMapByKeyForTesting(hsm_payload.associated_data,
                                    kChannelPublicKey, &channel_pub_key)) {
    LOG(ERROR) << "Unable to deserialize channel_pub_key from hsm_payload";
    return false;
  }

  brillo::SecureBlob aes_gcm_key;
  // The static nature of `channel_pub_key` (G*s) and `epoch_pub_key` (G*r)
  // requires the need to utilize a randomized salt value in the HKDF
  // computation.
  if (!GenerateEcdhHkdfSenderKey(
          ec_, channel_pub_key, epoch_pub_key, epoch_priv_key,
          GetResponsePayloadPlainTextHkdfInfo(), salt,
          RecoveryCrypto::kHkdfHash, kAesGcm256KeySize, &aes_gcm_key)) {
    LOG(ERROR)
        << "Failed to generate ECDH+HKDF recipient key for Recovery Request "
           "plaintext encryption";
    return false;
  }

  if (!AesGcmEncrypt(response_plain_text_cbor,
                     response_payload->associated_data, aes_gcm_key,
                     &response_payload->iv, &response_payload->tag,
                     &response_payload->cipher_text)) {
    LOG(ERROR) << "Failed to perform AES-GCM encryption of response_payload";
    return false;
  }

  return true;
}

bool FakeRecoveryMediatorCrypto::MediateRequestPayload(
    const brillo::SecureBlob& epoch_pub_key,
    const brillo::SecureBlob& epoch_priv_key,
    const brillo::SecureBlob& mediator_priv_key,
    const cryptorecovery::RequestPayload& request_payload,
    cryptorecovery::ResponsePayload* response_payload) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  brillo::SecureBlob request_plain_text_cbor;
  if (!DecryptRequestPayloadPlainText(epoch_priv_key, request_payload,
                                      &request_plain_text_cbor)) {
    LOG(ERROR) << "Unable to decrypt plain text in request_payload";
    return false;
  }

  cryptorecovery::RecoveryRequestPlainText plain_text;
  if (!DeserializeRecoveryRequestPlainTextFromCbor(request_plain_text_cbor,
                                                   &plain_text)) {
    LOG(ERROR)
        << "Unable to deserialize Recovery Request request_plain_text_cbor";
    return false;
  }

  cryptorecovery::HsmPayload hsm_payload;
  if (!GetHsmPayloadFromRequestAdForTesting(request_payload.associated_data,
                                            &hsm_payload)) {
    LOG(ERROR) << "Unable to extract hsm_payload from request_payload";
    return false;
  }

  return MediateHsmPayload(mediator_priv_key, epoch_pub_key, epoch_priv_key,
                           plain_text.ephemeral_pub_inv_key, hsm_payload,
                           response_payload);
}

}  // namespace cryptohome
