// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_attestation_context.h"

#include <memory>
#include <string>
#include <utility>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

namespace arc::keymint::context {

namespace {

constexpr ::keymaster::KmVersion kKeyMintVersion =
    ::keymaster::KmVersion::KEYMINT_2;

constexpr char kVerifiedBootState[] = "green";
constexpr char kLockedBootloaderState[] = "locked";
constexpr char kSampleVbMetaDigest[] =
    "ab76eece2ea8e2bea108d4dfd618bb6ab41096b291c6e83937637a941d87b303";

std::string keymasterBlobToString(keymaster_blob_t& blob) {
  return std::string(reinterpret_cast<const char*>(blob.data),
                     blob.data_length);
}

}  // namespace

class ArcAttestationContextTest : public ::testing::Test {
 protected:
  ArcAttestationContextTest() {}

  void SetUp() override {
    arc_attestation_context_ = new ArcAttestationContext(
        kKeyMintVersion, KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT);
  }

  void TearDown() override {}

  ArcAttestationContext* arc_attestation_context_;
};

TEST_F(ArcAttestationContextTest, GetVerifiedBootParams_Success) {
  // Prepare
  std::vector<uint8_t> vbmeta_digest =
      brillo::BlobFromString(kSampleVbMetaDigest);
  arc_attestation_context_->SetVerifiedBootParams(
      kVerifiedBootState, kLockedBootloaderState, vbmeta_digest);

  // Execute.
  keymaster_error_t get_params_error;
  auto result =
      arc_attestation_context_->GetVerifiedBootParams(&get_params_error);

  // Test.
  ASSERT_TRUE(result);
  EXPECT_EQ(KM_ERROR_OK, get_params_error);
  EXPECT_TRUE(result->device_locked);
  EXPECT_EQ(KM_VERIFIED_BOOT_VERIFIED, result->verified_boot_state);

  keymaster_blob_t boot_hash_blob = result->verified_boot_hash;
  EXPECT_EQ(kSampleVbMetaDigest, keymasterBlobToString(boot_hash_blob));
}
}  // namespace arc::keymint::context
