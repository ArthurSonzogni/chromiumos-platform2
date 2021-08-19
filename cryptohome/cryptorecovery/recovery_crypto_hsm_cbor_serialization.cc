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

base::Optional<cbor::Value> ReadCborPayload(
    const brillo::SecureBlob& payload_cbor) {
  cbor::Reader::DecoderError error_code;
  base::Optional<cbor::Value> cbor_response =
      cbor::Reader::Read(payload_cbor, &error_code);
  if (!cbor_response) {
    LOG(ERROR) << "Unable to create HSM cbor reader.";
    return base::nullopt;
  }
  if (error_code != cbor::Reader::DecoderError::CBOR_NO_ERROR) {
    LOG(ERROR) << "Error when parsing HSM cbor payload: "
               << cbor::Reader::ErrorCodeToString(error_code);
    return base::nullopt;
  }
  if (!cbor_response->is_map()) {
    LOG(ERROR) << "HSM cbor input is not a map.";
    return base::nullopt;
  }
  return cbor_response;
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
  const auto& cbor = ReadCborPayload(hsm_plain_text_cbor);
  if (!cbor) {
    return false;
  }
  if (!cbor->is_map()) {
    LOG(ERROR) << "HSM plain text is not a cbor map.";
    return false;
  }

  const cbor::Value::MapValue& response_map = cbor->GetMap();
  const auto dealer_pub_key_entry =
      response_map.find(cbor::Value(kDealerPublicKey));
  if (dealer_pub_key_entry == response_map.end()) {
    LOG(ERROR) << "No dealer public key in the HSM response map.";
    return false;
  }
  if (!dealer_pub_key_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted dealer key in the HSM response map.";
    return false;
  }

  const auto mediator_share_entry =
      response_map.find(cbor::Value(kMediatorShare));
  if (mediator_share_entry == response_map.end()) {
    LOG(ERROR) << "No share entry in the HSM response map.";
    return false;
  }
  if (!mediator_share_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted share entry in the HSM response map.";
    return false;
  }

  const auto key_auth_value_entry =
      response_map.find(cbor::Value(kKeyAuthValue));
  if (key_auth_value_entry == response_map.end()) {
    LOG(ERROR) << "No key auth value in the HSM response map.";
    return false;
  }
  if (!key_auth_value_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted key auth value in the HSM response map.";
    return false;
  }

  hsm_plain_text->dealer_pub_key.assign(
      dealer_pub_key_entry->second.GetBytestring().begin(),
      dealer_pub_key_entry->second.GetBytestring().end());
  hsm_plain_text->mediator_share.assign(
      mediator_share_entry->second.GetBytestring().begin(),
      mediator_share_entry->second.GetBytestring().end());
  hsm_plain_text->key_auth_value.assign(
      key_auth_value_entry->second.GetBytestring().begin(),
      key_auth_value_entry->second.GetBytestring().end());
  return true;
}

bool DeserializeRecoveryRequestPlainTextFromCbor(
    const brillo::SecureBlob& request_plain_text_cbor,
    RecoveryRequestPlainText* request_plain_text) {
  const auto& cbor = ReadCborPayload(request_plain_text_cbor);
  if (!cbor) {
    return false;
  }
  if (!cbor->is_map()) {
    LOG(ERROR) << "Request plain text is not a cbor map.";
    return false;
  }

  const cbor::Value::MapValue& request_map = cbor->GetMap();
  const auto ephemeral_pub_inv_key_entry =
      request_map.find(cbor::Value(kEphemeralPublicInvKey));
  if (ephemeral_pub_inv_key_entry == request_map.end()) {
    LOG(ERROR) << "No ephemeral_pub_inv_key in the Recovery Request map";
    return false;
  }
  if (!ephemeral_pub_inv_key_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted ephemeral_pub_inv_key in the Recovery "
                  "Request map";
    return false;
  }

  request_plain_text->ephemeral_pub_inv_key.assign(
      ephemeral_pub_inv_key_entry->second.GetBytestring().begin(),
      ephemeral_pub_inv_key_entry->second.GetBytestring().end());
  return true;
}

bool DeserializeHsmResponsePlainTextFromCbor(
    const brillo::SecureBlob& response_payload_cbor,
    HsmResponsePlainText* response_payload) {
  const auto& cbor = ReadCborPayload(response_payload_cbor);
  if (!cbor) {
    return false;
  }
  if (!cbor->is_map()) {
    LOG(ERROR) << "HSM plain text is not a cbor map.";
    return false;
  }

  const cbor::Value::MapValue& response_map = cbor->GetMap();
  const auto dealer_pub_key_entry =
      response_map.find(cbor::Value(kDealerPublicKey));
  if (dealer_pub_key_entry == response_map.end()) {
    LOG(ERROR) << "No dealer public key in the HSM response map.";
    return false;
  }
  if (!dealer_pub_key_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted dealer key in the HSM response map.";
    return false;
  }

  const auto mediator_share_entry =
      response_map.find(cbor::Value(kMediatedPoint));
  if (mediator_share_entry == response_map.end()) {
    LOG(ERROR) << "No share entry in the HSM response map.";
    return false;
  }
  if (!mediator_share_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted share entry in the HSM response map.";
    return false;
  }

  const auto key_auth_value_entry =
      response_map.find(cbor::Value(kKeyAuthValue));
  if (key_auth_value_entry == response_map.end()) {
    LOG(ERROR) << "No key auth value in the HSM response map.";
    return false;
  }
  if (!key_auth_value_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted key auth value in the HSM response map.";
    return false;
  }

  response_payload->dealer_pub_key.assign(
      dealer_pub_key_entry->second.GetBytestring().begin(),
      dealer_pub_key_entry->second.GetBytestring().end());
  response_payload->mediated_point.assign(
      mediator_share_entry->second.GetBytestring().begin(),
      mediator_share_entry->second.GetBytestring().end());
  response_payload->key_auth_value.assign(
      key_auth_value_entry->second.GetBytestring().begin(),
      key_auth_value_entry->second.GetBytestring().end());
  return true;
}

bool DeserializeHsmResponseAssociatedDataFromCbor(
    const brillo::SecureBlob& response_ad_cbor,
    HsmResponseAssociatedData* response_ad) {
  const auto& cbor = ReadCborPayload(response_ad_cbor);
  if (!cbor) {
    return false;
  }
  if (!cbor->is_map()) {
    LOG(ERROR) << "HSM associated data is not a cbor map.";
    return false;
  }

  const cbor::Value::MapValue& response_map = cbor->GetMap();
  const auto response_payload_salt_entry =
      response_map.find(cbor::Value(kResponsePayloadSalt));
  if (response_payload_salt_entry == response_map.end()) {
    LOG(ERROR)
        << "No response_payload_salt in the HSM response associated data map.";
    return false;
  }
  if (!response_payload_salt_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted response_payload_salt in the HSM response "
                  "associated data map.";
    return false;
  }

  const auto response_metadata_entry =
      response_map.find(cbor::Value(kResponseMetaData));
  if (response_metadata_entry == response_map.end()) {
    LOG(ERROR)
        << "No response_metadata in the HSM response associated data map.";
    return false;
  }
  if (!response_metadata_entry->second.is_bytestring()) {
    LOG(ERROR) << "Wrongly formatted response_metadata in the HSM response "
                  "associated data map.";
    return false;
  }

  response_ad->response_payload_salt.assign(
      response_payload_salt_entry->second.GetBytestring().begin(),
      response_payload_salt_entry->second.GetBytestring().end());
  response_ad->response_meta_data.assign(
      response_metadata_entry->second.GetBytestring().begin(),
      response_metadata_entry->second.GetBytestring().end());
  return true;
}

bool GetHsmCborMapByKeyForTesting(const brillo::SecureBlob& input_cbor,
                                  const std::string& map_key,
                                  brillo::SecureBlob* value) {
  const auto& cbor = ReadCborPayload(input_cbor);
  if (!cbor) {
    return false;
  }
  if (!cbor->is_map()) {
    LOG(ERROR) << "Provided cbor is not a map.";
    return false;
  }

  const cbor::Value::MapValue& cbor_map = cbor->GetMap();
  const auto value_entry = cbor_map.find(cbor::Value(map_key));
  if (value_entry == cbor_map.end()) {
    LOG(ERROR) << "No keyed entry in the cbor map.";
    return false;
  }
  if (!value_entry->second.is_bytestring()) {
    LOG(ERROR) << "Keyed entry in the cbor map has a wrong format.";
    return false;
  }
  value->assign(value_entry->second.GetBytestring().begin(),
                value_entry->second.GetBytestring().end());
  return true;
}

bool GetHsmPayloadFromRequestAdForTesting(
    const brillo::SecureBlob& request_payload_cbor, HsmPayload* hsm_payload) {
  const auto& cbor = ReadCborPayload(request_payload_cbor);
  if (!cbor) {
    return false;
  }
  if (!cbor->is_map()) {
    LOG(ERROR) << "Request associated data is not a cbor map.";
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

  const auto ct_entry = hsm_map.find(cbor::Value(kAeadCipherText));
  if (ct_entry == hsm_map.end()) {
    LOG(ERROR) << "No cipher text entry in the HSM cbor map.";
    return false;
  }
  if (!ct_entry->second.is_bytestring()) {
    LOG(ERROR) << "Cipher text entry in the HSM cbor map has a wrong format.";
    return false;
  }

  const auto ad_entry = hsm_map.find(cbor::Value(kAeadAd));
  if (ad_entry == hsm_map.end()) {
    LOG(ERROR) << "No associated data entry in the HSM cbor map.";
    return false;
  }
  if (!ad_entry->second.is_bytestring()) {
    LOG(ERROR)
        << "Associated data entry in the HSM cbor map has a wrong format.";
    return false;
  }

  const auto iv_entry = hsm_map.find(cbor::Value(kAeadIv));
  if (iv_entry == hsm_map.end()) {
    LOG(ERROR) << "No iv entry in the HSM cbor map.";
    return false;
  }
  if (!iv_entry->second.is_bytestring()) {
    LOG(ERROR) << "Iv entry in the HSM cbor map has a wrong format.";
    return false;
  }

  const auto tag_entry = hsm_map.find(cbor::Value(kAeadTag));
  if (tag_entry == hsm_map.end()) {
    LOG(ERROR) << "No tag entry in the HSM cbor map.";
    return false;
  }
  if (!tag_entry->second.is_bytestring()) {
    LOG(ERROR) << "Tag entry in the HSM cbor map has a wrong format.";
    return false;
  }

  hsm_payload->cipher_text.assign(ct_entry->second.GetBytestring().begin(),
                                  ct_entry->second.GetBytestring().end());
  hsm_payload->associated_data.assign(ad_entry->second.GetBytestring().begin(),
                                      ad_entry->second.GetBytestring().end());
  hsm_payload->iv.assign(iv_entry->second.GetBytestring().begin(),
                         iv_entry->second.GetBytestring().end());
  hsm_payload->tag.assign(tag_entry->second.GetBytestring().begin(),
                          tag_entry->second.GetBytestring().end());
  return true;
}

bool GetRequestPayloadSchemaVersionForTesting(
    const brillo::SecureBlob& input_cbor, int* value) {
  const auto& cbor = ReadCborPayload(input_cbor);
  if (!cbor) {
    return false;
  }
  if (!cbor->is_map()) {
    LOG(ERROR) << "Request associated data is not a cbor map.";
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

}  // namespace cryptorecovery
}  // namespace cryptohome
