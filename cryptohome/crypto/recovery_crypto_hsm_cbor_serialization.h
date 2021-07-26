// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_HSM_CBOR_SERIALIZATION_H_
#define CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_HSM_CBOR_SERIALIZATION_H_

#include <string>

#include <brillo/secure_blob.h>
#include <chromeos/cbor/values.h>

namespace cryptohome {

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
extern const char kHsmAeadCipherText[];
extern const char kHsmAeadAd[];
extern const char kHsmAeadIv[];
extern const char kHsmAeadTag[];
extern const char kEphemeralPublicInvKey[];
extern const char kRequestMetaData[];
extern const char kEpochPublicKey[];

// Mediation protocol version.
extern const int kProtocolVersion;

// Constructs cbor-encoded binary blob with associated data.
// `publisher_pub_key` and `channel_pub_key` are elliptic curve points
// encoded in OpenSSL octet form (a binary encoding of the EC_POINT
// structure as defined in RFC5480).
// TODO(mslus): exact format of rsa_public_key used for TPM 1.2 is
// to be defined.
bool SerializeHsmAssociatedDataToCbor(
    const brillo::SecureBlob& publisher_pub_key,
    const brillo::SecureBlob& channel_pub_key,
    const brillo::SecureBlob& rsa_public_key,
    const brillo::SecureBlob& onboarding_metadata,
    brillo::SecureBlob* ad_cbor);

// Constructs cbor-encoded binary blob with associated data for request payload.
// Parameters
//   hsm_aead_ct - ciphertext (CT1).
//   hsm_aead_ad - HSM associated data (AD1).
//   hsm_aead_iv - iv for AEAD of the HSM payload (CT1 and AD1).
//   hsm_aead_tag - tag for AEAD of the HSM payload.
//   request_meta_data - RMD according to the protocol spec.
//   epoch_pub_key - current epoch beacon value (G*r).
bool SerializeRecoveryRequestAssociatedDataToCbor(
    const brillo::SecureBlob& hsm_aead_ct,
    const brillo::SecureBlob& hsm_aead_ad,
    const brillo::SecureBlob& hsm_aead_iv,
    const brillo::SecureBlob& hsm_aead_tag,
    const brillo::SecureBlob& request_meta_data,
    const brillo::SecureBlob& epoch_pub_key,
    brillo::SecureBlob* request_ad_cbor);

// Constructs cbor-encoded binary blob from plain text of data that will
// be subsequently encrypted and in HSM payload. `dealer_pub_key` is an
// elliptic curve point encoded in OpenSSL octet form (a binary encoding
// of the EC_POINT structure as defined in RFC5480).
// `mediator_share` and `key_auth_value` are BIGNUMs encoded in big-endian
// form.
bool SerializeHsmPlainTextToCbor(const brillo::SecureBlob& mediator_share,
                                 const brillo::SecureBlob& dealer_pub_key,
                                 const brillo::SecureBlob& key_auth_value,
                                 brillo::SecureBlob* plain_text_cbor);

// Constructs cbor-encoded binary blob from plain text of data that will
// be subsequently encrypted and in response payload. `dealer_pub_key` and
// `mediated_point` are elliptic curve points encoded in OpenSSL octet form
// (a binary encoding of the EC_POINT structure as defined in RFC5480).
// `key_auth_value` is BIGNUM encoded in big-endian form.
bool SerializeHsmResponsePayloadToCbor(const brillo::SecureBlob& mediated_point,
                                       const brillo::SecureBlob& dealer_pub_key,
                                       const brillo::SecureBlob& key_auth_value,
                                       brillo::SecureBlob* response_cbor);

// Extracts data from HSM plain text cbor. `dealer_pub_key` is an
// elliptic curve point encoded in OpenSSL octet form (a binary encoding
// of the EC_POINT structure as defined in RFC5480).
// `mediator_share` and `key_auth_value` are BIGNUMs encoded in big-endian
// form.
bool DeserializeHsmPlainTextFromCbor(
    const brillo::SecureBlob& hsm_plain_text_cbor,
    brillo::SecureBlob* mediator_share,
    brillo::SecureBlob* dealer_pub_key,
    brillo::SecureBlob* key_auth_value);

// Extracts data from response payload cbor. `dealer_pub_key` and
// `mediated_point` are elliptic curve points encoded in OpenSSL octet form
// (a binary encoding of the EC_POINT structure as defined in RFC5480).
// `key_auth_value` is BIGNUM encoded in big-endian form.
bool DeserializeHsmResponsePayloadFromCbor(
    const brillo::SecureBlob& response_payload_cbor,
    brillo::SecureBlob* mediated_point,
    brillo::SecureBlob* dealer_pub_key,
    brillo::SecureBlob* key_auth_value);

bool GetHsmCborMapByKeyForTesting(const brillo::SecureBlob& input_cbor,
                                  const std::string& map_key,
                                  brillo::SecureBlob* value);

bool GetRequestPayloadSchemaVersionForTesting(
    const brillo::SecureBlob& input_cbor, int* value);

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTO_RECOVERY_CRYPTO_HSM_CBOR_SERIALIZATION_H_
