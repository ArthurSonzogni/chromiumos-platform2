// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_test_utils.h"

#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>


using brillo::Blob;
using brillo::SecureBlob;

namespace cryptohome {

ChallengeCredentialsHelper::GenerateNewCallback
MakeChallengeCredentialsGenerateNewResultWriter(
    std::unique_ptr<ChallengeCredentialsGenerateNewResult>* result) {
  DCHECK(!*result);
  return base::BindOnce(
      [](std::unique_ptr<ChallengeCredentialsGenerateNewResult>* result,
         std::unique_ptr<structure::SignatureChallengeInfo>
             signature_challenge_info,
         std::unique_ptr<brillo::SecureBlob> passkey) {
        ASSERT_FALSE(*result);
        *result = std::make_unique<ChallengeCredentialsGenerateNewResult>();
        (*result)->signature_challenge_info =
            std::move(signature_challenge_info);
        (*result)->passkey = std::move(passkey);
      },
      base::Unretained(result));
}

ChallengeCredentialsHelper::DecryptCallback
MakeChallengeCredentialsDecryptResultWriter(
    std::unique_ptr<ChallengeCredentialsDecryptResult>* result) {
  DCHECK(!*result);
  return base::BindOnce(
      [](std::unique_ptr<ChallengeCredentialsDecryptResult>* result,
         std::unique_ptr<brillo::SecureBlob> passkey) {
        ASSERT_FALSE(*result);
        *result = std::make_unique<ChallengeCredentialsDecryptResult>();
        (*result)->passkey = std::move(passkey);
      },
      base::Unretained(result));
}

void VerifySuccessfulChallengeCredentialsGenerateNewResult(
    const ChallengeCredentialsGenerateNewResult& result,
    const SecureBlob& expected_passkey) {
  ASSERT_TRUE(result.passkey);
  ASSERT_TRUE(result.signature_challenge_info);
  EXPECT_EQ(expected_passkey, *result.passkey);
}

void VerifySuccessfulChallengeCredentialsDecryptResult(
    const ChallengeCredentialsDecryptResult& result,
    const SecureBlob& expected_passkey) {
  ASSERT_TRUE(result.passkey);
  EXPECT_EQ(expected_passkey, *result.passkey);
}

void VerifyFailedChallengeCredentialsGenerateNewResult(
    const ChallengeCredentialsGenerateNewResult& result) {
  EXPECT_FALSE(result.passkey);
  EXPECT_FALSE(result.signature_challenge_info);
}

void VerifyFailedChallengeCredentialsDecryptResult(
    const ChallengeCredentialsDecryptResult& result) {
  EXPECT_FALSE(result.passkey);
}

}  // namespace cryptohome
