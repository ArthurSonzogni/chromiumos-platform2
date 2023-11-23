// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/tpm_fetcher.h"

#include <optional>

#include <attestation/proto_bindings/interface.pb.h>
#include <attestation-client-test/attestation/dbus-proxy-mocks.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;
using ::testing::_;
using ::testing::WithArg;

class TpmFetcherTest : public BaseFileTest {
 protected:
  TpmFetcherTest() = default;
  TpmFetcherTest(const TpmFetcherTest&) = delete;
  TpmFetcherTest& operator=(const TpmFetcherTest&) = delete;

  void SetUp() override {
    auto version = tpm_manager::GetVersionInfoReply();
    EXPECT_CALL(*mock_tpm_manager_proxy(), GetVersionInfoAsync(_, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(version));
    auto tpm_status = tpm_manager::GetTpmNonsensitiveStatusReply();
    EXPECT_CALL(*mock_tpm_manager_proxy(),
                GetTpmNonsensitiveStatusAsync(_, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(tpm_status));
    auto dictionary_attack = tpm_manager::GetDictionaryAttackInfoReply();
    EXPECT_CALL(*mock_tpm_manager_proxy(),
                GetDictionaryAttackInfoAsync(_, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(dictionary_attack));
    auto attestation_status = attestation::GetStatusReply();
    EXPECT_CALL(*mock_attestation_proxy(), GetStatusAsync(_, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(attestation_status));
    auto supported_features = tpm_manager::GetSupportedFeaturesReply();
    EXPECT_CALL(*mock_tpm_manager_proxy(),
                GetSupportedFeaturesAsync(_, _, _, _))
        .WillRepeatedly(base::test::RunOnceCallback<1>(supported_features));
  }

  mojom::TpmResultPtr FetchTpmInfoSync() {
    base::test::TestFuture<mojom::TpmResultPtr> future;
    FetchTpmInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  org::chromium::TpmManagerProxyMock* mock_tpm_manager_proxy() {
    return mock_context_.mock_tpm_manager_proxy();
  }

  org::chromium::AttestationProxyMock* mock_attestation_proxy() {
    return mock_context_.mock_attestation_proxy();
  }

 private:
  base::test::TaskEnvironment task_environment_;
  MockContext mock_context_;
};

TEST_F(TpmFetcherTest, Success) {
  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_tpm_info());
  EXPECT_TRUE(result->get_tpm_info()->version);
  EXPECT_TRUE(result->get_tpm_info()->status);
  EXPECT_TRUE(result->get_tpm_info()->dictionary_attack);
  EXPECT_TRUE(result->get_tpm_info()->attestation);
  EXPECT_TRUE(result->get_tpm_info()->supported_features);
  EXPECT_EQ(result->get_tpm_info()->did_vid, std::nullopt);
}

TEST_F(TpmFetcherTest, GetTpmVersionInfoSuccess) {
  auto version = tpm_manager::GetVersionInfoReply();
  version.set_gsc_version(tpm_manager::GSC_VERSION_CR50);
  version.set_family(841887744);
  version.set_spec_level(116);
  version.set_manufacturer(1129467731);
  version.set_tpm_model(1);
  version.set_firmware_version(12227635737188317877u);
  version.set_vendor_specific("xCG fTPM");
  EXPECT_CALL(*mock_tpm_manager_proxy(), GetVersionInfoAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(version));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_tpm_info());
  const auto& out_version = result->get_tpm_info()->version;
  EXPECT_EQ(out_version->gsc_version, mojom::TpmGSCVersion::kCr50);
  EXPECT_EQ(out_version->family, 841887744);
  EXPECT_EQ(out_version->spec_level, 116);
  EXPECT_EQ(out_version->manufacturer, 1129467731);
  EXPECT_EQ(out_version->tpm_model, 1);
  EXPECT_EQ(out_version->firmware_version, 12227635737188317877u);
  EXPECT_EQ(out_version->vendor_specific, "xCG fTPM");
}

TEST_F(TpmFetcherTest, GetTpmVersionInfoBadStatus) {
  auto version = tpm_manager::GetVersionInfoReply();
  version.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_CALL(*mock_tpm_manager_proxy(), GetVersionInfoAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(version));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetTpmVersionInfoError) {
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(*mock_tpm_manager_proxy(), GetVersionInfoAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<2>(error.get()));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetTpmNonsensitiveStatusSuccess) {
  auto status = tpm_manager::GetTpmNonsensitiveStatusReply();
  status.set_is_enabled(true);
  status.set_is_owned(true);
  status.set_is_owner_password_present(false);
  EXPECT_CALL(*mock_tpm_manager_proxy(),
              GetTpmNonsensitiveStatusAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(status));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_tpm_info());
  const auto& out_status = result->get_tpm_info()->status;
  EXPECT_TRUE(out_status->enabled);
  EXPECT_TRUE(out_status->owned);
  EXPECT_FALSE(out_status->owner_password_is_present);
}

TEST_F(TpmFetcherTest, GetTpmNonsensitiveStatusBadStatus) {
  auto status = tpm_manager::GetTpmNonsensitiveStatusReply();
  status.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_CALL(*mock_tpm_manager_proxy(),
              GetTpmNonsensitiveStatusAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(status));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetTpmNonsensitiveStatusError) {
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(*mock_tpm_manager_proxy(),
              GetTpmNonsensitiveStatusAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<2>(error.get()));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetDictionaryAttackInfoSuccess) {
  auto da = tpm_manager::GetDictionaryAttackInfoReply();
  da.set_dictionary_attack_counter(0);
  da.set_dictionary_attack_threshold(200);
  da.set_dictionary_attack_lockout_in_effect(false);
  da.set_dictionary_attack_lockout_seconds_remaining(0);

  EXPECT_CALL(*mock_tpm_manager_proxy(),
              GetDictionaryAttackInfoAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(da));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_tpm_info());
  const auto& out_da = result->get_tpm_info()->dictionary_attack;
  EXPECT_EQ(out_da->counter, 0);
  EXPECT_EQ(out_da->threshold, 200);
  EXPECT_FALSE(out_da->lockout_in_effect);
  EXPECT_EQ(out_da->lockout_seconds_remaining, 0);
}

TEST_F(TpmFetcherTest, GetDictionaryAttackInfoBadStatus) {
  auto da = tpm_manager::GetDictionaryAttackInfoReply();
  da.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_CALL(*mock_tpm_manager_proxy(),
              GetDictionaryAttackInfoAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(da));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetDictionaryAttackInfoError) {
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(*mock_tpm_manager_proxy(),
              GetDictionaryAttackInfoAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<2>(error.get()));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetAttestationStatusSuccess) {
  auto status = attestation::GetStatusReply();
  status.set_prepared_for_enrollment(true);
  status.set_enrolled(false);

  EXPECT_CALL(*mock_attestation_proxy(), GetStatusAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(status));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_tpm_info());
  const auto& out_attestation = result->get_tpm_info()->attestation;
  EXPECT_TRUE(out_attestation->prepared_for_enrollment);
  EXPECT_FALSE(out_attestation->enrolled);
}

TEST_F(TpmFetcherTest, GetAttestationStatusBadStatus) {
  auto status = attestation::GetStatusReply();
  status.set_status(attestation::STATUS_UNEXPECTED_DEVICE_ERROR);
  EXPECT_CALL(*mock_attestation_proxy(), GetStatusAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(status));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetAttestationStatusError) {
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(*mock_attestation_proxy(), GetStatusAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<2>(error.get()));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetSupportedFeaturesSuccess) {
  auto supported_features = tpm_manager::GetSupportedFeaturesReply();
  supported_features.set_support_u2f(true);
  supported_features.set_support_pinweaver(true);
  supported_features.set_support_runtime_selection(false);
  supported_features.set_is_allowed(true);

  EXPECT_CALL(*mock_tpm_manager_proxy(), GetSupportedFeaturesAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(supported_features));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_tpm_info());
  const auto& out_supported_features =
      result->get_tpm_info()->supported_features;
  EXPECT_TRUE(out_supported_features->support_u2f);
  EXPECT_TRUE(out_supported_features->support_pinweaver);
  EXPECT_FALSE(out_supported_features->support_runtime_selection);
  EXPECT_TRUE(out_supported_features->is_allowed);
}

TEST_F(TpmFetcherTest, GetSupportedFeaturesBadStatus) {
  auto supported_features = tpm_manager::GetSupportedFeaturesReply();
  supported_features.set_status(tpm_manager::STATUS_DEVICE_ERROR);
  EXPECT_CALL(*mock_tpm_manager_proxy(), GetSupportedFeaturesAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<1>(supported_features));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, GetSupportedFeaturesError) {
  auto error = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(*mock_tpm_manager_proxy(), GetSupportedFeaturesAsync(_, _, _, _))
      .WillRepeatedly(base::test::RunOnceCallback<2>(error.get()));

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_error());

  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

TEST_F(TpmFetcherTest, TpmDidVid) {
  SetFile({kFileTpmDidVid}, "TEST_DID_VID");

  const auto result = FetchTpmInfoSync();
  ASSERT_TRUE(result->is_tpm_info());

  EXPECT_EQ(result->get_tpm_info()->did_vid, "TEST_DID_VID");
}

}  // namespace
}  // namespace diagnostics
