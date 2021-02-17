// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/crypto/openssl_util.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <openssl/bnerr.h>
#include <openssl/err.h>

namespace cryptohome {

TEST(OpenSSLUtilTest, SecureBlobConversions) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(123u);
  ASSERT_TRUE(scalar);
  brillo::SecureBlob blob;
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, &blob));
  EXPECT_FALSE(blob.empty());
  crypto::ScopedBIGNUM scalar2 = SecureBlobToBigNum(blob);
  EXPECT_EQ(BN_get_word(scalar.get()), BN_get_word(scalar2.get()));
}

TEST(OpenSSLUtilTest, ZeroConversions) {
  crypto::ScopedBIGNUM scalar = BigNumFromValue(0);
  ASSERT_TRUE(scalar);
  brillo::SecureBlob blob;
  ASSERT_TRUE(BigNumToSecureBlob(*scalar, &blob));
  EXPECT_TRUE(blob.empty());
  crypto::ScopedBIGNUM scalar2 = SecureBlobToBigNum(blob);
  EXPECT_EQ(BN_is_zero(scalar.get()), 1);
}

TEST(OpenSSLUtilTest, ContextAndErrorHandling) {
  ScopedBN_CTX context = CreateBigNumContext();
  ASSERT_TRUE(context);

  EXPECT_TRUE(GetOpenSSLErrors().empty());

  // Trigger 2 different errors to get more interesting OpenSSL error queue.
  ERR_PUT_error(ERR_LIB_BN, 0, BN_R_INPUT_NOT_REDUCED, "", 0);
  ERR_PUT_error(ERR_LIB_BN, 0, BN_R_DIV_BY_ZERO, "", 0);
  EXPECT_THAT(GetOpenSSLErrors(), testing::ContainsRegex(".*;.*;"));

  EXPECT_TRUE(GetOpenSSLErrors().empty());
}

}  // namespace cryptohome
