// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/recovery_crypto.h"

#include "cryptohome/crypto/big_num_util.h"
#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/fake_recovery_mediator_crypto.h"
#include "cryptohome/crypto/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/crypto/secure_blob_util.h"

#include <gtest/gtest.h>

namespace cryptohome {

namespace {

const char kFakeEnrollmentMetaData[] = "fake_enrollment_metadata";
const char kFakeRequestMetaData[] = "fake_request_metadata";

}  // namespace

TEST(RecoveryCryptoTest, RecoveryRequestPayloadTest) {
  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  ASSERT_TRUE(recovery);
  std::unique_ptr<FakeRecoveryMediatorCrypto> mediator =
      FakeRecoveryMediatorCrypto::Create();
  ASSERT_TRUE(mediator);

  brillo::SecureBlob mediator_pub_key;
  brillo::SecureBlob mediator_priv_key;
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(&mediator_pub_key));
  ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
      &mediator_priv_key));

  brillo::SecureBlob epoch_pub_key;
  brillo::SecureBlob epoch_priv_key;
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(&epoch_pub_key));
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeEpochPrivateKey(&epoch_priv_key));

  // Generates HSM payload that would be persisted on a chromebook.
  RecoveryCrypto::HsmPayload hsm_payload;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob recovery_key;
  brillo::SecureBlob channel_pub_key;
  brillo::SecureBlob channel_priv_key;
  ASSERT_TRUE(recovery->GenerateHsmPayload(
      mediator_pub_key,
      /*rsa_pub_key=*/brillo::SecureBlob(),
      brillo::SecureBlob(kFakeEnrollmentMetaData), &hsm_payload,
      &destination_share, &recovery_key, &channel_pub_key, &channel_priv_key));

  // Start recovery process.
  brillo::SecureBlob ephemeral_pub_key;
  RecoveryCrypto::RequestPayload request_payload;
  ASSERT_TRUE(recovery->GenerateRequestPayload(
      hsm_payload,
      brillo::SecureBlob(kFakeRequestMetaData), channel_priv_key,
      channel_pub_key, epoch_pub_key, &request_payload, &ephemeral_pub_key));

  // Simulates mediation performed by HSM.
  FakeRecoveryMediatorCrypto::ResponsePayload response_payload;
  ASSERT_TRUE(mediator->MediateRequestPayload(
      epoch_pub_key, epoch_priv_key, mediator_priv_key, request_payload,
      &response_payload));

  brillo::SecureBlob response_plain_text;
  ASSERT_TRUE(recovery->DecryptResponsePayload(
      channel_priv_key, epoch_pub_key, response_payload.cipher_text,
      response_payload.associated_data, response_payload.iv,
      response_payload.tag, &response_plain_text));

  brillo::SecureBlob mediated_share;
  brillo::SecureBlob dealer_pub_key;
  brillo::SecureBlob key_auth_value;
  ASSERT_TRUE(DeserializeHsmResponsePayloadFromCbor(
      response_plain_text, &mediated_share, &dealer_pub_key, &key_auth_value));

  brillo::SecureBlob mediated_recovery_key;
  ASSERT_TRUE(recovery->RecoverDestination(dealer_pub_key, destination_share,
                                           ephemeral_pub_key, mediated_share,
                                           &mediated_recovery_key));

  // Checks that cryptohome encryption key generated at enrollment and the
  // one obtained after migration are identical.
  EXPECT_EQ(recovery_key, mediated_recovery_key);
}

TEST(RecoveryCryptoTest, RecoverDestination) {
  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  ASSERT_TRUE(recovery);
  std::unique_ptr<FakeRecoveryMediatorCrypto> mediator =
      FakeRecoveryMediatorCrypto::Create();
  ASSERT_TRUE(mediator);

  brillo::SecureBlob mediator_pub_key;
  brillo::SecureBlob mediator_priv_key;
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(&mediator_pub_key));
  ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
      &mediator_priv_key));

  RecoveryCrypto::EncryptedMediatorShare encrypted_mediator_share;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob dealer_pub_key;
  ASSERT_TRUE(recovery->GenerateShares(mediator_pub_key,
                                       &encrypted_mediator_share,
                                       &destination_share, &dealer_pub_key));

  brillo::SecureBlob publisher_pub_key;
  brillo::SecureBlob publisher_dh;
  ASSERT_TRUE(recovery->GeneratePublisherKeys(
      dealer_pub_key, &publisher_pub_key, &publisher_dh));

  brillo::SecureBlob mediated_publisher_pub_key;
  ASSERT_TRUE(mediator->Mediate(mediator_priv_key, publisher_pub_key,
                                encrypted_mediator_share,
                                &mediated_publisher_pub_key));

  brillo::SecureBlob destination_dh;
  ASSERT_TRUE(recovery->RecoverDestination(publisher_pub_key, destination_share,
                                           /*ephemeral_pub_key=*/base::nullopt,
                                           mediated_publisher_pub_key,
                                           &destination_dh));

  // Verify that `publisher_dh` equals `destination_dh`.
  // It should be equal, since
  //   publisher_dh
  //     = dealer_pub_key * secret
  //     = G * (mediator_share + destination_share (mod order)) * secret
  //     = publisher_pub_key * (mediator_share + destination_share (mod order))
  // and
  //   destination_dh
  //     = publisher_pub_key * destination_share + mediated_publisher_pub_key
  //     = publisher_pub_key * destination_share
  //       + publisher_pub_key * mediator_share
  //     = publisher_pub_key * (mediator_share + destination_share (mod order))
  EXPECT_EQ(publisher_dh, destination_dh);
}

TEST(RecoveryCryptoTest, RecoverDestinationFromInvalidInput) {
  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  ASSERT_TRUE(recovery);

  brillo::SecureBlob mediator_pub_key;
  brillo::SecureBlob mediator_priv_key;
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(&mediator_pub_key));
  ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
      &mediator_priv_key));

  RecoveryCrypto::EncryptedMediatorShare encrypted_mediator_share;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob dealer_pub_key;
  ASSERT_TRUE(recovery->GenerateShares(mediator_pub_key,
                                       &encrypted_mediator_share,
                                       &destination_share, &dealer_pub_key));

  // Create invalid key that is just a scalar (not a point on a curve).
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  brillo::SecureBlob scalar_blob;
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, dealer_pub_key.size(), &scalar_blob));

  brillo::SecureBlob publisher_pub_key;
  brillo::SecureBlob publisher_dh;
  EXPECT_FALSE(recovery->GeneratePublisherKeys(scalar_blob, &publisher_pub_key,
                                               &publisher_dh));
  ASSERT_TRUE(recovery->GeneratePublisherKeys(
      dealer_pub_key, &publisher_pub_key, &publisher_dh));

  brillo::SecureBlob destination_dh;
  EXPECT_FALSE(recovery->RecoverDestination(
      publisher_pub_key, destination_share, /*ephemeral_pub_key=*/base::nullopt,
      scalar_blob, &destination_dh));
  EXPECT_FALSE(recovery->RecoverDestination(scalar_blob, destination_share,
                                            /*ephemeral_pub_key=*/base::nullopt,
                                            scalar_blob, &destination_dh));
}

TEST(RecoveryCryptoTest, SerializeEncryptedMediatorShare) {
  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  ASSERT_TRUE(recovery);

  brillo::SecureBlob mediator_pub_key;
  brillo::SecureBlob mediator_priv_key;
  ASSERT_TRUE(
      FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(&mediator_pub_key));
  ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
      &mediator_priv_key));

  RecoveryCrypto::EncryptedMediatorShare encrypted_mediator_share;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob dealer_pub_key;
  ASSERT_TRUE(recovery->GenerateShares(mediator_pub_key,
                                       &encrypted_mediator_share,
                                       &destination_share, &dealer_pub_key));

  brillo::SecureBlob serialized_blob;
  ASSERT_TRUE(RecoveryCrypto::SerializeEncryptedMediatorShareForTesting(
      encrypted_mediator_share, &serialized_blob));
  RecoveryCrypto::EncryptedMediatorShare encrypted_mediator_share2;
  ASSERT_TRUE(RecoveryCrypto::DeserializeEncryptedMediatorShareForTesting(
      serialized_blob, &encrypted_mediator_share2));
  EXPECT_EQ(encrypted_mediator_share.tag, encrypted_mediator_share2.tag);
  EXPECT_EQ(encrypted_mediator_share.iv, encrypted_mediator_share2.iv);
  EXPECT_EQ(encrypted_mediator_share.ephemeral_pub_key,
            encrypted_mediator_share2.ephemeral_pub_key);
  EXPECT_EQ(encrypted_mediator_share.encrypted_data,
            encrypted_mediator_share2.encrypted_data);
}

}  // namespace cryptohome
