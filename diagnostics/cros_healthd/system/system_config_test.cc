// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/callback.h>
#include <base/files/scoped_temp_dir.h>
#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <base/test/task_environment.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <debugd/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/system/debugd_constants.h"
#include "diagnostics/cros_healthd/system/system_config.h"
#include "diagnostics/cros_healthd/system/system_config_constants.h"

using ::testing::_;
using ::testing::Return;
using ::testing::WithArg;

namespace diagnostics {
namespace {

// Fake marketing name used for testing cros config.
constexpr char kFakeMarketingName[] = "chromebook X 1234";
// Fake OEM name used for testing cros config.
constexpr char kFakeOemName[] = "Foo Bar OEM";
// Fake code name used for testing cros config.
constexpr char kFakeCodeName[] = "CodeName";

class SystemConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    system_config_ = std::make_unique<SystemConfig>(
        &fake_cros_config_, &debugd_proxy_, temp_dir_.GetPath());
    debugd_object_proxy_ =
        new dbus::MockObjectProxy(nullptr, "", dbus::ObjectPath("/"));
    ON_CALL(debugd_proxy_, GetObjectProxy())
        .WillByDefault(Return(debugd_object_proxy_.get()));
  }

  void TearDown() override { task_environment_.RunUntilIdle(); }

  bool NvmeSelfTestSupportedSync() {
    base::RunLoop run_loop;
    bool result;
    system_config()->NvmeSelfTestSupported(
        base::BindLambdaForTesting([&](bool response) {
          result = response;
          run_loop.Quit();
        }));
    run_loop.Run();
    return result;
  }

  void SetDebugdAvailability(bool available) {
    EXPECT_CALL(*debugd_object_proxy_.get(), DoWaitForServiceToBeAvailable(_))
        .WillOnce(WithArg<0>(
            [available](dbus::ObjectProxy::WaitForServiceToBeAvailableCallback*
                            callback) {
              std::move(*callback).Run(available);
            }));
  }

  brillo::FakeCrosConfig* fake_cros_config() { return &fake_cros_config_; }

  SystemConfig* system_config() { return system_config_.get(); }

  const base::FilePath& GetTempPath() const { return temp_dir_.GetPath(); }

  testing::NiceMock<org::chromium::debugdProxyMock> debugd_proxy_;
  scoped_refptr<dbus::MockObjectProxy> debugd_object_proxy_;

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  brillo::FakeCrosConfig fake_cros_config_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<SystemConfig> system_config_;
};

TEST_F(SystemConfigTest, FioSupportedTrue) {
  WriteFileAndCreateParentDirs(GetTempPath().AppendASCII(kFioToolPath), "");
  ASSERT_TRUE(system_config()->FioSupported());
}

TEST_F(SystemConfigTest, FioSupportedFalse) {
  ASSERT_FALSE(system_config()->FioSupported());
}

TEST_F(SystemConfigTest, TestBacklightTrue) {
  fake_cros_config()->SetString(kHardwarePropertiesPath, kHasBacklightProperty,
                                "");
  EXPECT_TRUE(system_config()->HasBacklight());
}

TEST_F(SystemConfigTest, TestBacklightFalse) {
  fake_cros_config()->SetString(kHardwarePropertiesPath, kHasBacklightProperty,
                                "false");
  EXPECT_FALSE(system_config()->HasBacklight());
}

TEST_F(SystemConfigTest, TestBacklightUnset) {
  EXPECT_TRUE(system_config()->HasBacklight());
}

TEST_F(SystemConfigTest, TestBatteryTrue) {
  fake_cros_config()->SetString(kHardwarePropertiesPath, kPsuTypeProperty, "");
  EXPECT_TRUE(system_config()->HasBattery());
}

TEST_F(SystemConfigTest, TestBatteryFalse) {
  fake_cros_config()->SetString(kHardwarePropertiesPath, kPsuTypeProperty,
                                "AC_only");
  EXPECT_FALSE(system_config()->HasBattery());
}

TEST_F(SystemConfigTest, TestBatteryUnset) {
  EXPECT_TRUE(system_config()->HasBattery());
}

TEST_F(SystemConfigTest, TestSkuNumberTrue) {
  fake_cros_config()->SetString(kCachedVpdPropertiesPath, kHasSkuNumberProperty,
                                "true");
  EXPECT_TRUE(system_config()->HasSkuNumber());
}

TEST_F(SystemConfigTest, TestSkuNumberFalse) {
  fake_cros_config()->SetString(kCachedVpdPropertiesPath, kHasSkuNumberProperty,
                                "");
  EXPECT_FALSE(system_config()->HasSkuNumber());
}

TEST_F(SystemConfigTest, TestSkuNumberUnset) {
  EXPECT_FALSE(system_config()->HasSkuNumber());
}

TEST_F(SystemConfigTest, TestSmartBatteryTrue) {
  fake_cros_config()->SetString(kBatteryPropertiesPath,
                                kHasSmartBatteryInfoProperty, "true");
  EXPECT_TRUE(system_config()->HasSmartBattery());
}

TEST_F(SystemConfigTest, TestSmartBatteryFalse) {
  fake_cros_config()->SetString(kBatteryPropertiesPath,
                                kHasSmartBatteryInfoProperty, "");
  EXPECT_FALSE(system_config()->HasSmartBattery());
}

TEST_F(SystemConfigTest, TestSmartBatteryUnset) {
  EXPECT_FALSE(system_config()->HasSmartBattery());
}

TEST_F(SystemConfigTest, TestPrivacyScreenTrue) {
  fake_cros_config()->SetString(kHardwarePropertiesPath,
                                kHasPrivacyScreenProperty, "true");
  EXPECT_TRUE(system_config()->HasPrivacyScreen());
}

TEST_F(SystemConfigTest, TestPrivacyScreenFalse) {
  fake_cros_config()->SetString(kHardwarePropertiesPath,
                                kHasPrivacyScreenProperty, "");
  EXPECT_FALSE(system_config()->HasPrivacyScreen());
}

TEST_F(SystemConfigTest, TestPrivacyScreenUnset) {
  EXPECT_FALSE(system_config()->HasPrivacyScreen());
}

TEST_F(SystemConfigTest, NvmeSupportedTrue) {
  WriteFileAndCreateParentDirs(GetTempPath().AppendASCII(kNvmeToolPath), "");
  WriteFileAndCreateParentDirs(
      GetTempPath().AppendASCII(kDevicePath).AppendASCII("nvme01p1"), "");
  ASSERT_TRUE(system_config()->NvmeSupported());
}

TEST_F(SystemConfigTest, NvmeSupportedToolOnlyFalse) {
  WriteFileAndCreateParentDirs(GetTempPath().AppendASCII(kNvmeToolPath), "");
  ASSERT_FALSE(system_config()->NvmeSupported());
}

TEST_F(SystemConfigTest, NvmeSupportedFalse) {
  ASSERT_FALSE(system_config()->NvmeSupported());
}

TEST_F(SystemConfigTest, NvmeSelfTestSupportedTrue) {
  constexpr char kResult[] = "test      : 0x100\noacs      : 0x17 ";
  EXPECT_CALL(debugd_proxy_, NvmeAsync(kNvmeIdentityOption, _, _, _))
      .WillOnce(WithArg<1>(
          [&](base::OnceCallback<void(const std::string&)> callback) {
            std::move(callback).Run(kResult);
          }));
  SetDebugdAvailability(true);
  EXPECT_TRUE(NvmeSelfTestSupportedSync());
}

TEST_F(SystemConfigTest, NvmeSelfTestSupportedFalse) {
  constexpr char kResult[] = "test      : 0x100\noacs      : 0x27 ";
  EXPECT_CALL(debugd_proxy_, NvmeAsync(kNvmeIdentityOption, _, _, _))
      .WillOnce(WithArg<1>(
          [&](base::OnceCallback<void(const std::string&)> callback) {
            std::move(callback).Run(kResult);
          }));
  SetDebugdAvailability(true);
  EXPECT_FALSE(NvmeSelfTestSupportedSync());
}

TEST_F(SystemConfigTest, NvmeSelfTestSupportedDebugdUnavailable) {
  SetDebugdAvailability(false);
  EXPECT_FALSE(NvmeSelfTestSupportedSync());
}

TEST_F(SystemConfigTest, SmartCtlSupportedTrue) {
  WriteFileAndCreateParentDirs(GetTempPath().AppendASCII(kSmartctlToolPath),
                               "");
  ASSERT_TRUE(system_config()->SmartCtlSupported());
}

TEST_F(SystemConfigTest, SmartCtlSupportedFalse) {
  ASSERT_FALSE(system_config()->SmartCtlSupported());
}

TEST_F(SystemConfigTest, FingerprintDiagnosticSupportedTrue) {
  fake_cros_config()->SetString(kFingerprintPropertiesPath,
                                kFingerprintRoutineEnable, "true");
  EXPECT_TRUE(system_config()->FingerprintDiagnosticSupported());
}

TEST_F(SystemConfigTest, FingerprintDiagnosticSupportedFalse) {
  fake_cros_config()->SetString(kFingerprintPropertiesPath,
                                kFingerprintRoutineEnable, "");
  EXPECT_FALSE(system_config()->FingerprintDiagnosticSupported());
}

TEST_F(SystemConfigTest, FingerprintDiagnosticSupportedUnset) {
  EXPECT_FALSE(system_config()->FingerprintDiagnosticSupported());
}

TEST_F(SystemConfigTest, WilcoDeviceTrue) {
  const auto wilco_board = *GetWilcoBoardNames().begin();
  auto lsb_release = "CHROMEOS_RELEASE_BOARD=" + wilco_board;
  base::test::ScopedChromeOSVersionInfo version(lsb_release, base::Time::Now());
  ASSERT_TRUE(system_config()->IsWilcoDevice());
}

TEST_F(SystemConfigTest, WilcoKernelNextDeviceTrue) {
  const auto wilco_board = *GetWilcoBoardNames().begin();
  auto lsb_release = "CHROMEOS_RELEASE_BOARD=" + wilco_board + "-kernelnext";
  base::test::ScopedChromeOSVersionInfo version(lsb_release, base::Time::Now());
  ASSERT_TRUE(system_config()->IsWilcoDevice());
}

TEST_F(SystemConfigTest, WilcoDeviceFalse) {
  auto lsb_release = "CHROMEOS_RELEASE_BOARD=mario";
  base::test::ScopedChromeOSVersionInfo version(lsb_release, base::Time::Now());
  ASSERT_FALSE(system_config()->IsWilcoDevice());
}

TEST_F(SystemConfigTest, CorrectMarketingName) {
  fake_cros_config()->SetString(kBrandingPath, kMarketingNameProperty,
                                kFakeMarketingName);
  EXPECT_TRUE(system_config()->GetMarketingName().has_value());
  EXPECT_EQ(system_config()->GetMarketingName().value(), kFakeMarketingName);
}

TEST_F(SystemConfigTest, MarketingNameUnset) {
  EXPECT_FALSE(system_config()->GetMarketingName().has_value());
}

TEST_F(SystemConfigTest, CorrectOemName) {
  fake_cros_config()->SetString(kBrandingPath, kOemNameProperty, kFakeOemName);
  EXPECT_TRUE(system_config()->GetOemName().has_value());
  EXPECT_EQ(system_config()->GetOemName().value(), kFakeOemName);
}

TEST_F(SystemConfigTest, OemNameUnset) {
  EXPECT_FALSE(system_config()->GetOemName().has_value());
}

TEST_F(SystemConfigTest, CorrectCodeName) {
  fake_cros_config()->SetString(kRootPath, kCodeNameProperty, kFakeCodeName);
  EXPECT_EQ(system_config()->GetCodeName(), kFakeCodeName);
}

TEST_F(SystemConfigTest, CodeNameUnset) {
  EXPECT_EQ(system_config()->GetCodeName(), "");
}

}  // namespace
}  // namespace diagnostics
