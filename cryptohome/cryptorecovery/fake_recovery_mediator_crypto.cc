// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/base64url.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/ecdh_hkdf.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <libhwsec-foundation/crypto/error_util.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptorecovery/inclusion_proof_test_util.h"
#include "cryptohome/cryptorecovery/recovery_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

using ::hwsec_foundation::AesGcmDecrypt;
using ::hwsec_foundation::AesGcmEncrypt;
using ::hwsec_foundation::CreateBigNumContext;
using ::hwsec_foundation::CreateRandomBlob;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::EllipticCurve;
using ::hwsec_foundation::GenerateEcdhHkdfSymmetricKey;
using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::ScopedBN_CTX;
using ::hwsec_foundation::SecureBlobToBigNum;
using ::hwsec_foundation::VerifyRsaSignatureSha256;

namespace cryptohome {
namespace cryptorecovery {
namespace {

// Hard-coded development ledger info and keys. Do not use them in
// production! Keys were generated at random using
// EllipticCurve::GenerateKeysAsSecureBlobs method and converted to hex /
// base64.
constexpr char kFakeLedgerName[] = "FakeRecoveryMediatorLedgerName";
constexpr uint32_t kFakeLedgerPublicKeyHash = 1234567890;
constexpr char kFakeLedgerPublicKeyBase64[] =
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE0_"
    "pCyErRdVCEMEF3AlT2U4qLbTG1rReoeNnujUeO5iavvfn2Ag8h6vCkCjUUrKjlTCPPSSUQzkEO"
    "M0EFO03dkw==";
constexpr char kFakeLedgerPrivateKeyHex[] =
    "4201905D75370DE422EE988412F1AD2929D5252B49FF88B6286552865EE380F0";

// Hardcoded fake mediator and epoch public and private keys. Do not use them in
// production! Keys were generated at random using
// EllipticCurve::GenerateKeysAsSecureBlobs method and converted to hex.
constexpr char kFakeMediatorPublicKeyHex[] =
    "3059301306072A8648CE3D020106082A8648CE3D030107034200041C66FD08151D1C34EA50"
    "03F7C24557D2E4802535AA4F65EDBE3CD495CFE060387D00D5D25D859B26C5134F1AD00F22"
    "30EAB72A47F46DF23407CF68FB18C509DE";
constexpr char kFakeMediatorPrivateKeyHex[] =
    "B7A01DA624ECF448D9F7E1B07236EA2930A17C9A31AD60E43E01A8FEA934AB1C";
constexpr char kFakeEpochPrivateKeyHex[] =
    "2DC064DBE7473CE2E617C689E3D1D71568E1B09EA6CEC5CB4463A66C06F1B535";
constexpr char kFakeEpochPublicKeyHex[] =
    "3059301306072A8648CE3D020106082A8648CE3D030107034200045D8393CDEF671228CB0D"
    "8454BBB6F2AAA18E05834BB6DBBD05721FC81ED3BED33D08A8EFD44F6786CAE7ADEB8E26A3"
    "55CD9714F59C78F063A3CA3A7D74877A8A";

bool HexStringToBlob(const std::string& hex, brillo::Blob* blob) {
  std::string decoded;
  if (!base::HexStringToString(hex, &decoded)) {
    return false;
  }
  *blob = brillo::BlobFromString(decoded);
  return true;
}

brillo::Blob GetMediatorShareHkdfInfo() {
  return brillo::BlobFromString(RecoveryCrypto::kMediatorShareHkdfInfoValue);
}

brillo::Blob GetRequestPayloadPlainTextHkdfInfo() {
  return brillo::BlobFromString(
      RecoveryCrypto::kRequestPayloadPlainTextHkdfInfoValue);
}

brillo::Blob GetResponsePayloadPlainTextHkdfInfo() {
  return brillo::BlobFromString(
      RecoveryCrypto::kResponsePayloadPlainTextHkdfInfoValue);
}

bool GetRecoveryRequestFromProto(
    const CryptoRecoveryRpcRequest& recovery_request_proto,
    RecoveryRequest* recovery_request) {
  if (!recovery_request_proto.has_cbor_cryptorecoveryrequest()) {
    LOG(ERROR)
        << "No cbor_cryptorecoveryrequest field in recovery_request_proto";
    return false;
  }
  brillo::Blob recovery_request_cbor(
      recovery_request_proto.cbor_cryptorecoveryrequest().begin(),
      recovery_request_proto.cbor_cryptorecoveryrequest().end());
  if (!DeserializeRecoveryRequestFromCbor(recovery_request_cbor,
                                          recovery_request)) {
    LOG(ERROR) << "Unable to deserialize Recovery Request";
    return false;
  }
  return true;
}

bool GenerateRecoveryRequestProto(
    const ResponsePayload& response,
    CryptoRecoveryRpcResponse* recovery_response) {
  brillo::Blob recovery_response_cbor;
  if (!SerializeResponsePayloadToCbor(response, &recovery_response_cbor)) {
    LOG(ERROR) << "Failed to serialize Recovery Response to cbor";
    return false;
  }
  recovery_response->set_protocol_version(1);
  recovery_response->set_cbor_cryptorecoveryresponse(
      recovery_response_cbor.data(), recovery_response_cbor.size());
  return true;
}

brillo::Blob GetFakeLedgerPublicKeyBase64() {
  return brillo::BlobFromString(kFakeLedgerPublicKeyBase64);
}

bool GetFakeLedgerPublicKey(brillo::Blob* key) {
  std::string ledger_public_key_decoded;
  if (!base::Base64UrlDecode(std::string(kFakeLedgerPublicKeyBase64),
                             base::Base64UrlDecodePolicy::IGNORE_PADDING,
                             &ledger_public_key_decoded)) {
    LOG(ERROR) << "Failed at decoding from url base64.";
    return false;
  }
  *key = brillo::BlobFromString(ledger_public_key_decoded);
  return true;
}
}  // namespace

std::unique_ptr<FakeRecoveryMediatorCrypto>
FakeRecoveryMediatorCrypto::Create() {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return nullptr;
  }
  std::optional<EllipticCurve> ec =
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
bool FakeRecoveryMediatorCrypto::GetFakeLedgerPrivateKey(EC_KEY* key) {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  std::optional<EllipticCurve> ec =
      EllipticCurve::Create(RecoveryCrypto::kCurve, context.get());
  if (!ec) {
    LOG(ERROR) << "Failed to create EllipticCurve";
    return false;
  }

  if (EC_KEY_set_group(key, ec->GetGroup()) != 1) {
    LOG(ERROR) << "Failed to set EC group";
    return false;
  }

  brillo::SecureBlob priv_key_blob;
  if (!brillo::SecureBlob::HexStringToSecureBlob(kFakeLedgerPrivateKeyHex,
                                                 &priv_key_blob)) {
    LOG(ERROR) << "Failed to convert hex to SecureBlob";
    return false;
  }
  crypto::ScopedBIGNUM key_bn = SecureBlobToBigNum(priv_key_blob);
  if (!key_bn) {
    LOG(ERROR) << "Failed to convert key_bn to BIGNUM";
    return false;
  }
  if (!EC_KEY_set_private_key(key, key_bn.get())) {
    LOG(ERROR) << "Failed to set private key";
    return false;
  }

  brillo::Blob pub_key_blob;
  if (!GetFakeLedgerPublicKey(&pub_key_blob)) {
    LOG(ERROR) << "Failed to get public key";
    return false;
  }
  crypto::ScopedEC_POINT pub_key =
      ec->DecodeFromSpkiDer(pub_key_blob, context.get());
  if (!pub_key) {
    LOG(ERROR) << "Failed to convert pub_key_blob to EC_POINT";
    return false;
  }

  if (!EC_KEY_set_public_key(key, pub_key.get())) {
    LOG(ERROR) << "Failed to set public key";
    return false;
  }

  // EC_KEY_check_key performs various sanity checks on the EC_KEY object to
  // confirm that it is valid.
  if (!EC_KEY_check_key(key)) {
    LOG(ERROR) << "Failed to create valid key";
    return false;
  }

  return true;
}

// static
LedgerInfo FakeRecoveryMediatorCrypto::GetFakeLedgerInfo() {
  return LedgerInfo{.name = kFakeLedgerName,
                    .key_hash = kFakeLedgerPublicKeyHash,
                    .public_key = GetFakeLedgerPublicKeyBase64()};
}

// static
bool FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
    brillo::Blob* mediator_pub_key) {
  if (!HexStringToBlob(kFakeMediatorPublicKeyHex, mediator_pub_key)) {
    LOG(ERROR) << "Failed to convert hex to Blob";
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
    brillo::Blob* epoch_pub_key) {
  if (!HexStringToBlob(kFakeEpochPublicKeyHex, epoch_pub_key)) {
    LOG(ERROR) << "Failed to convert hex to Blob";
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

// static
bool FakeRecoveryMediatorCrypto::GetFakeEpochResponse(
    CryptoRecoveryEpochResponse* epoch_response) {
  brillo::Blob epoch_pub_key;
  if (!GetFakeEpochPublicKey(&epoch_pub_key)) {
    LOG(ERROR) << "Failed to get fake epoch public key";
    return false;
  }
  brillo::Blob epoch_metadata_cbor;
  cbor::Value::MapValue meta_data_cbor;
  meta_data_cbor.emplace("meta_data_cbor_key", "meta_data_cbor_value");
  if (!SerializeCborMapForTesting(meta_data_cbor, &epoch_metadata_cbor)) {
    LOG(ERROR) << "Failed to create epoch_metadata_cbor";
    return false;
  }
  epoch_response->set_protocol_version(1);
  epoch_response->set_epoch_pub_key(epoch_pub_key.data(), epoch_pub_key.size());
  epoch_response->set_epoch_meta_data(epoch_metadata_cbor.data(),
                                      epoch_metadata_cbor.size());
  return true;
}

bool FakeRecoveryMediatorCrypto::DecryptHsmPayloadPlainText(
    const brillo::SecureBlob& mediator_priv_key,
    const HsmPayload& hsm_payload,
    brillo::SecureBlob* plain_text) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  brillo::Blob publisher_pub_key_blob;
  if (!GetBytestringValueFromCborMapByKeyForTesting(hsm_payload.associated_data,
                                                    kPublisherPublicKey,
                                                    &publisher_pub_key_blob)) {
    LOG(ERROR) << "Unable to deserialize publisher_pub_key from hsm_payload";
    return false;
  }
  crypto::ScopedBIGNUM mediator_priv_key_bn =
      SecureBlobToBigNum(mediator_priv_key);
  if (!mediator_priv_key_bn) {
    LOG(ERROR) << "Failed to convert mediator_priv_key to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT publisher_pub_key =
      ec_.DecodeFromSpkiDer(publisher_pub_key_blob, context.get());
  if (!publisher_pub_key) {
    LOG(ERROR) << "Failed to convert publisher_pub_key_blob to EC_POINT";
    return false;
  }
  crypto::ScopedEC_POINT shared_secret_point = ComputeEcdhSharedSecretPoint(
      ec_, *publisher_pub_key, *mediator_priv_key_bn);
  if (!shared_secret_point) {
    LOG(ERROR) << "Failed to compute shared_secret_point";
    return false;
  }
  brillo::SecureBlob aes_gcm_key;
  if (!GenerateEcdhHkdfSymmetricKey(
          ec_, *shared_secret_point, publisher_pub_key_blob,
          GetMediatorShareHkdfInfo(),
          /*hkdf_salt=*/brillo::Blob(), RecoveryCrypto::kHkdfHash,
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
    const RequestPayload& request_payload,
    brillo::SecureBlob* plain_text) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }

  brillo::Blob salt;
  if (!GetBytestringValueFromCborMapByKeyForTesting(
          request_payload.associated_data, kRequestPayloadSalt, &salt)) {
    LOG(ERROR) << "Unable to deserialize salt from request_payload";
    return false;
  }
  HsmPayload hsm_payload;
  if (!GetHsmPayloadFromRequestAdForTesting(request_payload.associated_data,
                                            &hsm_payload)) {
    LOG(ERROR) << "Unable to deserialize hsm_payload from request_payload";
    return false;
  }
  brillo::Blob channel_pub_key_blob;
  if (!GetBytestringValueFromCborMapByKeyForTesting(hsm_payload.associated_data,
                                                    kChannelPublicKey,
                                                    &channel_pub_key_blob)) {
    LOG(ERROR) << "Unable to deserialize channel_pub_key from "
                  "hsm_payload.associated_data";
    return false;
  }

  crypto::ScopedBIGNUM epoch_priv_key_bn = SecureBlobToBigNum(epoch_priv_key);
  if (!epoch_priv_key_bn) {
    LOG(ERROR) << "Failed to convert epoch_priv_key to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT channel_pub_key =
      ec_.DecodeFromSpkiDer(channel_pub_key_blob, context.get());
  if (!channel_pub_key) {
    LOG(ERROR) << "Failed to convert channel_pub_key_blob to EC_POINT";
    return false;
  }
  crypto::ScopedEC_POINT shared_secret_point =
      ComputeEcdhSharedSecretPoint(ec_, *channel_pub_key, *epoch_priv_key_bn);
  if (!shared_secret_point) {
    LOG(ERROR) << "Failed to compute shared_secret_point";
    return false;
  }
  brillo::SecureBlob aes_gcm_key;
  if (!GenerateEcdhHkdfSymmetricKey(
          ec_, *shared_secret_point, channel_pub_key_blob,
          GetRequestPayloadPlainTextHkdfInfo(), salt, RecoveryCrypto::kHkdfHash,
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

bool FakeRecoveryMediatorCrypto::MediateHsmPayload(
    const brillo::SecureBlob& mediator_priv_key,
    const brillo::Blob& epoch_pub_key,
    const brillo::SecureBlob& epoch_priv_key,
    const brillo::Blob& ephemeral_pub_inv_key,
    const HsmPayload& hsm_payload,
    CryptoRecoveryRpcResponse* recovery_response_proto) const {
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

  HsmPlainText hsm_plain_text;
  if (!DeserializeHsmPlainTextFromCbor(hsm_plain_text_cbor, &hsm_plain_text)) {
    LOG(ERROR) << "Unable to deserialize hsm_plain_text_cbor";
    return false;
  }
  HsmAssociatedData hsm_associated_data;
  if (!DeserializeHsmAssociatedDataFromCbor(hsm_payload.associated_data,
                                            &hsm_associated_data)) {
    LOG(ERROR) << "Unable to deserialize hsm_payload.associated_data";
    return false;
  }

  crypto::ScopedBIGNUM mediator_share_bn =
      SecureBlobToBigNum(hsm_plain_text.mediator_share);
  if (!mediator_share_bn) {
    LOG(ERROR) << "Failed to convert SecureBlob to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT dealer_pub_point =
      ec_.DecodeFromSpkiDer(hsm_plain_text.dealer_pub_key, context.get());
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
      ec_.DecodeFromSpkiDer(ephemeral_pub_inv_key, context.get());
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
  crypto::ScopedEC_KEY mediated_key = ec_.PointToEccKey(*mediated_point);
  if (!ec_.EncodeToSpkiDer(mediated_key, &mediator_dh, context.get())) {
    LOG(ERROR) << "Failed to encode EC_POINT to SubjectPublicKeyInfo";
    return false;
  }

  brillo::Blob salt = CreateRandomBlob(RecoveryCrypto::kHkdfSaltLength);
  ResponsePayload response_payload;
  HsmResponseAssociatedData response_ad;
  response_ad.response_payload_salt = salt;

  crypto::ScopedEC_Key ledger_priv_key(EC_KEY_new());
  if (!ledger_priv_key) {
    LOG(ERROR) << "Failed to allocate EC_KEY structure";
    return false;
  }
  if (!GetFakeLedgerPrivateKey(ledger_priv_key.get())) {
    return false;
  }
  if (!GenerateFakeLedgerSignedProofForTesting(
          {ledger_priv_key.get()}, GetFakeLedgerInfo(),
          hsm_associated_data.onboarding_meta_data,
          &response_ad.ledger_signed_proof)) {
    LOG(ERROR) << "Unable to create fake ledger signed proof";
    return false;
  }
  if (!SerializeHsmResponseAssociatedDataToCbor(
          response_ad, &response_payload.associated_data)) {
    LOG(ERROR) << "Unable to serialize response payload associated data";
    return false;
  }

  brillo::SecureBlob response_plain_text_cbor;
  HsmResponsePlainText response_plain_text;
  response_plain_text.mediated_point = mediator_dh;
  response_plain_text.dealer_pub_key = hsm_plain_text.dealer_pub_key;
  response_plain_text.key_auth_value = hsm_plain_text.key_auth_value;
  if (!SerializeHsmResponsePlainTextToCbor(response_plain_text,
                                           &response_plain_text_cbor)) {
    LOG(ERROR) << "Unable to serialize response plain text";
    return false;
  }

  brillo::Blob channel_pub_key_blob;
  if (!GetBytestringValueFromCborMapByKeyForTesting(hsm_payload.associated_data,
                                                    kChannelPublicKey,
                                                    &channel_pub_key_blob)) {
    LOG(ERROR) << "Unable to deserialize channel_pub_key from hsm_payload";
    return false;
  }

  crypto::ScopedBIGNUM epoch_priv_key_bn = SecureBlobToBigNum(epoch_priv_key);
  if (!epoch_priv_key_bn) {
    LOG(ERROR) << "Failed to convert epoch_priv_key to BIGNUM";
    return false;
  }
  crypto::ScopedEC_POINT channel_pub_key =
      ec_.DecodeFromSpkiDer(channel_pub_key_blob, context.get());
  if (!channel_pub_key) {
    LOG(ERROR) << "Failed to convert channel_pub_key_blob to EC_POINT";
    return false;
  }
  crypto::ScopedEC_POINT shared_secret_point =
      ComputeEcdhSharedSecretPoint(ec_, *channel_pub_key, *epoch_priv_key_bn);
  if (!shared_secret_point) {
    LOG(ERROR) << "Failed to compute shared_secret_point";
    return false;
  }
  brillo::SecureBlob aes_gcm_key;
  // The static nature of `channel_pub_key` (G*s) and `epoch_pub_key` (G*r)
  // requires the need to utilize a randomized salt value in the HKDF
  // computation.
  if (!GenerateEcdhHkdfSymmetricKey(ec_, *shared_secret_point, epoch_pub_key,
                                    GetResponsePayloadPlainTextHkdfInfo(), salt,
                                    RecoveryCrypto::kHkdfHash,
                                    kAesGcm256KeySize, &aes_gcm_key)) {
    LOG(ERROR)
        << "Failed to generate ECDH+HKDF recipient key for Recovery Request "
           "plaintext encryption";
    return false;
  }

  if (!AesGcmEncrypt(response_plain_text_cbor, response_payload.associated_data,
                     aes_gcm_key, &response_payload.iv, &response_payload.tag,
                     &response_payload.cipher_text)) {
    LOG(ERROR) << "Failed to perform AES-GCM encryption of response_payload";
    return false;
  }

  ResponsePayload recovery_response;
  recovery_response = std::move(response_payload);
  if (!GenerateRecoveryRequestProto(recovery_response,
                                    recovery_response_proto)) {
    LOG(ERROR) << "Failed to generate Recovery Response proto";
    return false;
  }
  return true;
}

bool FakeRecoveryMediatorCrypto::MediateRequestPayload(
    const brillo::Blob& epoch_pub_key,
    const brillo::SecureBlob& epoch_priv_key,
    const brillo::SecureBlob& mediator_priv_key,
    const CryptoRecoveryRpcRequest& recovery_request_proto,
    CryptoRecoveryRpcResponse* recovery_response_proto) const {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    LOG(ERROR) << "Failed to allocate BN_CTX structure";
    return false;
  }
  // Parse out the rsa_signature in Recovery Request
  RecoveryRequest recovery_request;
  if (!GetRecoveryRequestFromProto(recovery_request_proto, &recovery_request)) {
    LOG(ERROR) << "Couldn't get recovery request from recovery_request_proto";
    return false;
  }
  // Parse out the rsa_public_key, which is in Hsm Associated Data. Hsm
  // Associated Data is in Hsm Payload, and it is in the Associated Data of
  // Request Payload
  RequestPayload request_payload;
  if (!DeserializeRecoveryRequestPayloadFromCbor(
          recovery_request.request_payload, &request_payload)) {
    LOG(ERROR) << "Failed to deserialize Request payload.";
    return false;
  }
  HsmPayload hsm_payload;
  if (!GetHsmPayloadFromRequestAdForTesting(request_payload.associated_data,
                                            &hsm_payload)) {
    LOG(ERROR) << "Unable to extract hsm_payload from request_payload";
    return false;
  }
  HsmAssociatedData hsm_associated_data;
  if (!DeserializeHsmAssociatedDataFromCbor(hsm_payload.associated_data,
                                            &hsm_associated_data)) {
    LOG(ERROR) << "Unable to deserialize hsm_associated_data_cbor";
    return false;
  }

  // If the recovery request is sent from devices with TPM2.0, no RSA signature
  // is attached to be verified and the public key wrapped in AD1 would be
  // empty.
  if (!hsm_associated_data.rsa_public_key.empty() ||
      !recovery_request.rsa_signature.empty()) {
    // Verify RSA signature with RSA public key and request payload
    if (!VerifyRsaSignatureSha256(recovery_request.request_payload,
                                  recovery_request.rsa_signature,
                                  hsm_associated_data.rsa_public_key)) {
      LOG(ERROR)
          << "Unable to initiate verifying rsa signature in request_payload";
      return false;
    }
  }

  brillo::SecureBlob request_plain_text_cbor;
  if (!DecryptRequestPayloadPlainText(epoch_priv_key, request_payload,
                                      &request_plain_text_cbor)) {
    LOG(ERROR) << "Unable to decrypt plain text in request_payload";
    return false;
  }

  RecoveryRequestPlainText plain_text;
  if (!DeserializeRecoveryRequestPlainTextFromCbor(request_plain_text_cbor,
                                                   &plain_text)) {
    LOG(ERROR)
        << "Unable to deserialize Recovery Request request_plain_text_cbor";
    return false;
  }

  if (!MediateHsmPayload(mediator_priv_key, epoch_pub_key, epoch_priv_key,
                         plain_text.ephemeral_pub_inv_key, hsm_payload,
                         recovery_response_proto)) {
    LOG(ERROR) << "Unable to mediate hsm_payload";
    return false;
  }

  return true;
}

}  // namespace cryptorecovery
}  // namespace cryptohome
