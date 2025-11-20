// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/thermal/ec_fan_reader.h"

#include <memory>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/file_utils.h>
#include <chromeos/ec/ec_commands.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/mock_ec_command_factory.h>
#include <libec/mock_ec_command_version_supported.h>

#include "power_manager/powerd/testing/test_environment.h"

using ::testing::IsEmpty;
using ::testing::Return;

namespace power_manager::system {

namespace {
using ::testing::StrictMock;

class FakeGetFeaturesCommand : public ec::GetFeaturesCommand {
 public:
  FakeGetFeaturesCommand() = default;

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }
  const struct ec_response_get_features* Resp() const override {
    return &fake_response_;
  }

  void SetRunResult(bool result) { fake_run_result_ = result; }

  void SetFeatureSupported(enum ec_feature_code code) {
    if (code < 32) {
      fake_response_.flags[0] |= EC_FEATURE_MASK_0(code);
    } else {
      fake_response_.flags[1] |= EC_FEATURE_MASK_1(code);
    }
  }

  void SetFeatureUnsupported(enum ec_feature_code code) {
    if (code < 32) {
      fake_response_.flags[0] &= ~EC_FEATURE_MASK_0(code);
    } else {
      fake_response_.flags[1] &= ~EC_FEATURE_MASK_1(code);
    }
  }

 private:
  bool fake_run_result_ = false;
  struct ec_response_get_features fake_response_ = {.flags[0] = 0,
                                                    .flags[1] = 0};
};

class FakePwmGetFanTargetRpmCommand : public ec::PwmGetFanTargetRpmCommand {
 public:
  FakePwmGetFanTargetRpmCommand()
      : ec::PwmGetFanTargetRpmCommand(/*fan_idx=*/0) {}

  // ec::EcCommand overrides.
  const uint16_t* Resp() const override { return &fake_response_; }

  // ec::ReadMemmapCommand overrides.
  int IoctlReadmem(int fd,
                   uint32_t request,
                   cros_ec_readmem_v2* data) override {
    return -1;
  }
  bool EcCommandRun(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

  void SetRpm(uint16_t rpm) { fake_response_ = rpm; }

 private:
  bool fake_run_result_ = false;
  uint16_t fake_response_ = 0;
};

}  // namespace

class EcFanReaderTest : public TestEnvironment {
 public:
  EcFanReaderTest() = default;
  EcFanReaderTest(const EcFanReaderTest&) = delete;
  EcFanReaderTest& operator=(const EcFanReaderTest&) = delete;

  ~EcFanReaderTest() override = default;

 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    ASSERT_TRUE(temp_dir_.IsValid());
    ec_fan_reader_ = std::make_unique<EcFanReader>();
    cros_ec_path_ = temp_dir_.GetPath().Append("cros_ec");
    EXPECT_TRUE(base::WriteFile(cros_ec_path_, ""));
  }
  void Init() {
    ec_fan_reader_->Init(cros_ec_path_, &mock_ec_command_factory_);
  }

  void TearDown() override {}

  StrictMock<ec::MockEcCommandFactory> mock_ec_command_factory_;

  std::unique_ptr<EcFanReader> ec_fan_reader_;
  base::ScopedTempDir temp_dir_;
  base::FilePath cros_ec_path_;
};

TEST_F(EcFanReaderTest, GetCurrentHighestFanSpeedOneFan) {
  Init();
  auto features_cmd = std::make_unique<FakeGetFeaturesCommand>();
  features_cmd->SetRunResult(true);
  features_cmd->SetFeatureSupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(features_cmd)));

  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(0))
      .WillOnce([]() {
        auto get_fan_rpm_cmd =
            std::make_unique<FakePwmGetFanTargetRpmCommand>();
        get_fan_rpm_cmd->SetRunResult(true);
        get_fan_rpm_cmd->SetRpm(2000);
        return get_fan_rpm_cmd;
      });

  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(1))
      .WillOnce([]() {
        auto get_fan_rpm_cmd =
            std::make_unique<FakePwmGetFanTargetRpmCommand>();
        get_fan_rpm_cmd->SetRunResult(true);
        get_fan_rpm_cmd->SetRpm(EC_FAN_SPEED_NOT_PRESENT);
        return get_fan_rpm_cmd;
      });

  auto fan_rpm = ec_fan_reader_->GetCurrentHighestFanSpeed();
  EXPECT_EQ(fan_rpm, 2000);
}

TEST_F(EcFanReaderTest, GetCurrentHighestFanSpeedMultipleFans) {
  Init();
  auto features_cmd = std::make_unique<FakeGetFeaturesCommand>();
  features_cmd->SetRunResult(true);
  features_cmd->SetFeatureSupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(features_cmd)));

  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(0))
      .WillOnce([]() {
        auto get_fan_rpm_cmd =
            std::make_unique<FakePwmGetFanTargetRpmCommand>();
        get_fan_rpm_cmd->SetRunResult(true);
        get_fan_rpm_cmd->SetRpm(2000);
        return get_fan_rpm_cmd;
      });

  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(1))
      .WillOnce([]() {
        auto get_fan_rpm_cmd =
            std::make_unique<FakePwmGetFanTargetRpmCommand>();
        get_fan_rpm_cmd->SetRunResult(true);
        get_fan_rpm_cmd->SetRpm(3000);
        return get_fan_rpm_cmd;
      });

  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(2))
      .WillOnce([]() {
        auto get_fan_rpm_cmd =
            std::make_unique<FakePwmGetFanTargetRpmCommand>();
        get_fan_rpm_cmd->SetRunResult(true);
        get_fan_rpm_cmd->SetRpm(EC_FAN_SPEED_NOT_PRESENT);
        return get_fan_rpm_cmd;
      });

  auto fan_rpm = ec_fan_reader_->GetCurrentHighestFanSpeed();
  EXPECT_EQ(fan_rpm, 3000);
}

TEST_F(EcFanReaderTest, GetCurrentHighestFanSpeedNoEc) {
  cros_ec_path_ = base::FilePath("/tmp/this_path_does_not_exist");
  Init();
  auto err = ec_fan_reader_->GetCurrentHighestFanSpeed();
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(EcFanReaderTest, GetCurrentHighestFanSpeedGetFeaturesCommandFailed) {
  Init();
  auto cmd = std::make_unique<FakeGetFeaturesCommand>();
  cmd->SetRunResult(false);

  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(cmd)));

  auto err = ec_fan_reader_->GetCurrentHighestFanSpeed();
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(EcFanReaderTest, GetCurrentHighestFanSpeedFanNotSupported) {
  Init();
  auto cmd = std::make_unique<FakeGetFeaturesCommand>();
  cmd->SetRunResult(true);
  cmd->SetFeatureUnsupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(cmd)));

  auto err = ec_fan_reader_->GetCurrentHighestFanSpeed();
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(EcFanReaderTest, GetCurrentHighestFanSpeedNoFan) {
  Init();
  auto features_cmd = std::make_unique<FakeGetFeaturesCommand>();
  features_cmd->SetRunResult(true);
  features_cmd->SetFeatureSupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(features_cmd)));

  auto get_fan_rpm_cmd = std::make_unique<FakePwmGetFanTargetRpmCommand>();
  get_fan_rpm_cmd->SetRunResult(true);
  get_fan_rpm_cmd->SetRpm(EC_FAN_SPEED_NOT_PRESENT);
  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(0))
      .WillOnce(Return(std::move(get_fan_rpm_cmd)));

  auto fan_rpm = ec_fan_reader_->GetCurrentHighestFanSpeed();
  EXPECT_EQ(fan_rpm, 0);
}

TEST_F(EcFanReaderTest, GetCurrentHighestFanSpeedFailedRead) {
  Init();
  auto features_cmd = std::make_unique<FakeGetFeaturesCommand>();
  features_cmd->SetRunResult(true);
  features_cmd->SetFeatureSupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(features_cmd)));

  auto get_fan_rpm_cmd = std::make_unique<FakePwmGetFanTargetRpmCommand>();
  get_fan_rpm_cmd->SetRunResult(false);
  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(0))
      .WillOnce(Return(std::move(get_fan_rpm_cmd)));

  auto err = ec_fan_reader_->GetCurrentHighestFanSpeed();
  EXPECT_EQ(err, std::nullopt);
}

}  // namespace power_manager::system
