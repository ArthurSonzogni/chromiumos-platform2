// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/big_num_util.h"

#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cryptohome {

TEST(BigNumUtilTest, SecureBlobConversions) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  brillo::SecureBlob blob;
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, 1, &blob));
  EXPECT_FALSE(blob.empty());
  crypto::ScopedBIGNUM scalar2 = SecureBlobToBigNum(blob);
  EXPECT_EQ(BN_get_word(scalar.get()), BN_get_word(scalar2.get()));
}

TEST(BigNumUtilTest, SecureBlobPaddedConversions) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123123123u);
  ASSERT_TRUE(scalar);
  brillo::SecureBlob blob;
  EXPECT_FALSE(BigNumToSecureBlob(*scalar, 1, &blob));
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, 10, &blob));
  EXPECT_EQ(blob.size(), 10);
  EXPECT_FALSE(blob.empty());
  crypto::ScopedBIGNUM scalar2 = SecureBlobToBigNum(blob);
  EXPECT_EQ(BN_get_word(scalar.get()), BN_get_word(scalar2.get()));
}

TEST(BigNumUtilTest, ZeroConversions) {
  brillo::SecureBlob blob;
  EXPECT_TRUE(blob.empty());
  crypto::ScopedBIGNUM scalar = SecureBlobToBigNum(blob);
  EXPECT_EQ(BN_is_zero(scalar.get()), 1);
}

}  // namespace cryptohome
