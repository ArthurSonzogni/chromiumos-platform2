// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/recovery_crypto_hsm_cbor_serialization.h"

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

const char kHsmAeadCipherText[] = "hsm_aead_ct";
const char kHsmAeadAd[] = "hsm_aead_ad";
const char kHsmAeadIv[] = "hsm_aead_iv";
const char kHsmAeadTag[] = "hsm_aead_tag";
const char kRequestMetaData[] = "request_meta_data";
const char kEpochPublicKey[] = "epoch_pub_key";
const char kEphemeralPublicInvKey[] = "ephemeral_pub_inv_key";

const int kProtocolVersion = 1;

bool SerializeHsmAssociatedDataToCbor(
    const cryptorecovery::HsmAssociatedData& args,
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
    const cryptorecovery::RecoveryRequestAssociatedData& args,
    brillo::SecureBlob* request_ad_cbor) {
  cbor::Value::MapValue ad_map;

  ad_map.emplace(kRecoveryCryptoRequestSchemaVersion,
                 /*schema_version=*/kProtocolVersion);
  ad_map.emplace(kHsmAeadCipherText, args.hsm_aead_ct);
  ad_map.emplace(kHsmAeadAd, args.hsm_aead_ad);
  ad_map.emplace(kHsmAeadIv, args.hsm_aead_iv);
  ad_map.emplace(kHsmAeadTag, args.hsm_aead_tag);
  ad_map.emplace(kRequestMetaData, args.request_meta_data);
  ad_map.emplace(kEpochPublicKey, args.epoch_pub_key);

  if (!SerializeCborMap(ad_map, request_ad_cbor)) {
    LOG(ERROR)
        << "Failed to serialize Recovery Request Associated Data to CBOR";
    return false;
  }
  return true;
}

bool SerializeHsmPlainTextToCbor(const cryptorecovery::HsmPlainText& plain_text,
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
    const cryptorecovery::RecoveryRequestPlainText& plain_text,
    brillo::SecureBlob* plain_text_cbor) {
  cbor::Value::MapValue pt_map;

  pt_map.emplace(kEphemeralPublicInvKey, plain_text.ephemeral_pub_inv_key);

  if (!SerializeCborMap(pt_map, plain_text_cbor)) {
    LOG(ERROR) << "Failed to serialize Recovery Request plain text to CBOR";
    return false;
  }
  return true;
}

bool SerializeHsmResponsePlainTextToCbor(
    const cryptorecovery::HsmResponsePlainText& plain_text,
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
    cryptorecovery::HsmPlainText* hsm_plain_text) {
  const auto& cbor = ReadCborPayload(hsm_plain_text_cbor);
  if (!cbor) {
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
    cryptorecovery::RecoveryRequestPlainText* request_plain_text) {
  const auto& cbor = ReadCborPayload(request_plain_text_cbor);
  if (!cbor) {
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

bool DeserializeHsmResponsePayloadFromCbor(
    const brillo::SecureBlob& response_payload_cbor,
    cryptorecovery::HsmResponsePlainText* response_payload) {
  const auto& cbor = ReadCborPayload(response_payload_cbor);
  if (!cbor) {
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

bool GetHsmCborMapByKeyForTesting(const brillo::SecureBlob& input_cbor,
                                  const std::string& map_key,
                                  brillo::SecureBlob* value) {
  const auto& cbor = ReadCborPayload(input_cbor);
  if (!cbor) {
    return false;
  }
  const cbor::Value::MapValue& cbor_map = cbor->GetMap();
  const auto value_entry = cbor_map.find(cbor::Value(map_key));
  if (value_entry == cbor_map.end()) {
    LOG(ERROR) << "No keyed entry in the HSM response map.";
    return false;
  }
  if (!value_entry->second.is_bytestring()) {
    LOG(ERROR) << "Keyed entry in the HSM response has a wrong format.";
    return false;
  }
  value->assign(value_entry->second.GetBytestring().begin(),
                value_entry->second.GetBytestring().end());
  return true;
}

bool GetRequestPayloadSchemaVersionForTesting(
    const brillo::SecureBlob& input_cbor, int* value) {
  const auto& cbor = ReadCborPayload(input_cbor);
  if (!cbor) {
    return false;
  }
  const cbor::Value::MapValue& cbor_map = cbor->GetMap();
  const auto value_entry =
      cbor_map.find(cbor::Value(kRecoveryCryptoRequestSchemaVersion));
  if (value_entry == cbor_map.end()) {
    LOG(ERROR) << "No schema version encoded in the HSM cbor.";
    return false;
  }
  if (!value_entry->second.is_integer()) {
    LOG(ERROR) << "Schema version in HSM payload is incorrectly encoded.";
    return false;
  }
  *value = value_entry->second.GetInteger();
  return true;
}

}  // namespace cryptohome
