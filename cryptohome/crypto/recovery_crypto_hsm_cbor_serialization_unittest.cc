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

namespace cryptohome {

namespace {

constexpr EllipticCurve::CurveType kCurve = EllipticCurve::CurveType::kPrime256;
const char kOnboardingData[] = "fake onboarding data";

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

// Verifies serialization of HSM payload associated data to CBOR.
TEST_F(HsmPayloadCborHelperTest, GenerateAdCborWithoutRsaPublicKey) {
  brillo::SecureBlob rsa_public_key;
  brillo::SecureBlob onboarding_data(kOnboardingData);
  brillo::SecureBlob cbor_output;
  ASSERT_TRUE(SerializeHsmAssociatedDataToCbor(publisher_pub_key_,
                                               channel_pub_key_, rsa_public_key,
                                               onboarding_data, &cbor_output));
  brillo::SecureBlob deserialized_publisher_pub_key;
  brillo::SecureBlob deserialized_channel_pub_key;
  brillo::SecureBlob deserialized_onboarding_data;
  int schema_version;
  ASSERT_TRUE(
      GetHsmPayloadSchemaVersionForTesting(cbor_output, &schema_version));
  EXPECT_EQ(schema_version, 1);
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
  ASSERT_TRUE(SerializeHsmPlainTextToCbor(mediator_share, dealer_pub_key_,
                                          /*kav=*/brillo::SecureBlob(),
                                          &cbor_output));

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
// associated data..
TEST_F(HsmPayloadCborHelperTest, FailedAttemptToGetPlainTextFieldFromAd) {
  brillo::SecureBlob onboarding_data(kOnboardingData);
  brillo::SecureBlob cbor_output;
  ASSERT_TRUE(SerializeHsmAssociatedDataToCbor(
      publisher_pub_key_, channel_pub_key_,
      /*rsa_public_key*/ brillo::SecureBlob(), onboarding_data, &cbor_output));
  brillo::SecureBlob deserialized_dealer_pub_key;
  EXPECT_FALSE(GetHsmCborMapByKeyForTesting(cbor_output, kDealerPublicKey,
                                            &deserialized_dealer_pub_key));
}

}  // namespace cryptohome
