// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"

#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <chromeos/cbor/reader.h>
#include <chromeos/cbor/values.h>
#include <chromeos/cbor/writer.h>

namespace cryptohome {
namespace cryptorecovery {

namespace {

bool SerializeCborMap(const cbor::Value::MapValue& cbor_map,
                      brillo::SecureBlob* blob_cbor) {
  base::Optional<std::vector<uint8_t>> serialized =
      cbor::Writer::Write(cbor::Value(std::move(cbor_map)));
  if (!serialized) {
    LOG(ERROR) << "Failed to serialize CBOR Map.";
    return false;
  }
  blob_cbor->assign(serialized.value().begin(), serialized.value().end());
  return true;
}

base::Optional<cbor::Value> ReadCborMap(const brillo::SecureBlob& map_cbor) {
  cbor::Reader::DecoderError error_code;
  base::Optional<cbor::Value> cbor_response =
      cbor::Reader::Read(map_cbor, &error_code);
  if (!cbor_response) {
    LOG(ERROR) << "Unable to create CBOR reader.";
    return base::nullopt;
  }
  if (error_code != cbor::Reader::DecoderError::CBOR_NO_ERROR) {
    LOG(ERROR) << "Error when parsing CBOR input: "
               << cbor::Reader::ErrorCodeToString(error_code);
    return base::nullopt;
  }
  if (!cbor_response->is_map()) {
    LOG(ERROR) << "CBOR input is not a map.";
    return base::nullopt;
  }
  return cbor_response;
}

bool FindBytestringValueInCborMap(const cbor::Value::MapValue& map,
                                  const std::string& key,
                                  brillo::SecureBlob* blob) {
  const auto entry = map.find(cbor::Value(key));
  if (entry == map.end()) {
    LOG(ERROR) << "No `" + key + "` entry in the CBOR map.";
    return false;
  }
  if (!entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted `" + key + "` entry in the CBOR map.";
    return false;
  }

  blob->assign(entry->second.GetBytestring().begin(),
               entry->second.GetBytestring().end());
  return true;
}

cbor::Value::MapValue CreateHsmPayloadMap(const HsmPayload& payload) {
  cbor::Value::MapValue result;

  result.emplace(kAeadCipherText, payload.cipher_text);
  result.emplace(kAeadAd, payload.associated_data);
  result.emplace(kAeadIv, payload.iv);
  result.emplace(kAeadTag, payload.tag);

  return result;
}

}  // namespace

// !!! DO NOT MODIFY !!!
// All the consts below are used as keys in the CBOR blog exchanged with the
// server and must be synced with the server/HSM implementation (or the other
// party will not be able to decrypt the data).
const char kRecoveryCryptoRequestSchemaVersion[] = "schema_version";
const char kMediatorShare[] = "mediator_share";
const char kMediatedPoint[] = "mediated_point";
const char kKeyAuthValue[] = "key_auth_value";
const char kDealerPublicKey[] = "dealer_pub_key";
const char kPublisherPublicKey[] = "publisher_pub_key";
const char kChannelPublicKey[] = "channel_pub_key";
const char kRsaPublicKey[] = "epoch_rsa_sig_pkey";
const char kOnboardingMetaData[] = "onboarding_meta_data";
const char kHsmAead[] = "hsm_aead";
const char kAeadCipherText[] = "ct";
const char kAeadAd[] = "ad";
const char kAeadIv[] = "iv";
const char kAeadTag[] = "tag";
const char kRequestMetaData[] = "request_meta_data";
const char kEpochPublicKey[] = "epoch_pub_key";
const char kEphemeralPublicInvKey[] = "ephemeral_pub_inv_key";
const char kRequestPayloadSalt[] = "request_salt";
const char kResponseMetaData[] = "response_meta_data";
const char kResponsePayloadSalt[] = "response_salt";

const int kProtocolVersion = 1;

bool SerializeHsmAssociatedDataToCbor(const HsmAssociatedData& args,
                                      brillo::SecureBlob* ad_cbor) {
  cbor::Value::MapValue ad_map;

  ad_map.emplace(kPublisherPublicKey, args.publisher_pub_key);
  ad_map.emplace(kChannelPublicKey, args.channel_pub_key);
  ad_map.emplace(kRsaPublicKey, args.rsa_public_key);
  ad_map.emplace(kOnboardingMetaData, args.onboarding_meta_data);

  if (!SerializeCborMap(ad_map, ad_cbor)) {
    LOG(ERROR) << "Failed to serialize HSM Associated Data to CBOR";
    return false;
  }
  return true;
}

bool SerializeRecoveryRequestAssociatedDataToCbor(
    const RecoveryRequestAssociatedData& args,
    brillo::SecureBlob* request_ad_cbor) {
  cbor::Value::MapValue ad_map;

  ad_map.emplace(kRecoveryCryptoRequestSchemaVersion,
                 /*schema_version=*/kProtocolVersion);
  ad_map.emplace(kHsmAead, CreateHsmPayloadMap(args.hsm_payload));
  ad_map.emplace(kRequestMetaData, args.request_meta_data);
  ad_map.emplace(kEpochPublicKey, args.epoch_pub_key);
  ad_map.emplace(kRequestPayloadSalt, args.request_payload_salt);

  if (!SerializeCborMap(ad_map, request_ad_cbor)) {
    LOG(ERROR)
        << "Failed to serialize Recovery Request Associated Data to CBOR";
    return false;
  }
  return true;
}

bool SerializeHsmResponseAssociatedDataToCbor(
    const HsmResponseAssociatedData& response_ad,
    brillo::SecureBlob* response_ad_cbor) {
  cbor::Value::MapValue ad_map;

  ad_map.emplace(kResponseMetaData, response_ad.response_meta_data);
  ad_map.emplace(kResponsePayloadSalt, response_ad.response_payload_salt);

  if (!SerializeCborMap(ad_map, response_ad_cbor)) {
    LOG(ERROR) << "Failed to serialize HSM Response Associated Data to CBOR";
    return false;
  }
  return true;
}

bool SerializeHsmPlainTextToCbor(const HsmPlainText& plain_text,
                                 brillo::SecureBlob* plain_text_cbor) {
  cbor::Value::MapValue pt_map;

  pt_map.emplace(kDealerPublicKey, plain_text.dealer_pub_key);
  pt_map.emplace(kMediatorShare, plain_text.mediator_share);
  pt_map.emplace(kKeyAuthValue, plain_text.key_auth_value);
  if (!SerializeCborMap(pt_map, plain_text_cbor)) {
    LOG(ERROR) << "Failed to serialize HSM plain text to CBOR";
    return false;
  }
  return true;
}

bool SerializeRecoveryRequestPlainTextToCbor(
    const RecoveryRequestPlainText& plain_text,
    brillo::SecureBlob* plain_text_cbor) {
  cbor::Value::MapValue pt_map;

  pt_map.emplace(kEphemeralPublicInvKey, plain_text.ephemeral_pub_inv_key);

  if (!SerializeCborMap(pt_map, plain_text_cbor)) {
    LOG(ERROR) << "Failed to serialize Recovery Request plain text to CBOR";
    return false;
  }
  return true;
}

bool SerializeHsmResponsePlainTextToCbor(const HsmResponsePlainText& plain_text,
                                         brillo::SecureBlob* response_cbor) {
  cbor::Value::MapValue pt_map;

  pt_map.emplace(kDealerPublicKey, plain_text.dealer_pub_key);
  pt_map.emplace(kMediatedPoint, plain_text.mediated_point);
  pt_map.emplace(kKeyAuthValue, plain_text.key_auth_value);
  if (!SerializeCborMap(pt_map, response_cbor)) {
    LOG(ERROR) << "Failed to serialize HSM responce payload to CBOR";
    return false;
  }
  return true;
}

bool DeserializeHsmPlainTextFromCbor(
    const brillo::SecureBlob& hsm_plain_text_cbor,
    HsmPlainText* hsm_plain_text) {
  const auto& cbor = ReadCborMap(hsm_plain_text_cbor);
  if (!cbor) {
    return false;
  }

  const cbor::Value::MapValue& response_map = cbor->GetMap();
  brillo::SecureBlob dealer_pub_key;
  if (!FindBytestringValueInCborMap(response_map, kDealerPublicKey,
                                    &dealer_pub_key)) {
    LOG(ERROR) << "Failed to get dealer public key from the HSM response map.";
    return false;
  }
  brillo::SecureBlob mediator_share;
  if (!FindBytestringValueInCborMap(response_map, kMediatorShare,
                                    &mediator_share)) {
    LOG(ERROR) << "Failed to get mediator share from the HSM response map.";
    return false;
  }
  brillo::SecureBlob key_auth_value;
  if (!FindBytestringValueInCborMap(response_map, kKeyAuthValue,
                                    &key_auth_value)) {
    LOG(ERROR) << "Failed to get key auth value from the HSM response map.";
    return false;
  }

  hsm_plain_text->dealer_pub_key = std::move(dealer_pub_key);
  hsm_plain_text->mediator_share = std::move(mediator_share);
  hsm_plain_text->key_auth_value = std::move(key_auth_value);
  return true;
}

bool DeserializeRecoveryRequestPlainTextFromCbor(
    const brillo::SecureBlob& request_plain_text_cbor,
    RecoveryRequestPlainText* request_plain_text) {
  const auto& cbor = ReadCborMap(request_plain_text_cbor);
  if (!cbor) {
    return false;
  }

  const cbor::Value::MapValue& request_map = cbor->GetMap();
  brillo::SecureBlob ephemeral_pub_inv_key;
  if (!FindBytestringValueInCborMap(request_map, kEphemeralPublicInvKey,
                                    &ephemeral_pub_inv_key)) {
    LOG(ERROR) << "Failed to get ephemeral public inverse key from the "
                  "Recovery Request map.";
    return false;
  }

  request_plain_text->ephemeral_pub_inv_key = std::move(ephemeral_pub_inv_key);
  return true;
}

bool DeserializeHsmResponsePlainTextFromCbor(
    const brillo::SecureBlob& response_payload_cbor,
    HsmResponsePlainText* response_payload) {
  const auto& cbor = ReadCborMap(response_payload_cbor);
  if (!cbor) {
    return false;
  }

  const cbor::Value::MapValue& response_map = cbor->GetMap();
  brillo::SecureBlob dealer_pub_key;
  if (!FindBytestringValueInCborMap(response_map, kDealerPublicKey,
                                    &dealer_pub_key)) {
    LOG(ERROR) << "Failed to get dealer public key from the HSM response map.";
    return false;
  }
  brillo::SecureBlob mediated_point;
  if (!FindBytestringValueInCborMap(response_map, kMediatedPoint,
                                    &mediated_point)) {
    LOG(ERROR) << "Failed to get mediated point from the HSM response map.";
    return false;
  }
  // Key Auth Value is optional.
  brillo::SecureBlob key_auth_value;
  const auto key_auth_value_entry =
      response_map.find(cbor::Value(kKeyAuthValue));
  if (key_auth_value_entry != response_map.end()) {
    if (!key_auth_value_entry->second.is_bytestring()) {
      LOG(ERROR) << "Wrongly formatted `" << kKeyAuthValue
                 << "` entry in the Response plain text CBOR map.";
      return false;
    }

    key_auth_value.assign(key_auth_value_entry->second.GetBytestring().begin(),
                          key_auth_value_entry->second.GetBytestring().end());
  }

  response_payload->dealer_pub_key = std::move(dealer_pub_key);
  response_payload->mediated_point = std::move(mediated_point);
  response_payload->key_auth_value = std::move(key_auth_value);
  return true;
}

bool DeserializeHsmResponseAssociatedDataFromCbor(
    const brillo::SecureBlob& response_ad_cbor,
    HsmResponseAssociatedData* response_ad) {
  const auto& cbor = ReadCborMap(response_ad_cbor);
  if (!cbor) {
    return false;
  }

  const cbor::Value::MapValue& response_map = cbor->GetMap();
  brillo::SecureBlob response_payload_salt;
  if (!FindBytestringValueInCborMap(response_map, kResponsePayloadSalt,
                                    &response_payload_salt)) {
    LOG(ERROR) << "Failed to get response payload salt from the HSM response "
                  "associated data map.";
    return false;
  }
  brillo::SecureBlob response_meta_data;
  if (!FindBytestringValueInCborMap(response_map, kResponseMetaData,
                                    &response_meta_data)) {
    LOG(ERROR) << "Failed to get response metadata from the HSM response "
                  "associated data map.";
    return false;
  }

  response_ad->response_payload_salt = std::move(response_payload_salt);
  response_ad->response_meta_data = std::move(response_meta_data);
  return true;
}

bool GetValueFromCborMapByKeyForTesting(const brillo::SecureBlob& input_cbor,
                                        const std::string& map_key,
                                        brillo::SecureBlob* value) {
  const auto& cbor = ReadCborMap(input_cbor);
  if (!cbor) {
    return false;
  }

  if (!FindBytestringValueInCborMap(cbor->GetMap(), map_key, value)) {
    LOG(ERROR) << "Failed to get keyed entry from cbor map.";
    return false;
  }

  return true;
}

bool GetHsmPayloadFromRequestAdForTesting(
    const brillo::SecureBlob& request_payload_cbor, HsmPayload* hsm_payload) {
  const auto& cbor = ReadCborMap(request_payload_cbor);
  if (!cbor) {
    return false;
  }

  const cbor::Value::MapValue& cbor_map = cbor->GetMap();
  const auto hsm_payload_entry = cbor_map.find(cbor::Value(kHsmAead));
  if (hsm_payload_entry == cbor_map.end()) {
    LOG(ERROR) << "No HSM payload entry in the Request cbor map.";
    return false;
  }
  if (!hsm_payload_entry->second.is_map()) {
    LOG(ERROR)
        << "HSM payload entry in the Request cbor map has a wrong format.";
    return false;
  }

  const cbor::Value::MapValue& hsm_map = hsm_payload_entry->second.GetMap();

  brillo::SecureBlob cipher_text;
  if (!FindBytestringValueInCborMap(hsm_map, kAeadCipherText, &cipher_text)) {
    LOG(ERROR) << "Failed to get cipher text from the HSM payload map.";
    return false;
  }
  brillo::SecureBlob associated_data;
  if (!FindBytestringValueInCborMap(hsm_map, kAeadAd, &associated_data)) {
    LOG(ERROR) << "Failed to get associated data from the HSM payload map.";
    return false;
  }
  brillo::SecureBlob iv;
  if (!FindBytestringValueInCborMap(hsm_map, kAeadIv, &iv)) {
    LOG(ERROR) << "Failed to get iv from the HSM payload map.";
    return false;
  }
  brillo::SecureBlob tag;
  if (!FindBytestringValueInCborMap(hsm_map, kAeadTag, &tag)) {
    LOG(ERROR) << "Failed to get tag from the HSM payload map.";
    return false;
  }

  hsm_payload->cipher_text = std::move(cipher_text);
  hsm_payload->associated_data = std::move(associated_data);
  hsm_payload->iv = std::move(iv);
  hsm_payload->tag = std::move(tag);
  return true;
}

bool GetRequestPayloadSchemaVersionForTesting(
    const brillo::SecureBlob& input_cbor, int* value) {
  const auto& cbor = ReadCborMap(input_cbor);
  if (!cbor) {
    return false;
  }

  const cbor::Value::MapValue& cbor_map = cbor->GetMap();
  const auto value_entry =
      cbor_map.find(cbor::Value(kRecoveryCryptoRequestSchemaVersion));
  if (value_entry == cbor_map.end()) {
    LOG(ERROR) << "No schema version encoded in the Request associated data.";
    return false;
  }
  if (!value_entry->second.is_integer()) {
    LOG(ERROR)
        << "Schema version in Request associated data is incorrectly encoded.";
    return false;
  }
  *value = value_entry->second.GetInteger();
  return true;
}

int GetCborMapSize(const brillo::SecureBlob& input_cbor) {
  const auto& cbor = ReadCborMap(input_cbor);
  if (!cbor) {
    return -1;
  }

  return cbor->GetMap().size();
}

}  // namespace cryptorecovery
}  // namespace cryptohome
