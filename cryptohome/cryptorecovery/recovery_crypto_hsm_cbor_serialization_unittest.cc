// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"

#include <map>
#include <utility>
#include <vector>

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <chromeos/cbor/writer.h>
#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest.h>
#include <openssl/bn.h>

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

namespace cryptohome {
namespace cryptorecovery {

namespace {

constexpr EllipticCurve::CurveType kCurve = EllipticCurve::CurveType::kPrime256;
// Size of public/private key for EllipticCurve::CurveType::kPrime256.
constexpr size_t kEc256PubKeySize = 65;
constexpr size_t kEc256PrivKeySize = 32;

const char kOnboardingData[] = "fake onboarding data";
const char kFakeRequestData[] = "fake request metadata";
const char kFakeHsmPayloadCipherText[] = "fake hsm payload cipher text";
const char kFakeHsmPayloadAd[] = "fake hsm payload ad";
const char kFakeHsmPayloadIv[] = "fake hsm payload iv";
const char kFakeHsmPayloadTag[] = "fake hsm payload tag";
const char kFakeResponseData[] = "fake response metadata";
const char kFakeResponseSalt[] = "fake response salt";

bool CreateCborMapForTesting(const cbor::Value::MapValue& map,
                             brillo::SecureBlob* serialized_cbor_map) {
  base::Optional<std::vector<uint8_t>> serialized =
      cbor::Writer::Write(cbor::Value(map));
  if (!serialized) {
    LOG(ERROR) << "Failed to serialize CBOR Map.";
    return false;
  }

  serialized_cbor_map->assign(serialized.value().begin(),
                              serialized.value().end());
  return true;
}

void ExpectSerializedCborMapContainsValue(
    const brillo::SecureBlob& cbor_output,
    const std::string& key,
    const brillo::SecureBlob& expected_value) {
  brillo::SecureBlob deserialized_value;
  EXPECT_TRUE(GetBytestringValueFromCborMapByKeyForTesting(
      cbor_output, key, &deserialized_value));
  EXPECT_EQ(deserialized_value, expected_value);
}

void ExpectCborMapContainsValue(const cbor::Value::MapValue& map,
                                const std::string& key,
                                const brillo::SecureBlob& expected_value) {
  const auto entry = map.find(cbor::Value(key));
  ASSERT_TRUE(entry != map.end());
  ASSERT_TRUE(entry->second.is_bytestring());
  brillo::SecureBlob result(entry->second.GetBytestring().begin(),
                            entry->second.GetBytestring().end());
  EXPECT_EQ(result, expected_value);
}

}  // namespace

class HsmPayloadCborHelperTest : public testing::Test {
 public:
  void SetUp() override {
    context_ = CreateBigNumContext();
    ASSERT_TRUE(context_);
    ec_ = EllipticCurve::Create(kCurve, context_.get());
    ASSERT_TRUE(ec_);
    ASSERT_TRUE(ec_->GenerateKeysAsSecureBlobs(
        &publisher_pub_key_, &publisher_priv_key_, context_.get()));
    ASSERT_TRUE(ec_->GenerateKeysAsSecureBlobs(
        &channel_pub_key_, &channel_priv_key_, context_.get()));
    ASSERT_TRUE(ec_->GenerateKeysAsSecureBlobs(
        &dealer_pub_key_, &dealer_priv_key_, context_.get()));
  }

 protected:
  ScopedBN_CTX context_;
  base::Optional<EllipticCurve> ec_;
  brillo::SecureBlob publisher_pub_key_;
  brillo::SecureBlob publisher_priv_key_;
  brillo::SecureBlob channel_pub_key_;
  brillo::SecureBlob channel_priv_key_;
  brillo::SecureBlob dealer_pub_key_;
  brillo::SecureBlob dealer_priv_key_;
};

class AeadPayloadHelper {
 public:
  AeadPayloadHelper() {
    fake_aead_payload_.cipher_text = brillo::SecureBlob("fake cipher text");
    fake_aead_payload_.associated_data =
        brillo::SecureBlob("fake associated data");
    fake_aead_payload_.iv = brillo::SecureBlob("fake iv");
    fake_aead_payload_.tag = brillo::SecureBlob("fake tag");
  }

  const AeadPayload& GetFakePayload() const { return fake_aead_payload_; }

  cbor::Value::MapValue GetFakePayloadCborMap() const {
    cbor::Value::MapValue payload;
    payload.emplace(kAeadCipherText, fake_aead_payload_.cipher_text);
    payload.emplace(kAeadAd, fake_aead_payload_.associated_data);
    payload.emplace(kAeadIv, fake_aead_payload_.iv);
    payload.emplace(kAeadTag, fake_aead_payload_.tag);
    return payload;
  }

  void ExpectEqualsToFakeAeadPayload(const AeadPayload& payload) const {
    EXPECT_EQ(payload.cipher_text, fake_aead_payload_.cipher_text);
    EXPECT_EQ(payload.associated_data, fake_aead_payload_.associated_data);
    EXPECT_EQ(payload.iv, fake_aead_payload_.iv);
    EXPECT_EQ(payload.tag, fake_aead_payload_.tag);
  }

  void ExpectEqualsToFakeAeadPayload(
      const cbor::Value::MapValue& cbor_map) const {
    ExpectCborMapContainsValue(cbor_map, kAeadCipherText,
                               fake_aead_payload_.cipher_text);
    ExpectCborMapContainsValue(cbor_map, kAeadAd,
                               fake_aead_payload_.associated_data);
    ExpectCborMapContainsValue(cbor_map, kAeadIv, fake_aead_payload_.iv);
    ExpectCborMapContainsValue(cbor_map, kAeadTag, fake_aead_payload_.tag);
  }

 private:
  AeadPayload fake_aead_payload_;
};

class RecoveryRequestCborHelperTest : public testing::Test {
 public:
  void SetUp() override {
    context_ = CreateBigNumContext();
    ASSERT_TRUE(context_);
    ec_ = EllipticCurve::Create(kCurve, context_.get());
    ASSERT_TRUE(ec_);
    ASSERT_TRUE(ec_->GenerateKeysAsSecureBlobs(
        &epoch_pub_key_, &epoch_priv_key_, context_.get()));
  }

 protected:
  AeadPayloadHelper aead_helper_;
  ScopedBN_CTX context_;
  base::Optional<EllipticCurve> ec_;
  brillo::SecureBlob epoch_pub_key_;
  brillo::SecureBlob epoch_priv_key_;
};

class RecoveryResponseCborHelperTest : public testing::Test {
 protected:
  AeadPayloadHelper aead_helper_;
  const int fake_error_code_ = 2;
  const std::string fake_error_string_ = "fake error string";
  const brillo::SecureBlob fake_pub_key_ =
      CreateSecureRandomBlob(kEc256PubKeySize);
  const brillo::SecureBlob fake_priv_key_ =
      CreateSecureRandomBlob(kEc256PrivKeySize);
  const brillo::SecureBlob metadata_{kFakeResponseData};
  const brillo::SecureBlob salt_{kFakeResponseSalt};
};

// Verifies serialization of HSM payload associated data to CBOR.
TEST_F(HsmPayloadCborHelperTest, GenerateAdCborWithEmptyRsaPublicKey) {
  brillo::SecureBlob cbor_output;
  HsmAssociatedData args;
  args.publisher_pub_key = publisher_pub_key_;
  args.channel_pub_key = channel_pub_key_;
  args.onboarding_meta_data = brillo::SecureBlob(kOnboardingData);
  ASSERT_TRUE(SerializeHsmAssociatedDataToCbor(args, &cbor_output));

  ExpectSerializedCborMapContainsValue(cbor_output, kPublisherPublicKey,
                                       publisher_pub_key_);
  ExpectSerializedCborMapContainsValue(cbor_output, kChannelPublicKey,
                                       channel_pub_key_);
  ExpectSerializedCborMapContainsValue(cbor_output, kOnboardingMetaData,
                                       brillo::SecureBlob(kOnboardingData));
  EXPECT_EQ(4, GetCborMapSize(cbor_output));
}

// Verifies serialization of HSM payload plain text encrypted payload to CBOR.
TEST_F(HsmPayloadCborHelperTest, GeneratePlainTextHsmPayloadCbor) {
  brillo::SecureBlob mediator_share;
  brillo::SecureBlob cbor_output;

  crypto::ScopedBIGNUM scalar = BigNumFromValue(123123123u);
  ASSERT_TRUE(scalar);
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, 10, &mediator_share));

  // Serialize plain text payload with empty kav.
  HsmPlainText hsm_plain_text;
  hsm_plain_text.mediator_share = mediator_share;
  hsm_plain_text.dealer_pub_key = dealer_pub_key_;
  hsm_plain_text.key_auth_value = brillo::SecureBlob();
  ASSERT_TRUE(SerializeHsmPlainTextToCbor(hsm_plain_text, &cbor_output));

  ExpectSerializedCborMapContainsValue(cbor_output, kDealerPublicKey,
                                       dealer_pub_key_);
  ExpectSerializedCborMapContainsValue(cbor_output, kKeyAuthValue,
                                       brillo::SecureBlob());
  brillo::SecureBlob deserialized_mediator_share;
  EXPECT_TRUE(GetBytestringValueFromCborMapByKeyForTesting(
      cbor_output, kMediatorShare, &deserialized_mediator_share));
  EXPECT_EQ(BN_get_word(scalar.get()),
            BN_get_word(SecureBlobToBigNum(deserialized_mediator_share).get()));
  EXPECT_EQ(3, GetCborMapSize(cbor_output));
}

// Verifies deserialization of HSM payload plain text from CBOR.
TEST_F(HsmPayloadCborHelperTest, DeserializePlainTextHsmPayload) {
  brillo::SecureBlob mediator_share;
  brillo::SecureBlob cbor_output;

  crypto::ScopedBIGNUM scalar = BigNumFromValue(123123123u);
  ASSERT_TRUE(scalar);
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, 10, &mediator_share));

  // Serialize plain text payload with empty kav.
  cbor::Value::MapValue fake_map;
  fake_map.emplace(kDealerPublicKey, dealer_pub_key_);
  fake_map.emplace(kMediatorShare, mediator_share);
  fake_map.emplace(kKeyAuthValue, brillo::SecureBlob());
  brillo::SecureBlob hsm_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(fake_map, &hsm_cbor));

  HsmPlainText hsm_plain_text;
  DeserializeHsmPlainTextFromCbor(hsm_cbor, &hsm_plain_text);

  EXPECT_EQ(hsm_plain_text.dealer_pub_key, dealer_pub_key_);
  EXPECT_EQ(hsm_plain_text.mediator_share, mediator_share);
  EXPECT_TRUE(hsm_plain_text.key_auth_value.empty());
}

// Verifies that the deserialization of HSM payload plain text from CBOR fails
// if input is not a CBOR.
TEST_F(HsmPayloadCborHelperTest, DeserializePlainTextHsmPayloadNotCbor) {
  HsmPlainText hsm_plain_text;
  brillo::SecureBlob hsm_cbor("actually not a CBOR");
  EXPECT_FALSE(DeserializeHsmPlainTextFromCbor(hsm_cbor, &hsm_plain_text));
}

// Verifies that the deserialization of HSM payload plain text from CBOR fails
// if input is not a CBOR map.
TEST_F(HsmPayloadCborHelperTest, DeserializePlainTextHsmPayloadNotMap) {
  HsmPlainText hsm_plain_text;
  base::Optional<std::vector<uint8_t>> serialized =
      cbor::Writer::Write(cbor::Value("a CBOR but not a map"));
  ASSERT_TRUE(serialized.has_value());
  brillo::SecureBlob hsm_cbor(serialized.value().begin(),
                              serialized.value().end());
  EXPECT_FALSE(DeserializeHsmPlainTextFromCbor(hsm_cbor, &hsm_plain_text));
}

// Verifies that the deserialization of HSM payload plain text from CBOR fails
// if CBOR has wrong format.
TEST_F(HsmPayloadCborHelperTest, DeserializePlainTextHsmPayloadWrongFormat) {
  HsmPlainText hsm_plain_text;
  brillo::SecureBlob cbor_output;
  cbor::Value::MapValue fake_map;
  fake_map.emplace(kMediatorShare, "a string value instead of bytes");
  fake_map.emplace(kDealerPublicKey, dealer_pub_key_);
  fake_map.emplace(kKeyAuthValue, brillo::SecureBlob());
  brillo::SecureBlob hsm_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(fake_map, &hsm_cbor));
  EXPECT_FALSE(DeserializeHsmPlainTextFromCbor(hsm_cbor, &hsm_plain_text));
}

// Simulates failed attempt to get dealer public key from the Hsm payload
// associated data.
TEST_F(HsmPayloadCborHelperTest, FailedAttemptToGetPlainTextFieldFromAd) {
  brillo::SecureBlob cbor_output;
  HsmAssociatedData args;
  args.publisher_pub_key = publisher_pub_key_;
  args.channel_pub_key = channel_pub_key_;
  args.onboarding_meta_data = brillo::SecureBlob(kOnboardingData);
  ASSERT_TRUE(SerializeHsmAssociatedDataToCbor(args, &cbor_output));
  brillo::SecureBlob deserialized_dealer_pub_key;
  EXPECT_FALSE(GetBytestringValueFromCborMapByKeyForTesting(
      cbor_output, kDealerPublicKey, &deserialized_dealer_pub_key));
}

// Verifies serialization of Recovery Request payload associated data to CBOR.
TEST_F(RecoveryRequestCborHelperTest, GenerateAd) {
  brillo::SecureBlob salt("fake salt");
  brillo::SecureBlob cbor_output;
  HsmPayload hsm_payload;
  hsm_payload.cipher_text = brillo::SecureBlob(kFakeHsmPayloadCipherText);
  hsm_payload.associated_data = brillo::SecureBlob(kFakeHsmPayloadAd);
  hsm_payload.iv = brillo::SecureBlob(kFakeHsmPayloadIv);
  hsm_payload.tag = brillo::SecureBlob(kFakeHsmPayloadTag);
  RecoveryRequestAssociatedData request_ad;
  request_ad.hsm_payload = std::move(hsm_payload);
  request_ad.request_meta_data = brillo::SecureBlob(kFakeRequestData);
  request_ad.epoch_pub_key = epoch_pub_key_;
  request_ad.request_payload_salt = salt;
  ASSERT_TRUE(
      SerializeRecoveryRequestAssociatedDataToCbor(request_ad, &cbor_output));

  HsmPayload deserialized_hsm_payload;
  brillo::SecureBlob deserialized_epoch_pub_key;
  brillo::SecureBlob deserialized_request_meta_data;
  int schema_version;
  ASSERT_TRUE(
      GetRequestPayloadSchemaVersionForTesting(cbor_output, &schema_version));
  EXPECT_EQ(schema_version, kProtocolVersion);

  ASSERT_TRUE(GetHsmPayloadFromRequestAdForTesting(cbor_output,
                                                   &deserialized_hsm_payload));
  EXPECT_EQ(deserialized_hsm_payload.cipher_text.to_string(),
            kFakeHsmPayloadCipherText);
  EXPECT_EQ(deserialized_hsm_payload.associated_data.to_string(),
            kFakeHsmPayloadAd);
  EXPECT_EQ(deserialized_hsm_payload.iv.to_string(), kFakeHsmPayloadIv);
  EXPECT_EQ(deserialized_hsm_payload.tag.to_string(), kFakeHsmPayloadTag);

  ExpectSerializedCborMapContainsValue(cbor_output, kEpochPublicKey,
                                       epoch_pub_key_);
  ExpectSerializedCborMapContainsValue(cbor_output, kRequestPayloadSalt, salt);
  ExpectSerializedCborMapContainsValue(cbor_output, kRequestMetaData,
                                       brillo::SecureBlob(kFakeRequestData));
  // TODO(anastasiian): add schema_version to structs according to the protocol
  // description.
  // 4 fields + schema version:
  EXPECT_EQ(5, GetCborMapSize(cbor_output));
}

// Verifies serialization of Recovery Request payload plain text encrypted
// payload to CBOR.
TEST_F(RecoveryRequestCborHelperTest, GeneratePlainText) {
  brillo::SecureBlob ephemeral_inverse_key;
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  BN_set_negative(scalar.get(), 1);
  crypto::ScopedEC_POINT inverse_point =
      ec_->MultiplyWithGenerator(*scalar, context_.get());
  ASSERT_TRUE(ec_->PointToSecureBlob(*inverse_point, &ephemeral_inverse_key,
                                     context_.get()));

  brillo::SecureBlob cbor_output;
  RecoveryRequestPlainText plain_text;
  plain_text.ephemeral_pub_inv_key = ephemeral_inverse_key;
  ASSERT_TRUE(
      SerializeRecoveryRequestPlainTextToCbor(plain_text, &cbor_output));

  ExpectSerializedCborMapContainsValue(cbor_output, kEphemeralPublicInvKey,
                                       ephemeral_inverse_key);
  EXPECT_EQ(1, GetCborMapSize(cbor_output));
}

// Verifies serialization of Recovery Request to CBOR.
TEST_F(RecoveryRequestCborHelperTest, SerializeRecoveryRequest) {
  RecoveryRequest request;
  request.request_payload = aead_helper_.GetFakePayload();

  brillo::SecureBlob cbor_output;
  EXPECT_TRUE(SerializeRecoveryRequestToCbor(request, &cbor_output));

  cbor::Value deserialized_request_payload;
  EXPECT_TRUE(GetValueFromCborMapByKeyForTesting(
      cbor_output, kRequestAead, &deserialized_request_payload));
  ASSERT_TRUE(deserialized_request_payload.is_map());
  aead_helper_.ExpectEqualsToFakeAeadPayload(
      deserialized_request_payload.GetMap());
}

// Verifies deserialization of Recovery Request from CBOR.
TEST_F(RecoveryRequestCborHelperTest, DeserializeRecoveryRequest) {
  cbor::Value::MapValue request;
  request.emplace(kRequestAead, aead_helper_.GetFakePayloadCborMap());
  brillo::SecureBlob request_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(request, &request_cbor));

  RecoveryRequest recovery_request;
  EXPECT_TRUE(
      DeserializeRecoveryRequestFromCbor(request_cbor, &recovery_request));
  aead_helper_.ExpectEqualsToFakeAeadPayload(recovery_request.request_payload);
}

// Verifies that deserialization of Recovery Request from CBOR fails if payload
// has wrong format.
TEST_F(RecoveryRequestCborHelperTest, DeserializeRecoveryRequestWrongFormat) {
  brillo::SecureBlob payload_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(aead_helper_.GetFakePayloadCborMap(),
                                      &payload_cbor));

  cbor::Value::MapValue request;
  // Field value is serialized CBOR instead of nested map.
  request.emplace(kRequestAead, payload_cbor);
  brillo::SecureBlob request_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(request, &request_cbor));

  RecoveryRequest recovery_request;
  EXPECT_FALSE(
      DeserializeRecoveryRequestFromCbor(request_cbor, &recovery_request));
}

// Verifies that deserialization of Recovery Request from CBOR fails if payload
// has a missing field.
TEST_F(RecoveryRequestCborHelperTest, DeserializeRecoveryRequestMissingField) {
  cbor::Value::MapValue payload = aead_helper_.GetFakePayloadCborMap();
  // Remove `iv` value.
  payload.erase(payload.find(cbor::Value(kAeadIv)));

  cbor::Value::MapValue request;
  request.emplace(kRequestAead, payload);
  brillo::SecureBlob request_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(request, &request_cbor));

  RecoveryRequest recovery_request;
  EXPECT_FALSE(
      DeserializeRecoveryRequestFromCbor(request_cbor, &recovery_request));
}

// Verifies serialization of Response payload associated data to CBOR.
TEST_F(RecoveryResponseCborHelperTest, SerializeAssociatedData) {
  HsmResponseAssociatedData response_ad;
  response_ad.response_meta_data = metadata_;
  response_ad.response_payload_salt = salt_;
  brillo::SecureBlob cbor_output;
  ASSERT_TRUE(
      SerializeHsmResponseAssociatedDataToCbor(response_ad, &cbor_output));

  ExpectSerializedCborMapContainsValue(cbor_output, kResponseMetaData,
                                       metadata_);
  ExpectSerializedCborMapContainsValue(cbor_output, kResponsePayloadSalt,
                                       salt_);
  EXPECT_EQ(2, GetCborMapSize(cbor_output));
}

// Verifies serialization of Response payload plain text to CBOR, without kav.
TEST_F(RecoveryResponseCborHelperTest, SerializePlainTextWithoutKav) {
  brillo::SecureBlob mediated_point("mediated point");

  HsmResponsePlainText response_plain_text;
  response_plain_text.mediated_point = mediated_point;
  response_plain_text.dealer_pub_key = fake_pub_key_;
  response_plain_text.key_auth_value = brillo::SecureBlob();
  brillo::SecureBlob cbor_output;
  ASSERT_TRUE(
      SerializeHsmResponsePlainTextToCbor(response_plain_text, &cbor_output));

  ExpectSerializedCborMapContainsValue(cbor_output, kMediatedPoint,
                                       mediated_point);
  ExpectSerializedCborMapContainsValue(cbor_output, kDealerPublicKey,
                                       fake_pub_key_);
  ExpectSerializedCborMapContainsValue(cbor_output, kKeyAuthValue,
                                       brillo::SecureBlob());
  EXPECT_EQ(3, GetCborMapSize(cbor_output));
}

// Verifies serialization of Response payload plain text to CBOR.
TEST_F(RecoveryResponseCborHelperTest, SerializePlainText) {
  brillo::SecureBlob mediated_point("mediated point");
  brillo::SecureBlob key_auth_value("key auth value");

  HsmResponsePlainText response_plain_text;
  response_plain_text.mediated_point = mediated_point;
  response_plain_text.dealer_pub_key = fake_pub_key_;
  response_plain_text.key_auth_value = key_auth_value;
  brillo::SecureBlob cbor_output;
  ASSERT_TRUE(
      SerializeHsmResponsePlainTextToCbor(response_plain_text, &cbor_output));

  ExpectSerializedCborMapContainsValue(cbor_output, kMediatedPoint,
                                       mediated_point);
  ExpectSerializedCborMapContainsValue(cbor_output, kDealerPublicKey,
                                       fake_pub_key_);
  ExpectSerializedCborMapContainsValue(cbor_output, kKeyAuthValue,
                                       key_auth_value);
  EXPECT_EQ(3, GetCborMapSize(cbor_output));
}

// Verifies deserialization of Response payload associated data from CBOR.
TEST_F(RecoveryResponseCborHelperTest, DeserializeAssociatedData) {
  cbor::Value::MapValue fake_map;
  fake_map.emplace(kResponseMetaData, metadata_);
  fake_map.emplace(kResponsePayloadSalt, salt_);
  brillo::SecureBlob response_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(fake_map, &response_cbor));

  HsmResponseAssociatedData response_ad;
  EXPECT_TRUE(DeserializeHsmResponseAssociatedDataFromCbor(response_cbor,
                                                           &response_ad));
  EXPECT_EQ(response_ad.response_meta_data, metadata_);
  EXPECT_EQ(response_ad.response_payload_salt, salt_);
}

// Verifies that deserialization of Response payload associated data from CBOR
// fails when input is not a map.
TEST_F(RecoveryResponseCborHelperTest, DeserializeAssociatedDataNotMap) {
  base::Optional<std::vector<uint8_t>> serialized =
      cbor::Writer::Write(cbor::Value("a CBOR but not a map"));
  ASSERT_TRUE(serialized.has_value());
  brillo::SecureBlob response_cbor(serialized.value().begin(),
                                   serialized.value().end());

  HsmResponseAssociatedData response_ad;
  EXPECT_FALSE(DeserializeHsmResponseAssociatedDataFromCbor(response_cbor,
                                                            &response_ad));
}

// Verifies that deserialization of Response payload associated data from CBOR
// fails when a field has a wrong format.
TEST_F(RecoveryResponseCborHelperTest, DeserializeAssociatedDataWrongFormat) {
  cbor::Value::MapValue fake_map;
  fake_map.emplace(kResponseMetaData, "a string value instead of bytes");
  fake_map.emplace(kResponsePayloadSalt, salt_);
  brillo::SecureBlob response_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(fake_map, &response_cbor));

  HsmResponseAssociatedData response_ad;
  EXPECT_FALSE(DeserializeHsmResponseAssociatedDataFromCbor(response_cbor,
                                                            &response_ad));
}

// Verifies deserialization of Response payload plain text from CBOR.
TEST_F(RecoveryResponseCborHelperTest, DeserializePlainText) {
  brillo::SecureBlob mediated_point("mediated point");
  cbor::Value::MapValue fake_map;
  fake_map.emplace(kMediatedPoint, mediated_point);
  fake_map.emplace(kDealerPublicKey, fake_pub_key_);
  fake_map.emplace(kKeyAuthValue, brillo::SecureBlob());
  brillo::SecureBlob response_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(fake_map, &response_cbor));

  HsmResponsePlainText response_plain_text;
  EXPECT_TRUE(DeserializeHsmResponsePlainTextFromCbor(response_cbor,
                                                      &response_plain_text));
  EXPECT_EQ(response_plain_text.mediated_point, mediated_point);
  EXPECT_EQ(response_plain_text.dealer_pub_key, fake_pub_key_);
  EXPECT_EQ(response_plain_text.key_auth_value, brillo::SecureBlob());
}

// Verifies that deserialization of Response payload plain text from CBOR fails
// when input is not a map.
TEST_F(RecoveryResponseCborHelperTest, DeserializePlainTextNotMap) {
  base::Optional<std::vector<uint8_t>> serialized =
      cbor::Writer::Write(cbor::Value("a CBOR but not a map"));
  ASSERT_TRUE(serialized.has_value());
  brillo::SecureBlob response_cbor(serialized.value().begin(),
                                   serialized.value().end());

  HsmResponsePlainText response_plain_text;
  EXPECT_FALSE(DeserializeHsmResponsePlainTextFromCbor(response_cbor,
                                                       &response_plain_text));
}

// Verifies that deserialization of Response payload plain text from CBOR fails
// when a field has a wrong format.
TEST_F(RecoveryResponseCborHelperTest, DeserializePlainTextWrongFormat) {
  cbor::Value::MapValue fake_map;
  fake_map.emplace(kMediatedPoint, "a string value instead of bytes");
  fake_map.emplace(kDealerPublicKey, fake_pub_key_);
  fake_map.emplace(kKeyAuthValue, brillo::SecureBlob());
  brillo::SecureBlob response_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(fake_map, &response_cbor));

  HsmResponsePlainText response_plain_text;
  EXPECT_FALSE(DeserializeHsmResponsePlainTextFromCbor(response_cbor,
                                                       &response_plain_text));
}

// Verifies serialization of Recovery Response to CBOR.
TEST_F(RecoveryResponseCborHelperTest, SerializeRecoveryResponse) {
  RecoveryResponse response;
  response.response_payload = aead_helper_.GetFakePayload();
  response.error_code = fake_error_code_;
  response.error_string = fake_error_string_;

  brillo::SecureBlob cbor_output;
  EXPECT_TRUE(SerializeRecoveryResponseToCbor(response, &cbor_output));

  cbor::Value deserialized_error_code;
  EXPECT_TRUE(GetValueFromCborMapByKeyForTesting(
      cbor_output, kResponseErrorCode, &deserialized_error_code));
  ASSERT_TRUE(deserialized_error_code.is_integer());
  EXPECT_EQ(deserialized_error_code.GetInteger(), response.error_code);

  cbor::Value deserialized_error_string;
  EXPECT_TRUE(GetValueFromCborMapByKeyForTesting(
      cbor_output, kResponseErrorString, &deserialized_error_string));
  ASSERT_TRUE(deserialized_error_string.is_string());
  EXPECT_EQ(deserialized_error_string.GetString(), response.error_string);

  cbor::Value deserialized_response_payload;
  EXPECT_TRUE(GetValueFromCborMapByKeyForTesting(
      cbor_output, kResponseAead, &deserialized_response_payload));
  ASSERT_TRUE(deserialized_response_payload.is_map());
  aead_helper_.ExpectEqualsToFakeAeadPayload(
      deserialized_response_payload.GetMap());
  EXPECT_EQ(3, GetCborMapSize(cbor_output));
}

// Verifies deserialization of Recovery Response from CBOR.
TEST_F(RecoveryResponseCborHelperTest, DeserializeRecoveryResponse) {
  cbor::Value::MapValue response;
  response.emplace(kResponseAead, aead_helper_.GetFakePayloadCborMap());
  response.emplace(kResponseErrorCode, fake_error_code_);
  response.emplace(kResponseErrorString, fake_error_string_);
  brillo::SecureBlob response_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(response, &response_cbor));

  RecoveryResponse recovery_response;
  EXPECT_TRUE(
      DeserializeRecoveryResponseFromCbor(response_cbor, &recovery_response));
  EXPECT_EQ(recovery_response.error_code, fake_error_code_);
  EXPECT_EQ(recovery_response.error_string, fake_error_string_);
  aead_helper_.ExpectEqualsToFakeAeadPayload(
      recovery_response.response_payload);
}

// Verifies that deserialization of Recovery Response from CBOR fails if payload
// has a wrong format.
TEST_F(RecoveryResponseCborHelperTest, DeserializeRecoveryResponseWrongFormat) {
  brillo::SecureBlob payload_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(aead_helper_.GetFakePayloadCborMap(),
                                      &payload_cbor));

  cbor::Value::MapValue response;
  // Field value is serialized CBOR instead of nested map.
  response.emplace(kResponseAead, payload_cbor);
  response.emplace(kResponseErrorCode, fake_error_code_);
  response.emplace(kResponseErrorString, fake_error_string_);
  brillo::SecureBlob response_cbor;
  ASSERT_TRUE(CreateCborMapForTesting(response, &response_cbor));

  RecoveryResponse recovery_response;
  EXPECT_FALSE(
      DeserializeRecoveryResponseFromCbor(response_cbor, &recovery_response));
}

}  // namespace cryptorecovery
}  // namespace cryptohome
