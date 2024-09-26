// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/fetchers/thermal_fetcher.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/ec_command.h>
#include <libec/mock_ec_command_factory.h>
#include <libec/thermal/get_memmap_temp_b_command.h>
#include <libec/thermal/get_memmap_temp_command.h>
#include <libec/thermal/get_memmap_thermal_version_command.h>
#include <libec/thermal/temp_sensor_get_info_command.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::DoubleEq;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::StrEq;

class FakeGetMemmapThermalVersionCommand
    : public ec::GetMemmapThermalVersionCommand {
 public:
  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }
  const uint8_t* Resp() const override { return &fake_response_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }
  void SetThermalVersion(uint8_t value) { fake_response_ = value; }

 private:
  bool fake_run_result_ = false;
  uint8_t fake_response_ = 0;
};

class FakeGetMemmapTempCommand : public ec::GetMemmapTempCommand {
 public:
  FakeGetMemmapTempCommand() : ec::GetMemmapTempCommand(/*id=*/0) {}

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }
  const uint8_t* Resp() const override { return &fake_response_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }
  void SetTemp(uint8_t value) { fake_response_ = value; }

 private:
  bool fake_run_result_ = false;
  uint8_t fake_response_ = 0;
};

class FakeGetMemmapTempBCommand : public ec::GetMemmapTempBCommand {
 public:
  FakeGetMemmapTempBCommand() : ec::GetMemmapTempBCommand(/*id=*/0) {}

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }
  const uint8_t* Resp() const override { return &fake_response_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }
  void SetTemp(uint8_t value) { fake_response_ = value; }

 private:
  bool fake_run_result_ = false;
  uint8_t fake_response_ = 0;
};

class FakeTempSensorGetInfoCommand : public ec::TempSensorGetInfoCommand {
 public:
  FakeTempSensorGetInfoCommand() : ec::TempSensorGetInfoCommand(/*id=*/0) {
    std::fill_n(fake_response_.sensor_name, sizeof(fake_response_.sensor_name),
                0);
  }

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }
  const ec_response_temp_sensor_get_info* Resp() const override {
    return &fake_response_;
  }

  void SetRunResult(bool result) { fake_run_result_ = result; }
  void SetName(std::string_view value) {
    CHECK_LT(value.length(), sizeof(fake_response_.sensor_name));
    value.copy(fake_response_.sensor_name, value.length());
  }

 private:
  bool fake_run_result_ = false;
  struct ec_response_temp_sensor_get_info fake_response_;
};

std::unique_ptr<ec::GetMemmapThermalVersionCommand>
CreateSuccessfulGetMemmapThermalVersionCommand(uint8_t version) {
  auto cmd = std::make_unique<FakeGetMemmapThermalVersionCommand>();
  cmd->SetRunResult(true);
  cmd->SetThermalVersion(version);
  return cmd;
}

std::unique_ptr<ec::GetMemmapTempCommand> CreateSuccessfulGetMemmapTempCommand(
    uint8_t temp) {
  auto cmd = std::make_unique<FakeGetMemmapTempCommand>();
  cmd->SetRunResult(true);
  cmd->SetTemp(temp);
  return cmd;
}

std::unique_ptr<ec::GetMemmapTempBCommand>
CreateSuccessfulGetMemmapTempBCommand(uint8_t temp) {
  auto cmd = std::make_unique<FakeGetMemmapTempBCommand>();
  cmd->SetRunResult(true);
  cmd->SetTemp(temp);
  return cmd;
}

std::unique_ptr<ec::TempSensorGetInfoCommand>
CreateSuccessfulTempSensorGetInfoCommand(std::string_view name) {
  auto cmd = std::make_unique<FakeTempSensorGetInfoCommand>();
  cmd->SetRunResult(true);
  cmd->SetName(name);
  return cmd;
}

// Use `ThermalFetcherDelegateTest` rather than `ThermalFetcherTest` to avoid
// fixture name collision.
class ThermalFetcherDelegateTest : public BaseFileTest {
 public:
  ThermalFetcherDelegateTest(const ThermalFetcherDelegateTest&) = delete;
  ThermalFetcherDelegateTest& operator=(const ThermalFetcherDelegateTest&) =
      delete;

 protected:
  ThermalFetcherDelegateTest() = default;

  void SetUp() override {
    SetFile(ec::kCrosEcPath, "");

    ON_CALL(mock_ec_command_factory_, GetMemmapThermalVersionCommand())
        .WillByDefault(
            []() { return CreateSuccessfulGetMemmapThermalVersionCommand(1); });
  }

  ec::MockEcCommandFactory mock_ec_command_factory_;
};

TEST_F(ThermalFetcherDelegateTest, ErrorIfFailedToGetThermalVersion) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapThermalVersionCommand())
      .WillOnce([]() {
        auto cmd = std::make_unique<FakeGetMemmapThermalVersionCommand>();
        cmd->SetRunResult(false);
        return cmd;
      });

  EXPECT_EQ(FetchEcThermalSensors(&mock_ec_command_factory_), std::nullopt);
}

TEST_F(ThermalFetcherDelegateTest, Success) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(0))
      .WillOnce(Return(CreateSuccessfulGetMemmapTempCommand(100)));
  EXPECT_CALL(mock_ec_command_factory_, TempSensorGetInfoCommand(0))
      .WillOnce(
          Return(CreateSuccessfulTempSensorGetInfoCommand("fake name 1")));
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(1))
      .WillOnce(Return(CreateSuccessfulGetMemmapTempCommand(120)));
  EXPECT_CALL(mock_ec_command_factory_, TempSensorGetInfoCommand(1))
      .WillOnce(
          Return(CreateSuccessfulTempSensorGetInfoCommand("fake name 2")));
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(2))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_NOT_PRESENT)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  ASSERT_THAT(res.value(), SizeIs(2));
  const auto& info_0 = res.value()[0];
  ASSERT_TRUE(info_0);
  EXPECT_THAT(info_0->name, StrEq("fake name 1"));
  EXPECT_EQ(info_0->source, mojom::ThermalSensorInfo::ThermalSensorSource::kEc);
  EXPECT_THAT(info_0->temperature_celsius, DoubleEq(300 - 273.15));
  const auto& info_1 = res.value()[1];
  ASSERT_TRUE(info_1);
  EXPECT_THAT(info_1->name, StrEq("fake name 2"));
  EXPECT_EQ(info_1->source, mojom::ThermalSensorInfo::ThermalSensorSource::kEc);
  EXPECT_THAT(info_1->temperature_celsius, DoubleEq(320 - 273.15));
}

TEST_F(ThermalFetcherDelegateTest, IgnoreFailedToReadTempSensor) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(0)).WillOnce([]() {
    auto cmd = std::make_unique<FakeGetMemmapTempCommand>();
    cmd->SetRunResult(false);
    return cmd;
  });
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(1))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_NOT_PRESENT)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(res.value(), IsEmpty());
}

TEST_F(ThermalFetcherDelegateTest, IgnoreErrorSensor) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(0))
      .WillOnce(
          Return(CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_ERROR)));
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(1))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_NOT_PRESENT)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(res.value(), IsEmpty());
}

TEST_F(ThermalFetcherDelegateTest, IgnoreNotPoweredSensor) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(0))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_NOT_POWERED)));
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(1))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_NOT_PRESENT)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(res.value(), IsEmpty());
}

TEST_F(ThermalFetcherDelegateTest, IgnoreNotCalibratedSensor) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(0))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_NOT_CALIBRATED)));
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(1))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_NOT_PRESENT)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(res.value(), IsEmpty());
}

TEST_F(ThermalFetcherDelegateTest, IgnoreFailedToGetInfoSensor) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(0))
      .WillOnce(Return(CreateSuccessfulGetMemmapTempCommand(100)));
  EXPECT_CALL(mock_ec_command_factory_, TempSensorGetInfoCommand(0))
      .WillOnce([]() {
        auto cmd = std::make_unique<FakeTempSensorGetInfoCommand>();
        cmd->SetRunResult(false);
        return cmd;
      });
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(1))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempCommand(EC_TEMP_SENSOR_NOT_PRESENT)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(res.value(), IsEmpty());
}

TEST_F(ThermalFetcherDelegateTest, TreatZeroTempOffsetAsNotPresent) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(0))
      .WillOnce(Return(CreateSuccessfulGetMemmapTempCommand(0)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(res.value(), IsEmpty());
}

TEST_F(ThermalFetcherDelegateTest, IgnoreSecondBankWhenVersionIsOld) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapThermalVersionCommand())
      .WillOnce(Return(CreateSuccessfulGetMemmapThermalVersionCommand(1)));

  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(_))
      .WillRepeatedly(
          []() { return CreateSuccessfulGetMemmapTempCommand(100); });
  EXPECT_CALL(mock_ec_command_factory_, TempSensorGetInfoCommand(_))
      .WillRepeatedly(
          []() { return CreateSuccessfulTempSensorGetInfoCommand(""); });

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(res.value(), SizeIs(EC_TEMP_SENSOR_ENTRIES));
}

TEST_F(ThermalFetcherDelegateTest, ReadSecondBank) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapThermalVersionCommand())
      .WillOnce(Return(CreateSuccessfulGetMemmapThermalVersionCommand(2)));

  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(_))
      .WillRepeatedly(
          []() { return CreateSuccessfulGetMemmapTempCommand(100); });
  EXPECT_CALL(mock_ec_command_factory_, TempSensorGetInfoCommand(_))
      .WillRepeatedly(
          []() { return CreateSuccessfulTempSensorGetInfoCommand(""); });
  EXPECT_CALL(mock_ec_command_factory_,
              GetMemmapTempBCommand(EC_TEMP_SENSOR_ENTRIES))
      .WillOnce(Return(CreateSuccessfulGetMemmapTempBCommand(120)));
  EXPECT_CALL(mock_ec_command_factory_,
              TempSensorGetInfoCommand(EC_TEMP_SENSOR_ENTRIES))
      .WillOnce(
          Return(CreateSuccessfulTempSensorGetInfoCommand("fake name B")));
  EXPECT_CALL(mock_ec_command_factory_,
              GetMemmapTempBCommand(EC_TEMP_SENSOR_ENTRIES + 1))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempBCommand(EC_TEMP_SENSOR_NOT_PRESENT)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  ASSERT_THAT(res.value(), SizeIs(EC_TEMP_SENSOR_ENTRIES + 1));
  const auto& info_b = res.value()[EC_TEMP_SENSOR_ENTRIES];
  ASSERT_TRUE(info_b);
  EXPECT_THAT(info_b->name, StrEq("fake name B"));
  EXPECT_EQ(info_b->source, mojom::ThermalSensorInfo::ThermalSensorSource::kEc);
  EXPECT_THAT(info_b->temperature_celsius, DoubleEq(320 - 273.15));
}

TEST_F(ThermalFetcherDelegateTest, IgnoreFailedToReadTempSensorInSecondBank) {
  EXPECT_CALL(mock_ec_command_factory_, GetMemmapThermalVersionCommand())
      .WillOnce(Return(CreateSuccessfulGetMemmapThermalVersionCommand(2)));

  EXPECT_CALL(mock_ec_command_factory_, GetMemmapTempCommand(_))
      .WillRepeatedly(
          []() { return CreateSuccessfulGetMemmapTempCommand(100); });
  EXPECT_CALL(mock_ec_command_factory_, TempSensorGetInfoCommand(_))
      .WillRepeatedly(
          []() { return CreateSuccessfulTempSensorGetInfoCommand(""); });
  EXPECT_CALL(mock_ec_command_factory_,
              GetMemmapTempBCommand(EC_TEMP_SENSOR_ENTRIES))
      .WillOnce([]() {
        auto cmd = std::make_unique<FakeGetMemmapTempBCommand>();
        cmd->SetRunResult(false);
        return cmd;
      });
  EXPECT_CALL(mock_ec_command_factory_,
              GetMemmapTempBCommand(EC_TEMP_SENSOR_ENTRIES + 1))
      .WillOnce(Return(
          CreateSuccessfulGetMemmapTempBCommand(EC_TEMP_SENSOR_NOT_PRESENT)));

  auto res = FetchEcThermalSensors(&mock_ec_command_factory_);
  ASSERT_TRUE(res.has_value());
  EXPECT_THAT(res.value(), SizeIs(EC_TEMP_SENSOR_ENTRIES));
}

}  // namespace
}  // namespace diagnostics
