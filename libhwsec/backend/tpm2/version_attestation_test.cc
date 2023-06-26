// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/tpm_generated.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"
#include "libhwsec/platform/fake_platform.h"

using base::FilePath;
using brillo::BlobFromString;
using brillo::BlobToString;
using std::string;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using trunks::TPM_RC_FAILURE;
using trunks::TPM_RC_SUCCESS;

namespace hwsec {

class BackendVersionAttestationTpm2Test : public BackendTpm2TestBase {
 protected:
  StatusOr<ScopedKey> LoadFakeECCKey(const uint32_t fake_key_handle) {
    const OperationPolicy kFakePolicy{};
    const std::string kFakeKeyBlob = "fake_key_blob";
    const trunks::TPMT_PUBLIC kFakePublic = {
        .type = trunks::TPM_ALG_ECC,
    };

    EXPECT_CALL(proxy_->GetMockTpmUtility(), LoadKey(kFakeKeyBlob, _, _))
        .WillOnce(
            DoAll(SetArgPointee<2>(fake_key_handle), Return(TPM_RC_SUCCESS)));

    EXPECT_CALL(proxy_->GetMockTpmUtility(),
                GetKeyPublicArea(fake_key_handle, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFakePublic), Return(TPM_RC_SUCCESS)));

    return backend_->GetKeyManagementTpm2().LoadKey(
        kFakePolicy, BlobFromString(kFakeKeyBlob),
        Backend::KeyManagement::LoadKeyOptions{});
  }

  void ExpectGetKeyname(bool success) {
    EXPECT_CALL(proxy_->GetMockTpmUtility(), GetKeyName(kFakeKeyHandle, _))
        .WillOnce(DoAll(SetArgPointee<1>(kFakeKeyName),
                        Return(success ? TPM_RC_SUCCESS : TPM_RC_FAILURE)));
  }

  void ExpectQuote(bool success) {
    EXPECT_CALL(proxy_->GetMockTpm(),
                QuoteSync(kFakeKeyHandle, kFakeKeyName, _, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<5>(kFakeQuotedStruct),
                        SetArgPointee<6>(kFakeSignature),
                        Return(success ? TPM_RC_SUCCESS : TPM_RC_FAILURE)));
  }

  void SetupFakeFiles() {
    EXPECT_CALL(proxy_->GetFakePlatform(), ReadFileToString(kLsbReleasePath, _))
        .WillOnce(DoAll(SetArgPointee<1>(string(kFakeLsbReleaseContent)),
                        Return(true)));
    EXPECT_CALL(proxy_->GetFakePlatform(), ReadFileToString(kCmdlinePath, _))
        .WillOnce(
            DoAll(SetArgPointee<1>(string(kFakeCmdlineContent)), Return(true)));
  }

  static constexpr uint32_t kFakeKeyHandle = 0x1337;
  static constexpr char kFakeKeyName[] = "fake_key_name";
  static constexpr char kFakeCert[] = "fake_cert";
  static constexpr char kFakeChallenge[] = "fake_challenge";

  const trunks::TPMT_SIGNATURE kFakeSignature = {
      .sig_alg = trunks::TPM_ALG_ECDSA,
      .signature.ecdsa.signature_r =
          trunks::Make_TPM2B_ECC_PARAMETER("fake_quote_r"),
      .signature.ecdsa.signature_s =
          trunks::Make_TPM2B_ECC_PARAMETER("fake_quote_s"),
  };
  const trunks::TPM2B_ATTEST kFakeQuotedStruct =
      trunks::Make_TPM2B_ATTEST("fake_quoted_data");
  const FilePath kLsbReleasePath = FilePath("/etc/lsb-release");
  const FilePath kCmdlinePath = FilePath("/proc/cmdline");
  static constexpr char kFakeLsbReleaseContent[] = "this_is_lsb_release=true";
  static constexpr char kFakeCmdlineContent[] = "some_cmdline=something_else";
};

TEST_F(BackendVersionAttestationTpm2Test, Success) {
  auto load_key_result = LoadFakeECCKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  ExpectGetKeyname(true /* success */);
  ExpectQuote(true /* success */);
  SetupFakeFiles();

  auto result = backend_->GetVersionAttestationTpm2().AttestVersion(
      fake_key.GetKey(), kFakeCert, BlobFromString(kFakeChallenge));

  ASSERT_OK(result);
  EXPECT_EQ(result->version(), arc_attestation::CrOSVersionAttestationVersion::
                                   CROS_BLOB_VERSION_TPM2_FORMAT_1);
  EXPECT_EQ(result->tpm_certifying_key_cert(), kFakeCert);
  EXPECT_EQ(result->kernel_cmdline_quote(), "fake_quoted_data");
  EXPECT_NE(result->kernel_cmdline_quote_signature().find("fake_quote_r"),
            std::string::npos);
  EXPECT_NE(result->kernel_cmdline_quote_signature().find("fake_quote_s"),
            std::string::npos);
  EXPECT_EQ(result->kernel_cmdline_content(), kFakeCmdlineContent);
  EXPECT_EQ(result->lsb_release_content(), kFakeLsbReleaseContent);
}

TEST_F(BackendVersionAttestationTpm2Test, FailToGetKeyname) {
  auto load_key_result = LoadFakeECCKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  ExpectGetKeyname(false /* success */);
  SetupFakeFiles();

  auto result = backend_->GetVersionAttestationTpm2().AttestVersion(
      fake_key.GetKey(), kFakeCert, BlobFromString(kFakeChallenge));

  ASSERT_NOT_OK(result);
}

TEST_F(BackendVersionAttestationTpm2Test, FailToQuote) {
  auto load_key_result = LoadFakeECCKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  ExpectGetKeyname(true /* success */);
  ExpectQuote(false /* success */);
  SetupFakeFiles();

  auto result = backend_->GetVersionAttestationTpm2().AttestVersion(
      fake_key.GetKey(), kFakeCert, BlobFromString(kFakeChallenge));

  ASSERT_NOT_OK(result);
}

TEST_F(BackendVersionAttestationTpm2Test, FailToReadCmdline) {
  auto load_key_result = LoadFakeECCKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  EXPECT_CALL(proxy_->GetFakePlatform(), ReadFileToString(kLsbReleasePath, _))
      .WillOnce(Return(true));
  EXPECT_CALL(proxy_->GetFakePlatform(), ReadFileToString(kCmdlinePath, _))
      .WillOnce(Return(false));

  auto result = backend_->GetVersionAttestationTpm2().AttestVersion(
      fake_key.GetKey(), kFakeCert, BlobFromString(kFakeChallenge));

  ASSERT_NOT_OK(result);
}

TEST_F(BackendVersionAttestationTpm2Test, FailToReadLsbRelease) {
  auto load_key_result = LoadFakeECCKey(kFakeKeyHandle);
  ASSERT_OK(load_key_result);
  const ScopedKey& fake_key = load_key_result.value();

  EXPECT_CALL(proxy_->GetFakePlatform(), ReadFileToString(kLsbReleasePath, _))
      .WillOnce(Return(false));

  auto result = backend_->GetVersionAttestationTpm2().AttestVersion(
      fake_key.GetKey(), kFakeCert, BlobFromString(kFakeChallenge));

  ASSERT_NOT_OK(result);
}

}  // namespace hwsec
