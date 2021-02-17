// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/recovery_crypto.h"

#include "cryptohome/crypto/elliptic_curve.h"
#include "cryptohome/crypto/openssl_util.h"

#include <gtest/gtest.h>

namespace cryptohome {

TEST(RecoveryCryptoTest, RecoverDestination) {
  std::unique_ptr<RecoveryCrypto> recovery = RecoveryCrypto::Create();
  ASSERT_TRUE(recovery);

  brillo::SecureBlob mediator_share;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob dealer_pub_key;
  ASSERT_TRUE(recovery->GenerateShares(&mediator_share, &destination_share,
                                       &dealer_pub_key));

  brillo::SecureBlob publisher_pub_key;
  brillo::SecureBlob publisher_dh;
  ASSERT_TRUE(recovery->GeneratePublisherKeys(
      dealer_pub_key, &publisher_pub_key, &publisher_dh));

  brillo::SecureBlob mediated_publisher_pub_key;
  ASSERT_TRUE(recovery->Mediate(publisher_pub_key, mediator_share,
                                &mediated_publisher_pub_key));

  brillo::SecureBlob destination_dh;
  ASSERT_TRUE(recovery->RecoverDestination(publisher_pub_key, destination_share,
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

  brillo::SecureBlob mediator_share;
  brillo::SecureBlob destination_share;
  brillo::SecureBlob dealer_pub_key;
  ASSERT_TRUE(recovery->GenerateShares(&mediator_share, &destination_share,
                                       &dealer_pub_key));

  // Create invalid key that is just a scalar (not a point on a curve).
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  brillo::SecureBlob scalar_blob;
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, &scalar_blob));

  brillo::SecureBlob publisher_pub_key;
  brillo::SecureBlob publisher_dh;
  EXPECT_FALSE(recovery->GeneratePublisherKeys(scalar_blob, &publisher_pub_key,
                                               &publisher_dh));
  ASSERT_TRUE(recovery->GeneratePublisherKeys(
      dealer_pub_key, &publisher_pub_key, &publisher_dh));

  brillo::SecureBlob mediated_publisher_pub_key;
  EXPECT_FALSE(recovery->Mediate(scalar_blob, mediator_share,
                                 &mediated_publisher_pub_key));
  ASSERT_TRUE(recovery->Mediate(publisher_pub_key, mediator_share,
                                &mediated_publisher_pub_key));

  brillo::SecureBlob destination_dh;
  EXPECT_FALSE(recovery->RecoverDestination(
      publisher_pub_key, destination_share, scalar_blob, &destination_dh));
  EXPECT_FALSE(recovery->RecoverDestination(scalar_blob, destination_share,
                                            mediated_publisher_pub_key,
                                            &destination_dh));
  ASSERT_TRUE(recovery->RecoverDestination(publisher_pub_key, destination_share,
                                           mediated_publisher_pub_key,
                                           &destination_dh));
}

}  // namespace cryptohome
