// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_HSM_CBOR_SERIALIZATION_H_
#define CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_HSM_CBOR_SERIALIZATION_H_

#include <string>

#include <brillo/secure_blob.h>
#include <chromeos/cbor/values.h>

#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {

// Constants that will be used as keys in the CBOR map.
extern const char kRecoveryCryptoRequestSchemaVersion[];
extern const char kMediatorShare[];
extern const char kMediatedPoint[];
extern const char kKeyAuthValue[];
extern const char kDealerPublicKey[];
extern const char kPublisherPublicKey[];
extern const char kChannelPublicKey[];
extern const char kRsaPublicKey[];
extern const char kOnboardingMetaData[];
extern const char kHsmAead[];
extern const char kAeadCipherText[];
extern const char kAeadAd[];
extern const char kAeadIv[];
extern const char kAeadTag[];
extern const char kEphemeralPublicInvKey[];
extern const char kRequestMetaData[];
extern const char kEpochPublicKey[];
extern const char kRequestPayloadSalt[];
extern const char kResponseMetaData[];
extern const char kResponsePayloadSalt[];

// Mediation protocol version.
extern const int kProtocolVersion;

// Constructs cbor-encoded binary blob with associated data.
bool SerializeHsmAssociatedDataToCbor(const HsmAssociatedData& ad,
                                      brillo::SecureBlob* ad_cbor);

// Constructs cbor-encoded binary blob with associated data for request payload.
bool SerializeRecoveryRequestAssociatedDataToCbor(
    const RecoveryRequestAssociatedData& request_ad,
    brillo::SecureBlob* request_ad_cbor);

// Constructs cbor-encoded binary blob with associated data for response
// payload.
bool SerializeHsmResponseAssociatedDataToCbor(
    const HsmResponseAssociatedData& response_ad,
    brillo::SecureBlob* response_ad_cbor);

// Constructs cbor-encoded binary blob from plain text of data that will
// be subsequently encrypted and in HSM payload.
bool SerializeHsmPlainTextToCbor(const HsmPlainText& plain_text,
                                 brillo::SecureBlob* plain_text_cbor);

// Constructs cbor-encoded binary blob from plain text of data that will
// be subsequently encrypted and in Request payload.
bool SerializeRecoveryRequestPlainTextToCbor(
    const RecoveryRequestPlainText& plain_text,
    brillo::SecureBlob* plain_text_cbor);

// Constructs cbor-encoded binary blob from plain text of data that will
// be subsequently encrypted and in response payload.
bool SerializeHsmResponsePlainTextToCbor(const HsmResponsePlainText& plain_text,
                                         brillo::SecureBlob* plain_text_cbor);

// Extracts data from HSM plain text cbor.
bool DeserializeHsmPlainTextFromCbor(
    const brillo::SecureBlob& hsm_plain_text_cbor,
    HsmPlainText* hsm_plain_text);

// Extracts data from Recovery Request plain text cbor.
bool DeserializeRecoveryRequestPlainTextFromCbor(
    const brillo::SecureBlob& request_plain_text_cbor,
    RecoveryRequestPlainText* request_plain_text);

// Extracts data from response plain text cbor.
bool DeserializeHsmResponsePlainTextFromCbor(
    const brillo::SecureBlob& response_payload_cbor,
    HsmResponsePlainText* response_payload);

// Extracts data from HSM Response associated data cbor.
bool DeserializeHsmResponseAssociatedDataFromCbor(
    const brillo::SecureBlob& response_ad_cbor,
    HsmResponseAssociatedData* response_ad);

bool GetValueFromCborMapByKeyForTesting(const brillo::SecureBlob& input_cbor,
                                        const std::string& map_key,
                                        brillo::SecureBlob* value);

bool GetHsmPayloadFromRequestAdForTesting(
    const brillo::SecureBlob& request_payload_cbor, HsmPayload* hsm_payload);

bool GetRequestPayloadSchemaVersionForTesting(
    const brillo::SecureBlob& input_cbor, int* value);

// Returns number of values in CBOR map. Returns -1 if provided blob is not a
// CBOR map.
int GetCborMapSize(const brillo::SecureBlob& input_cbor);

}  // namespace cryptorecovery
}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTORECOVERY_RECOVERY_CRYPTO_HSM_CBOR_SERIALIZATION_H_
