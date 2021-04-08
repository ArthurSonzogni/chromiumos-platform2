// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/big_num_util.h"

#include <crypto/scoped_openssl_types.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cryptohome {

TEST(BigNumUtilTest, SecureBlobConversions) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  brillo::SecureBlob blob;
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, &blob));
  EXPECT_FALSE(blob.empty());
  crypto::ScopedBIGNUM scalar2 = SecureBlobToBigNum(blob);
  EXPECT_EQ(BN_get_word(scalar.get()), BN_get_word(scalar2.get()));
}

TEST(BigNumUtilTest, ZeroConversions) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(0);
  ASSERT_TRUE(scalar);
  brillo::SecureBlob blob;
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, &blob));
  EXPECT_TRUE(blob.empty());
  crypto::ScopedBIGNUM scalar2 = SecureBlobToBigNum(blob);
  EXPECT_EQ(BN_is_zero(scalar.get()), 1);
}

}  // namespace cryptohome
