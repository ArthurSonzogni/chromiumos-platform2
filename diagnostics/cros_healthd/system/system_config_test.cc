// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/functional/callback.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/scoped_chromeos_version_info.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <debugd/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/base/paths.h"
#include "diagnostics/cros_healthd/service_config.h"
#include "diagnostics/cros_healthd/system/cros_config.h"
#include "diagnostics/cros_healthd/system/debugd_constants.h"
#include "diagnostics/cros_healthd/system/system_config.h"
#include "diagnostics/cros_healthd/system/system_config_constants.h"

using ::testing::_;
using ::testing::Return;
using ::testing::WithArg;

namespace diagnostics {
namespace {

namespace paths = paths::cros_config;

// Fake marketing name used for testing cros config.
constexpr char kFakeMarketingName[] = "chromebook X 1234";
// Fake OEM name used for testing cros config.
constexpr char kFakeOemName[] = "Foo Bar OEM";
// Fake code name used for testing cros config.
constexpr char kFakeCodeName[] = "CodeName";

class SystemConfigTest : public BaseFileTest {
 protected:
  void SetUp() override {
    system_config_ =
        std::make_unique<SystemConfig>(&cros_config_, &debugd_proxy_);
    debugd_object_proxy_ =
        new dbus::MockObjectProxy(nullptr, "", dbus::ObjectPath("/"));
    ON_CALL(debugd_proxy_, GetObjectProxy())
        .WillByDefault(Return(debugd_object_proxy_.get()));
  }

  void TearDown() override { task_environment_.RunUntilIdle(); }

  bool NvmeSelfTestSupportedSync() {
    base::test::TestFuture<bool> future;
    system_config()->NvmeSelfTestSupported(future.GetCallback());
    return future.Get();
  }

  void SetDebugdAvailability(bool available) {
    EXPECT_CALL(*debugd_object_proxy_.get(), DoWaitForServiceToBeAvailable(_))
        .WillOnce(WithArg<0>(
            [available](dbus::ObjectProxy::WaitForServiceToBeAvailableCallback*
                            callback) {
              std::move(*callback).Run(available);
            }));
  }

  SystemConfig* system_config() { return system_config_.get(); }

  base::test::SingleThreadTaskEnvironment task_environment_;
  CrosConfig cros_config_{ServiceConfig{}};
  std::unique_ptr<SystemConfig> system_config_;
  testing::NiceMock<org::chromium::debugdProxyMock> debugd_proxy_;
  scoped_refptr<dbus::MockObjectProxy> debugd_object_proxy_;
};

TEST_F(SystemConfigTest, TestBacklightTrue) {
  SetFakeCrosConfig(paths::kHasBacklight, "");
  EXPECT_TRUE(system_config()->HasBacklight());
}

TEST_F(SystemConfigTest, TestBacklightFalse) {
  SetFakeCrosConfig(paths::kHasBacklight, "false");
  EXPECT_FALSE(system_config()->HasBacklight());
}

TEST_F(SystemConfigTest, TestBacklightUnset) {
  EXPECT_TRUE(system_config()->HasBacklight());
}

TEST_F(SystemConfigTest, TestBatteryTrue) {
  SetFakeCrosConfig(paths::kPsuType, "");
  EXPECT_TRUE(system_config()->HasBattery());
}

TEST_F(SystemConfigTest, TestBatteryFalse) {
  SetFakeCrosConfig(paths::kPsuType, "AC_only");
  EXPECT_FALSE(system_config()->HasBattery());
}

TEST_F(SystemConfigTest, TestBatteryUnset) {
  EXPECT_TRUE(system_config()->HasBattery());
}

TEST_F(SystemConfigTest, TestSkuNumberTrue) {
  SetFakeCrosConfig(paths::kHasSkuNumber, "true");
  EXPECT_TRUE(system_config()->HasSkuNumber());
}

TEST_F(SystemConfigTest, TestSkuNumberFalse) {
  SetFakeCrosConfig(paths::kHasSkuNumber, "");
  EXPECT_FALSE(system_config()->HasSkuNumber());
}

TEST_F(SystemConfigTest, TestSkuNumberUnset) {
  EXPECT_FALSE(system_config()->HasSkuNumber());
}

TEST_F(SystemConfigTest, TestSmartBatteryTrue) {
  SetFakeCrosConfig(paths::kHasSmartBatteryInfo, "true");
  EXPECT_TRUE(system_config()->HasSmartBattery());
}

TEST_F(SystemConfigTest, TestSmartBatteryFalse) {
  SetFakeCrosConfig(paths::kHasSmartBatteryInfo, "");
  EXPECT_FALSE(system_config()->HasSmartBattery());
}

TEST_F(SystemConfigTest, TestSmartBatteryUnset) {
  EXPECT_FALSE(system_config()->HasSmartBattery());
}

TEST_F(SystemConfigTest, TestPrivacyScreenTrue) {
  SetFakeCrosConfig(paths::kHasPrivacyScreen, "true");
  EXPECT_TRUE(system_config()->HasPrivacyScreen());
}

TEST_F(SystemConfigTest, TestPrivacyScreenFalse) {
  SetFakeCrosConfig(paths::kHasPrivacyScreen, "");
  EXPECT_FALSE(system_config()->HasPrivacyScreen());
}

TEST_F(SystemConfigTest, TestPrivacyScreenUnset) {
  EXPECT_FALSE(system_config()->HasPrivacyScreen());
}

TEST_F(SystemConfigTest, TestChromiumECTrue) {
  SetFile(kChromiumECPath, "");
  EXPECT_TRUE(system_config()->HasChromiumEC());
}

TEST_F(SystemConfigTest, TestChromiumECFalse) {
  EXPECT_FALSE(system_config()->HasChromiumEC());
}

TEST_F(SystemConfigTest, NvmeSupportedTrue) {
  SetFile(kNvmeToolPath, "");
  SetFile({kDevicePath, "nvme01p1"}, "");
  ASSERT_TRUE(system_config()->NvmeSupported());
}

TEST_F(SystemConfigTest, NvmeSupportedToolOnlyFalse) {
  SetFile(kNvmeToolPath, "");
  ASSERT_FALSE(system_config()->NvmeSupported());
}

TEST_F(SystemConfigTest, NvmeSupportedFalse) {
  ASSERT_FALSE(system_config()->NvmeSupported());
}

TEST_F(SystemConfigTest, NvmeSelfTestSupportedTrue) {
  constexpr char kResult[] = "test      : 0x100\noacs      : 0x17 ";
  EXPECT_CALL(debugd_proxy_, NvmeAsync(kNvmeIdentityOption, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(std::string(kResult)));
  SetDebugdAvailability(true);
  EXPECT_TRUE(NvmeSelfTestSupportedSync());
}

TEST_F(SystemConfigTest, NvmeSelfTestSupportedFalse) {
  constexpr char kResult[] = "test      : 0x100\noacs      : 0x27 ";
  EXPECT_CALL(debugd_proxy_, NvmeAsync(kNvmeIdentityOption, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(std::string(kResult)));
  SetDebugdAvailability(true);
  EXPECT_FALSE(NvmeSelfTestSupportedSync());
}

TEST_F(SystemConfigTest, NvmeSelfTestSupportedDebugdUnavailable) {
  SetDebugdAvailability(false);
  EXPECT_FALSE(NvmeSelfTestSupportedSync());
}

TEST_F(SystemConfigTest, SmartCtlSupportedTrue) {
  SetFile(kSmartctlToolPath, "");
  ASSERT_TRUE(system_config()->SmartCtlSupported());
}

TEST_F(SystemConfigTest, SmartCtlSupportedFalse) {
  ASSERT_FALSE(system_config()->SmartCtlSupported());
}

TEST_F(SystemConfigTest, MmcSupportedTrue) {
  SetFile(kMmcToolPath, "");
  ASSERT_TRUE(system_config()->MmcSupported());
}

TEST_F(SystemConfigTest, MmcSupportedFalse) {
  ASSERT_FALSE(system_config()->MmcSupported());
}

TEST_F(SystemConfigTest, FingerprintDiagnosticSupportedTrue) {
  SetFakeCrosConfig(paths::kFingerprintDiagRoutineEnable, "true");
  EXPECT_TRUE(system_config()->FingerprintDiagnosticSupported());
}

TEST_F(SystemConfigTest, FingerprintDiagnosticSupportedFalse) {
  SetFakeCrosConfig(paths::kFingerprintDiagRoutineEnable, "");
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
  SetFakeCrosConfig(paths::kMarketingName, kFakeMarketingName);
  EXPECT_TRUE(system_config()->GetMarketingName().has_value());
  EXPECT_EQ(system_config()->GetMarketingName().value(), kFakeMarketingName);
}

TEST_F(SystemConfigTest, MarketingNameUnset) {
  EXPECT_FALSE(system_config()->GetMarketingName().has_value());
}

TEST_F(SystemConfigTest, CorrectOemName) {
  SetFakeCrosConfig(paths::kOemName, kFakeOemName);
  EXPECT_TRUE(system_config()->GetOemName().has_value());
  EXPECT_EQ(system_config()->GetOemName().value(), kFakeOemName);
}

TEST_F(SystemConfigTest, OemNameUnset) {
  EXPECT_FALSE(system_config()->GetOemName().has_value());
}

TEST_F(SystemConfigTest, CorrectCodeName) {
  SetFakeCrosConfig(paths::kCodeName, kFakeCodeName);
  EXPECT_EQ(system_config()->GetCodeName(), kFakeCodeName);
}

TEST_F(SystemConfigTest, CodeNameUnset) {
  EXPECT_EQ(system_config()->GetCodeName(), "");
}

TEST_F(SystemConfigTest, TestBaseAccelerometerTrue) {
  SetFakeCrosConfig(paths::kHasBaseAccelerometer, "true");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kBaseAccelerometer);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_TRUE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestBaseAccelerometerFalse) {
  SetFakeCrosConfig(paths::kHasBaseAccelerometer, "false");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kBaseAccelerometer);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_FALSE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestBaseAccelerometerUnset) {
  EXPECT_FALSE(
      system_config()->HasSensor(SensorType::kBaseAccelerometer).has_value());
}

TEST_F(SystemConfigTest, TestBaseGyroscopeTrue) {
  SetFakeCrosConfig(paths::kHasBaseGyroscope, "true");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kBaseGyroscope);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_TRUE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestBaseGyroscopeFalse) {
  SetFakeCrosConfig(paths::kHasBaseGyroscope, "false");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kBaseGyroscope);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_FALSE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestBaseGyroscopeUnset) {
  EXPECT_FALSE(
      system_config()->HasSensor(SensorType::kBaseGyroscope).has_value());
}

TEST_F(SystemConfigTest, TestBaseMagnetometerTrue) {
  SetFakeCrosConfig(paths::kHasBaseMagnetometer, "true");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kBaseMagnetometer);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_TRUE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestBaseMagnetometerFalse) {
  SetFakeCrosConfig(paths::kHasBaseMagnetometer, "false");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kBaseMagnetometer);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_FALSE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestBaseMagnetometerUnset) {
  EXPECT_FALSE(
      system_config()->HasSensor(SensorType::kBaseMagnetometer).has_value());
}

TEST_F(SystemConfigTest, TestBaseGravitySensorTrue) {
  SetFakeCrosConfig(paths::kHasBaseAccelerometer, "true");
  SetFakeCrosConfig(paths::kHasBaseGyroscope, "true");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kBaseGravitySensor);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_TRUE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestBaseGravitySensorFalse) {
  SetFakeCrosConfig(paths::kHasBaseAccelerometer, "false");
  SetFakeCrosConfig(paths::kHasBaseGyroscope, "false");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kBaseGravitySensor);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_FALSE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestBaseGravitySensorUnset) {
  EXPECT_FALSE(
      system_config()->HasSensor(SensorType::kBaseGravitySensor).has_value());
}

TEST_F(SystemConfigTest, TestLidAccelerometerTrue) {
  SetFakeCrosConfig(paths::kHasLidAccelerometer, "true");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kLidAccelerometer);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_TRUE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestLidAccelerometerFalse) {
  SetFakeCrosConfig(paths::kHasLidAccelerometer, "false");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kLidAccelerometer);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_FALSE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestLidAccelerometerUnset) {
  EXPECT_FALSE(
      system_config()->HasSensor(SensorType::kLidAccelerometer).has_value());
}

TEST_F(SystemConfigTest, TestLidGyroscopeTrue) {
  SetFakeCrosConfig(paths::kHasLidGyroscope, "true");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kLidGyroscope);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_TRUE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestLidGyroscopeFalse) {
  SetFakeCrosConfig(paths::kHasLidGyroscope, "false");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kLidGyroscope);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_FALSE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestLidGyroscopeUnset) {
  EXPECT_FALSE(
      system_config()->HasSensor(SensorType::kLidGyroscope).has_value());
}

TEST_F(SystemConfigTest, TestLidMagnetometerTrue) {
  SetFakeCrosConfig(paths::kHasLidMagnetometer, "true");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kLidMagnetometer);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_TRUE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestLidMagnetometerFalse) {
  SetFakeCrosConfig(paths::kHasLidMagnetometer, "false");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kLidMagnetometer);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_FALSE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestLidMagnetometerUnset) {
  EXPECT_FALSE(
      system_config()->HasSensor(SensorType::kLidMagnetometer).has_value());
}

TEST_F(SystemConfigTest, TestLidGravitySensorTrue) {
  SetFakeCrosConfig(paths::kHasLidAccelerometer, "true");
  SetFakeCrosConfig(paths::kHasLidGyroscope, "true");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kLidGravitySensor);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_TRUE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestLidGravitySensorFalse) {
  SetFakeCrosConfig(paths::kHasLidAccelerometer, "false");
  SetFakeCrosConfig(paths::kHasLidGyroscope, "false");
  const auto& has_sensor =
      system_config()->HasSensor(SensorType::kLidGravitySensor);
  ASSERT_TRUE(has_sensor.has_value());
  EXPECT_FALSE(has_sensor.value());
}

TEST_F(SystemConfigTest, TestLidGravitySensorUnset) {
  EXPECT_FALSE(
      system_config()->HasSensor(SensorType::kLidGravitySensor).has_value());
}

}  // namespace
}  // namespace diagnostics
