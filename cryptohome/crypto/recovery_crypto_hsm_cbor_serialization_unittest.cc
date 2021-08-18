// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/recovery_crypto_hsm_cbor_serialization.h"

#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest.h>
#include <openssl/bn.h>

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/recovery_crypto_util.h"

namespace cryptohome {

namespace {

constexpr EllipticCurve::CurveType kCurve = EllipticCurve::CurveType::kPrime256;
const char kOnboardingData[] = "fake onboarding data";
const char kFakeRequestData[] = "fake request metadata";
const char kFakeHsmPayloadCipherText[] = "fake hsm payload cipher text";
const char kFakeHsmPayloadAd[] = "fake hsm payload ad";
const char kFakeHsmPayloadIv[] = "fake hsm payload iv";
const char kFakeHsmPayloadTag[] = "fake hsm payload tag";

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

class RequestPayloadCborHelperTest : public testing::Test {
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
  ScopedBN_CTX context_;
  base::Optional<EllipticCurve> ec_;
  brillo::SecureBlob epoch_pub_key_;
  brillo::SecureBlob epoch_priv_key_;
};

// Verifies serialization of HSM payload associated data to CBOR.
TEST_F(HsmPayloadCborHelperTest, GenerateAdCborWithoutRsaPublicKey) {
  brillo::SecureBlob cbor_output;
  cryptorecovery::HsmAssociatedData args;
  args.publisher_pub_key = publisher_pub_key_;
  args.channel_pub_key = channel_pub_key_;
  args.onboarding_meta_data = brillo::SecureBlob(kOnboardingData);
  ASSERT_TRUE(SerializeHsmAssociatedDataToCbor(args, &cbor_output));
  brillo::SecureBlob deserialized_publisher_pub_key;
  brillo::SecureBlob deserialized_channel_pub_key;
  brillo::SecureBlob deserialized_onboarding_data;
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kPublisherPublicKey,
                                           &deserialized_publisher_pub_key));
  EXPECT_EQ(publisher_pub_key_, deserialized_publisher_pub_key);
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kChannelPublicKey,
                                           &deserialized_channel_pub_key));
  EXPECT_EQ(channel_pub_key_, deserialized_channel_pub_key);
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kOnboardingMetaData,
                                           &deserialized_onboarding_data));
  EXPECT_EQ(deserialized_onboarding_data.to_string(), kOnboardingData);
}

// Verifies serialization of HSM payload plain text encrypted payload to CBOR.
TEST_F(HsmPayloadCborHelperTest, GeneratePlainTextHsmPayloadCbor) {
  brillo::SecureBlob mediator_share;
  brillo::SecureBlob cbor_output;

  crypto::ScopedBIGNUM scalar = BigNumFromValue(123123123u);
  ASSERT_TRUE(scalar);
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, 10, &mediator_share));

  // Serialize plain text payload with empty kav.
  cryptorecovery::HsmPlainText hsm_plain_text;
  hsm_plain_text.mediator_share = mediator_share;
  hsm_plain_text.dealer_pub_key = dealer_pub_key_;
  hsm_plain_text.key_auth_value = brillo::SecureBlob();
  ASSERT_TRUE(SerializeHsmPlainTextToCbor(hsm_plain_text, &cbor_output));

  brillo::SecureBlob deserialized_dealer_pub_key;
  brillo::SecureBlob deserialized_mediator_share;
  brillo::SecureBlob deserialized_key_auth_value;
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kDealerPublicKey,
                                           &deserialized_dealer_pub_key));
  EXPECT_EQ(dealer_pub_key_, deserialized_dealer_pub_key);
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kMediatorShare,
                                           &deserialized_mediator_share));
  EXPECT_EQ(BN_get_word(scalar.get()),
            BN_get_word(SecureBlobToBigNum(deserialized_mediator_share).get()));
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kKeyAuthValue,
                                           &deserialized_key_auth_value));
  EXPECT_TRUE(deserialized_key_auth_value.empty());
}

// Simulates failed attempt to get dealer public key from the Hsm payload
// associated data.
TEST_F(HsmPayloadCborHelperTest, FailedAttemptToGetPlainTextFieldFromAd) {
  brillo::SecureBlob cbor_output;
  cryptorecovery::HsmAssociatedData args;
  args.publisher_pub_key = publisher_pub_key_;
  args.channel_pub_key = channel_pub_key_;
  args.onboarding_meta_data = brillo::SecureBlob(kOnboardingData);
  ASSERT_TRUE(SerializeHsmAssociatedDataToCbor(args, &cbor_output));
  brillo::SecureBlob deserialized_dealer_pub_key;
  EXPECT_FALSE(GetHsmCborMapByKeyForTesting(cbor_output, kDealerPublicKey,
                                            &deserialized_dealer_pub_key));
}

// Verifies serialization of Recovery Request payload associated data to CBOR.
TEST_F(RequestPayloadCborHelperTest, GenerateAd) {
  brillo::SecureBlob cbor_output;
  cryptorecovery::RecoveryRequestAssociatedData args;
  args.hsm_aead_ct = brillo::SecureBlob(kFakeHsmPayloadCipherText);
  args.hsm_aead_ad = brillo::SecureBlob(kFakeHsmPayloadAd);
  args.hsm_aead_iv = brillo::SecureBlob(kFakeHsmPayloadIv);
  args.hsm_aead_tag = brillo::SecureBlob(kFakeHsmPayloadTag);
  args.request_meta_data = brillo::SecureBlob(kFakeRequestData);
  args.epoch_pub_key = epoch_pub_key_;
  ASSERT_TRUE(SerializeRecoveryRequestAssociatedDataToCbor(args, &cbor_output));
  brillo::SecureBlob deserialized_epoch_pub_key;
  brillo::SecureBlob deserialized_hsm_aead_ct;
  brillo::SecureBlob deserialized_hsm_aead_ad;
  brillo::SecureBlob deserialized_hsm_aead_iv;
  brillo::SecureBlob deserialized_hsm_aead_tag;
  brillo::SecureBlob deserialized_request_meta_data;
  int schema_version;
  ASSERT_TRUE(
      GetRequestPayloadSchemaVersionForTesting(cbor_output, &schema_version));
  EXPECT_EQ(schema_version, kProtocolVersion);

  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kHsmAeadCipherText,
                                           &deserialized_hsm_aead_ct));
  EXPECT_EQ(deserialized_hsm_aead_ct.to_string(), kFakeHsmPayloadCipherText);
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kHsmAeadAd,
                                           &deserialized_hsm_aead_ad));
  EXPECT_EQ(deserialized_hsm_aead_ad.to_string(), kFakeHsmPayloadAd);
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kHsmAeadIv,
                                           &deserialized_hsm_aead_iv));
  EXPECT_EQ(deserialized_hsm_aead_iv.to_string(), kFakeHsmPayloadIv);
  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kHsmAeadTag,
                                           &deserialized_hsm_aead_tag));
  EXPECT_EQ(deserialized_hsm_aead_tag.to_string(), kFakeHsmPayloadTag);

  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kEpochPublicKey,
                                           &deserialized_epoch_pub_key));
  EXPECT_EQ(epoch_pub_key_, deserialized_epoch_pub_key);

  ASSERT_TRUE(GetHsmCborMapByKeyForTesting(cbor_output, kRequestMetaData,
                                           &deserialized_request_meta_data));
  EXPECT_EQ(deserialized_request_meta_data.to_string(), kFakeRequestData);
}

// Verifies serialization of Recovery Request payload plain text encrypted
// payload to CBOR.
TEST_F(RequestPayloadCborHelperTest, GeneratePlainText) {
  brillo::SecureBlob ephemeral_inverse_key;
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  BN_set_negative(scalar.get(), 1);
  crypto::ScopedEC_POINT inverse_point =
      ec_->MultiplyWithGenerator(*scalar, context_.get());
  ASSERT_TRUE(ec_->PointToSecureBlob(*inverse_point, &ephemeral_inverse_key,
                                     context_.get()));

  brillo::SecureBlob cbor_output;
  cryptorecovery::RecoveryRequestPlainText plain_text;
  plain_text.ephemeral_pub_inv_key = ephemeral_inverse_key;
  ASSERT_TRUE(
      SerializeRecoveryRequestPlainTextToCbor(plain_text, &cbor_output));

  brillo::SecureBlob deserialized_ephemeral_inverse_key;
  ASSERT_TRUE(
      GetHsmCborMapByKeyForTesting(cbor_output, kEphemeralPublicInvKey,
                                   &deserialized_ephemeral_inverse_key));
  EXPECT_EQ(ephemeral_inverse_key, deserialized_ephemeral_inverse_key);
}

}  // namespace cryptohome
