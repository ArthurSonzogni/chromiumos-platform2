// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_u2f.h"

#include <optional>
#include <string>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "trunks/cr50_headers/u2f.h"
#include "trunks/error_codes.h"

namespace trunks {

namespace {

brillo::Blob GetAppId() {
  return brillo::Blob(U2F_APPID_SIZE, 0);
}

brillo::SecureBlob GetUserSecret() {
  return brillo::SecureBlob(U2F_USER_SECRET_SIZE, 1);
}

brillo::Blob GetAuthTimeSecretHash() {
  return brillo::Blob(SHA256_DIGEST_SIZE, 2);
}

brillo::Blob GetPublicKey() {
  return brillo::Blob(U2F_EC_POINT_SIZE, 3);
}

brillo::Blob GetKeyHandle() {
  return brillo::Blob(U2F_V0_KH_SIZE, 4);
}

brillo::Blob GetVersionedKeyHandle() {
  return brillo::Blob(U2F_V1_KH_SIZE, 5);
}

std::string GetU2fGenerateResp() {
  return brillo::BlobToString(
      brillo::CombineBlobs({GetPublicKey(), GetKeyHandle()}));
}

std::string GetU2fGenerateVersionedResp() {
  return brillo::BlobToString(
      brillo::CombineBlobs({GetPublicKey(), GetVersionedKeyHandle()}));
}

}  // namespace

// A placeholder test fixture to prevent typos.
class TpmU2fTest : public testing::Test {};

TEST_F(TpmU2fTest, SerializeU2fGenerate) {
  const brillo::Blob kInvalidAppId(31, 1);
  const brillo::SecureBlob kInvalidUserSecret(31, 1);
  std::string out;

  // Incorrect app_id size.
  EXPECT_EQ(Serialize_u2f_generate_t(0, kInvalidAppId, GetUserSecret(),
                                     /*consume=*/true,
                                     /*up_required=*/true, std::nullopt, &out),
            SAPI_RC_BAD_PARAMETER);
  EXPECT_TRUE(out.empty());

  // Incorrect user_secret size.
  EXPECT_EQ(Serialize_u2f_generate_t(0, GetAppId(), kInvalidUserSecret,
                                     /*consume=*/true,
                                     /*up_required=*/true, std::nullopt, &out),
            SAPI_RC_BAD_PARAMETER);
  EXPECT_TRUE(out.empty());

  // Invalid version.
  EXPECT_EQ(Serialize_u2f_generate_t(2, GetAppId(), GetUserSecret(),
                                     /*consume=*/true,
                                     /*up_required=*/true, std::nullopt, &out),
            SAPI_RC_BAD_PARAMETER);
  EXPECT_TRUE(out.empty());

  // auth_time_secret_hash should be nullopt for v0 requests.
  EXPECT_EQ(Serialize_u2f_generate_t(0, GetAppId(), GetUserSecret(),
                                     /*consume=*/true,
                                     /*up_required=*/true,
                                     GetAuthTimeSecretHash(), &out),
            SAPI_RC_BAD_PARAMETER);
  EXPECT_TRUE(out.empty());

  // auth_time_secret_hash shouldn't be nullopt for v1 requests.
  EXPECT_EQ(Serialize_u2f_generate_t(1, GetAppId(), GetUserSecret(),
                                     /*consume=*/true,
                                     /*up_required=*/true, std::nullopt, &out),
            SAPI_RC_BAD_PARAMETER);
  EXPECT_TRUE(out.empty());

  // Valid v0 requests.
  EXPECT_EQ(Serialize_u2f_generate_t(0, GetAppId(), GetUserSecret(),
                                     /*consume=*/false,
                                     /*up_required=*/false, std::nullopt, &out),
            TPM_RC_SUCCESS);
  EXPECT_EQ(out.length(), sizeof(u2f_generate_req));

  EXPECT_EQ(Serialize_u2f_generate_t(0, GetAppId(), GetUserSecret(),
                                     /*consume=*/false,
                                     /*up_required=*/true, std::nullopt, &out),
            TPM_RC_SUCCESS);
  EXPECT_EQ(out.length(), sizeof(u2f_generate_req));

  // Valid v1 requests.
  EXPECT_EQ(Serialize_u2f_generate_t(1, GetAppId(), GetUserSecret(),
                                     /*consume=*/true,
                                     /*up_required=*/false,
                                     GetAuthTimeSecretHash(), &out),
            TPM_RC_SUCCESS);
  EXPECT_EQ(out.length(), sizeof(u2f_generate_req));

  EXPECT_EQ(Serialize_u2f_generate_t(1, GetAppId(), GetUserSecret(),
                                     /*consume=*/true,
                                     /*up_required=*/true,
                                     GetAuthTimeSecretHash(), &out),
            TPM_RC_SUCCESS);
  EXPECT_EQ(out.length(), sizeof(u2f_generate_req));
}

TEST_F(TpmU2fTest, ParseU2fGenerate) {
  brillo::Blob public_key, key_handle;

  // Incorrect version.
  EXPECT_EQ(
      Parse_u2f_generate_t(GetU2fGenerateResp(), 2, &public_key, &key_handle),
      SAPI_RC_BAD_PARAMETER);
  EXPECT_TRUE(public_key.empty());
  EXPECT_TRUE(key_handle.empty());

  // Incorrect response size.
  EXPECT_EQ(
      Parse_u2f_generate_t(GetU2fGenerateResp(), 1, &public_key, &key_handle),
      SAPI_RC_BAD_SIZE);
  EXPECT_TRUE(public_key.empty());
  EXPECT_TRUE(key_handle.empty());

  EXPECT_EQ(Parse_u2f_generate_t(GetU2fGenerateVersionedResp(), 0, &public_key,
                                 &key_handle),
            SAPI_RC_BAD_SIZE);
  EXPECT_TRUE(public_key.empty());
  EXPECT_TRUE(key_handle.empty());

  // Valid responses.
  EXPECT_EQ(
      Parse_u2f_generate_t(GetU2fGenerateResp(), 0, &public_key, &key_handle),
      TPM_RC_SUCCESS);
  EXPECT_EQ(public_key, GetPublicKey());
  EXPECT_EQ(key_handle, GetKeyHandle());

  EXPECT_EQ(Parse_u2f_generate_t(GetU2fGenerateVersionedResp(), 1, &public_key,
                                 &key_handle),
            TPM_RC_SUCCESS);
  EXPECT_EQ(public_key, GetPublicKey());
  EXPECT_EQ(key_handle, GetVersionedKeyHandle());
}

}  // namespace trunks
