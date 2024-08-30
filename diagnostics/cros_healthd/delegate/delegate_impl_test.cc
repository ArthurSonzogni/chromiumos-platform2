// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/delegate_impl.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/time/time.h>
#include <chromeos/ec/ec_commands.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/fingerprint/fp_mode_command.h>
#include <libec/led_control_command.h>
#include <libec/mkbp_event.h>
#include <libec/mock_ec_command_factory.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/delegate/routines/prime_number_search_delegate.h"
#include "diagnostics/cros_healthd/delegate/utils/mock_display_util_factory.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;
using ::testing::WithArg;

namespace mojom = ::ash::cros_healthd::mojom;

inline constexpr mojom::LedName kArbitraryValidLedName =
    mojom::LedName::kBattery;
inline constexpr mojom::LedColor kArbitraryValidLedColor =
    mojom::LedColor::kAmber;
// The ec_led_colors corresponding to |kArbitraryValidLedColor|.
inline constexpr ec_led_colors kArbitraryValidLedColorEcEnum =
    EC_LED_COLOR_AMBER;

// Parameters for running i2c read command.
constexpr uint8_t kBatteryI2cAddress = 0x16;
constexpr uint8_t kBatteryI2cManufactureDateOffset = 0x1B;
constexpr uint8_t kBatteryI2cTemperatureOffset = 0x08;
constexpr uint8_t kBatteryI2cReadLen = 2;

class FakeFpInfoCommand : public ec::FpInfoCommand {
 public:
  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }
  struct ec_response_fp_info* Resp() override { return &fake_response_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

  void SetSensorImageSize(uint16_t width, uint16_t height) {
    fake_response_.width = width;
    fake_response_.height = height;
  }

 private:
  struct ec_response_fp_info fake_response_ = {
      .frame_size = 0,
      .pixel_format = 0,
      .width = 0,
      .height = 0,
      .bpp = 0,
  };
  // If `fake_run_result_` is declared before `fake_response_`, there will be
  // "runtime error: reference binding to misaligned address" errors in ubsan.
  bool fake_run_result_ = false;
};

class FakeMkbpEvent : public ec::MkbpEvent {
 public:
  FakeMkbpEvent() : ec::MkbpEvent(0, EC_MKBP_EVENT_FINGERPRINT) {}
  ~FakeMkbpEvent() = default;

  // ec::MkbpEvent overrides.
  int Enable() override { return fake_enable_result_; }
  int Wait(int timeout) override { return fake_wait_result_; }

  void SetEnableResult(int result) { fake_enable_result_ = result; }
  void SetWaitResult(int result) { fake_wait_result_ = result; }

 private:
  int fake_enable_result_ = 0;
  int fake_wait_result_ = 0;
};

class FakeFpModeCommand : public ec::FpModeCommand {
 public:
  FakeFpModeCommand()
      : ec::FpModeCommand(ec::FpMode(ec::FpMode::Mode::kCapture)) {}
  ~FakeFpModeCommand() = default;

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

 private:
  bool fake_run_result_ = false;
};

class FakeGetProtocolInfoCommand : public ec::GetProtocolInfoCommand {
 public:
  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

 private:
  bool fake_run_result_ = false;
};

class FakeFpFrameCommand : public ec::FpFrameCommand {
 public:
  explicit FakeFpFrameCommand(uint32_t frame_size)
      : ec::FpFrameCommand(FP_FRAME_INDEX_RAW_IMAGE, frame_size, frame_size) {}
  ~FakeFpFrameCommand() = default;

  // ec::EcCommand overrides.
  ec::FpFramePacket* Resp() override { return &fake_response_; }

  // ec::FpFrameCommand overrides.
  bool EcCommandRun(int fd) override { return fake_run_result_; }
  void Sleep(base::TimeDelta duration) override {
    // No-op.
  }

  void SetRunResult(bool result) { fake_run_result_ = result; }

  void SetFrame(const std::vector<uint8_t>& frame) {
    CHECK(frame.size() <= fake_response_.size());
    std::copy(frame.begin(), frame.end(), fake_response_.begin());
  }

 private:
  bool fake_run_result_ = false;
  ec::FpFramePacket fake_response_{0};
};

class FakeGetVersionCommand : public ec::GetVersionCommand {
 public:
  FakeGetVersionCommand() = default;

  // ec::EcCommand overrides.
  struct ec_response_get_version* Resp() override { return &fake_response_; }

  // ec::GetVersionCommand overrides.
  bool EcCommandRun(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

  void SetImage(ec_image image) { fake_response_.current_image = image; }

 private:
  bool fake_run_result_ = false;
  struct ec_response_get_version fake_response_ = {
      .version_string_ro = "",
      .version_string_rw = "",
      .reserved = "",
      .current_image = ec_image::EC_IMAGE_UNKNOWN};
};

class FakeLedControlAutoCommand : public ec::LedControlAutoCommand {
 public:
  FakeLedControlAutoCommand()
      : ec::LedControlAutoCommand(/*led_id=*/EC_LED_ID_BATTERY_LED) {}

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

 private:
  bool fake_run_result_ = false;
};

class FakeLedControlQueryCommand : public ec::LedControlQueryCommand {
 public:
  FakeLedControlQueryCommand()
      : ec::LedControlQueryCommand(/*led_id=*/EC_LED_ID_BATTERY_LED) {}

  // ec::EcCommand overrides.
  struct ec_response_led_control* Resp() override { return &fake_response_; }

  // ec::LedControlQueryCommand overrides.
  bool EcCommandRun(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }
  void SetBrightness(ec_led_colors color, uint8_t value) {
    fake_response_.brightness_range[color] = value;
  }

 private:
  bool fake_run_result_ = false;
  struct ec_response_led_control fake_response_ = {.brightness_range = {}};
};

class FakeLedControlSetCommand : public ec::LedControlSetCommand {
 public:
  FakeLedControlSetCommand()
      : ec::LedControlSetCommand(/*led_id=*/EC_LED_ID_BATTERY_LED,
                                 /*brightness=*/{}) {}

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

 private:
  bool fake_run_result_ = false;
};

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

class FakeI2cReadCommand : public ec::I2cReadCommand {
 public:
  FakeI2cReadCommand() = default;

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }

  // ec::I2cReadCommand overrides.
  uint32_t Data() const override { return fake_data_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

  void SetData(uint32_t data) { fake_data_ = data; }

 private:
  bool fake_run_result_ = false;
  uint32_t fake_data_ = 0;
};

class FakeMotionSenseCommandLidAngle : public ec::MotionSenseCommandLidAngle {
 public:
  FakeMotionSenseCommandLidAngle() = default;

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }
  uint32_t Result() const override { return fake_result_; }

  // ec::MotionSenseCommandLidAngle overrides.
  uint16_t LidAngle() const override { return fake_lid_angle_; }

  void SetRunResult(bool run_result) { fake_run_result_ = run_result; }
  void SetResult(uint32_t result) { fake_result_ = result; }
  void SetLidAngle(uint32_t lid_angle) { fake_lid_angle_ = lid_angle; }

 private:
  bool fake_run_result_ = false;
  uint32_t fake_result_ = 0;
  uint16_t fake_lid_angle_ = 0;
};

class MockPrimeNumberSearchDelegate : public PrimeNumberSearchDelegate {
 public:
  MOCK_METHOD(bool, Run, (), (override));
};

class MockDelegateImpl : public DelegateImpl {
 public:
  MockDelegateImpl(ec::EcCommandFactoryInterface* ec_command_factory,
                   DisplayUtilFactory* display_util_factory)
      : DelegateImpl(ec_command_factory, display_util_factory) {}
  MockDelegateImpl(const MockDelegateImpl&) = delete;
  MockDelegateImpl& operator=(const MockDelegateImpl&) = delete;
  ~MockDelegateImpl() = default;

  MOCK_METHOD(std::unique_ptr<ec::MkbpEvent>,
              CreateMkbpEvent,
              (int fd, enum ec_mkbp_event event_type),
              (override));

  MOCK_METHOD(std::unique_ptr<PrimeNumberSearchDelegate>,
              CreatePrimeNumberSearchDelegate,
              (uint64_t max_num),
              (override));
};

class DelegateImplTest : public BaseFileTest {
 public:
  DelegateImplTest(const DelegateImplTest&) = delete;
  DelegateImplTest& operator=(const DelegateImplTest&) = delete;

 protected:
  DelegateImplTest() = default;

  void SetUp() override { SetFile(ec::kCrosEcPath, ""); }

  std::pair<mojom::FingerprintFrameResultPtr, std::optional<std::string>>
  GetFingerprintFrameSync(mojom::FingerprintCaptureType type) {
    base::test::TestFuture<mojom::FingerprintFrameResultPtr,
                           const std::optional<std::string>&>
        future;
    delegate_.GetFingerprintFrame(type, future.GetCallback());
    return future.Take();
  }

  std::pair<mojom::FingerprintInfoResultPtr, std::optional<std::string>>
  GetFingerprintInfoSync() {
    base::test::TestFuture<mojom::FingerprintInfoResultPtr,
                           const std::optional<std::string>&>
        future;
    delegate_.GetFingerprintInfo(future.GetCallback());
    return future.Take();
  }

  std::optional<std::string> SetLedColorSync(mojom::LedName name,
                                             mojom::LedColor color) {
    base::test::TestFuture<const std::optional<std::string>&> err_future;
    delegate_.SetLedColor(name, color, err_future.GetCallback());
    return err_future.Take();
  }

  std::optional<std::string> ResetLedColorSync(mojom::LedName name) {
    base::test::TestFuture<const std::optional<std::string>&> err_future;
    delegate_.ResetLedColor(name, err_future.GetCallback());
    return err_future.Take();
  }

  std::pair<std::vector<uint16_t>, std::optional<std::string>>
  GetAllFanSpeedSync() {
    base::test::TestFuture<const std::vector<uint16_t>&,
                           const std::optional<std::string>&>
        future;
    delegate_.GetAllFanSpeed(future.GetCallback());
    return future.Get();
  }

  std::optional<uint32_t> GetSmartBatteryManufactureDateSync(uint8_t i2c_port) {
    base::test::TestFuture<std::optional<uint32_t>> future;
    delegate_.GetSmartBatteryManufactureDate(i2c_port, future.GetCallback());
    return future.Get();
  }

  std::optional<uint32_t> GetSmartBatteryTemperatureSync(uint8_t i2c_port) {
    base::test::TestFuture<std::optional<uint32_t>> future;
    delegate_.GetSmartBatteryTemperature(i2c_port, future.GetCallback());
    return future.Get();
  }

  std::optional<uint16_t> GetLidAngleSync() {
    base::test::TestFuture<std::optional<uint16_t>> future;
    delegate_.GetLidAngle(future.GetCallback());
    return future.Get();
  }

  bool RunPrimeSearchSync(base::TimeDelta exec_duration, uint64_t max_num) {
    base::test::TestFuture<bool> future;
    delegate_.RunPrimeSearch(exec_duration, max_num, future.GetCallback());
    return future.Get();
  }

  void FastForwardBy(base::TimeDelta time) {
    task_environment_.FastForwardBy(time);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  StrictMock<ec::MockEcCommandFactory> mock_ec_command_factory_;
  MockDisplayUtilFactory mock_display_util_factory_;
  MockDelegateImpl delegate_{&mock_ec_command_factory_,
                             &mock_display_util_factory_};
};

TEST_F(DelegateImplTest, GetFingerprintFrameFpInfoCommandFailed) {
  auto cmd = std::make_unique<FakeFpInfoCommand>();
  cmd->SetRunResult(false);

  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(cmd)));

  auto [unused, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, "Failed to run ec::FpInfoCommand");
}

TEST_F(DelegateImplTest, GetFingerprintFrameMkbpEventEnableFailed) {
  auto fp_info_cmd = std::make_unique<FakeFpInfoCommand>();
  fp_info_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(fp_info_cmd)));

  auto mkbp_event = std::make_unique<FakeMkbpEvent>();
  mkbp_event->SetEnableResult(1);
  EXPECT_CALL(delegate_, CreateMkbpEvent(_, _))
      .WillOnce(Return(std::move(mkbp_event)));

  auto [unused, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, "Failed to enable fingerprint event");
}

TEST_F(DelegateImplTest, GetFingerprintFrameFpModeCommandFailed) {
  auto fp_info_cmd = std::make_unique<FakeFpInfoCommand>();
  fp_info_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(fp_info_cmd)));

  auto mkbp_event = std::make_unique<FakeMkbpEvent>();
  mkbp_event->SetEnableResult(0);
  EXPECT_CALL(delegate_, CreateMkbpEvent(_, _))
      .WillOnce(Return(std::move(mkbp_event)));

  auto fp_mode_cmd = std::make_unique<FakeFpModeCommand>();
  fp_mode_cmd->SetRunResult(false);
  EXPECT_CALL(mock_ec_command_factory_, FpModeCommand(_))
      .WillOnce(Return(std::move(fp_mode_cmd)));

  auto [unused, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, "Failed to set capture mode");
}

TEST_F(DelegateImplTest, GetFingerprintFrameMkbpEventWaitFailed) {
  auto fp_info_cmd = std::make_unique<FakeFpInfoCommand>();
  fp_info_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(fp_info_cmd)));

  auto mkbp_event = std::make_unique<FakeMkbpEvent>();
  mkbp_event->SetEnableResult(0);
  mkbp_event->SetWaitResult(0);
  EXPECT_CALL(delegate_, CreateMkbpEvent(_, _))
      .WillOnce(Return(std::move(mkbp_event)));

  auto fp_mode_cmd = std::make_unique<FakeFpModeCommand>();
  fp_mode_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpModeCommand(_))
      .WillOnce(Return(std::move(fp_mode_cmd)));

  auto [unused, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, "Failed to poll fingerprint event after 5 seconds");
}

TEST_F(DelegateImplTest, GetFingerprintFrameGetProtocolInfoCommandFailed) {
  auto fp_info_cmd = std::make_unique<FakeFpInfoCommand>();
  fp_info_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(fp_info_cmd)));

  auto mkbp_event = std::make_unique<FakeMkbpEvent>();
  mkbp_event->SetEnableResult(0);
  mkbp_event->SetWaitResult(1);
  EXPECT_CALL(delegate_, CreateMkbpEvent(_, _))
      .WillOnce(Return(std::move(mkbp_event)));

  auto fp_mode_cmd = std::make_unique<FakeFpModeCommand>();
  fp_mode_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpModeCommand(_))
      .WillOnce(Return(std::move(fp_mode_cmd)));

  auto protocol_cmd = std::make_unique<FakeGetProtocolInfoCommand>();
  protocol_cmd->SetRunResult(false);
  EXPECT_CALL(mock_ec_command_factory_, GetProtocolInfoCommand())
      .WillOnce(Return(std::move(protocol_cmd)));

  auto [unused, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, "Failed to get EC protocol info");
}

TEST_F(DelegateImplTest, GetFingerprintFrameFrameSizeZero) {
  auto fp_info_cmd = std::make_unique<FakeFpInfoCommand>();
  fp_info_cmd->SetRunResult(true);
  fp_info_cmd->SetSensorImageSize(/*width=*/0, /*height=*/0);
  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(fp_info_cmd)));

  auto mkbp_event = std::make_unique<FakeMkbpEvent>();
  mkbp_event->SetEnableResult(0);
  mkbp_event->SetWaitResult(1);
  EXPECT_CALL(delegate_, CreateMkbpEvent(_, _))
      .WillOnce(Return(std::move(mkbp_event)));

  auto fp_mode_cmd = std::make_unique<FakeFpModeCommand>();
  fp_mode_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpModeCommand(_))
      .WillOnce(Return(std::move(fp_mode_cmd)));

  auto protocol_cmd = std::make_unique<FakeGetProtocolInfoCommand>();
  protocol_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, GetProtocolInfoCommand())
      .WillOnce(Return(std::move(protocol_cmd)));

  auto [unused, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, "Frame size is zero");
}

TEST_F(DelegateImplTest, GetFingerprintFrameFpFrameCommandFailed) {
  auto fp_info_cmd = std::make_unique<FakeFpInfoCommand>();
  fp_info_cmd->SetRunResult(true);
  fp_info_cmd->SetSensorImageSize(/*width=*/2, /*height=*/3);
  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(fp_info_cmd)));

  auto mkbp_event = std::make_unique<FakeMkbpEvent>();
  mkbp_event->SetEnableResult(0);
  mkbp_event->SetWaitResult(1);
  EXPECT_CALL(delegate_, CreateMkbpEvent(_, _))
      .WillOnce(Return(std::move(mkbp_event)));

  auto fp_mode_cmd = std::make_unique<FakeFpModeCommand>();
  fp_mode_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpModeCommand(_))
      .WillOnce(Return(std::move(fp_mode_cmd)));

  auto protocol_cmd = std::make_unique<FakeGetProtocolInfoCommand>();
  protocol_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, GetProtocolInfoCommand())
      .WillOnce(Return(std::move(protocol_cmd)));

  auto fp_frame_cmd = std::make_unique<FakeFpFrameCommand>(6);
  fp_frame_cmd->SetRunResult(false);
  EXPECT_CALL(mock_ec_command_factory_, FpFrameCommand(_, _, _))
      .WillOnce(Return(std::move(fp_frame_cmd)));

  auto [unused, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, "Failed to get fingerprint frame");
}

TEST_F(DelegateImplTest, GetFingerprintFrameFrameSizeMismatched) {
  auto fp_info_cmd = std::make_unique<FakeFpInfoCommand>();
  fp_info_cmd->SetRunResult(true);
  fp_info_cmd->SetSensorImageSize(/*width=*/2, /*height=*/3);
  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(fp_info_cmd)));

  auto mkbp_event = std::make_unique<FakeMkbpEvent>();
  mkbp_event->SetEnableResult(0);
  mkbp_event->SetWaitResult(1);
  EXPECT_CALL(delegate_, CreateMkbpEvent(_, _))
      .WillOnce(Return(std::move(mkbp_event)));

  auto fp_mode_cmd = std::make_unique<FakeFpModeCommand>();
  fp_mode_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpModeCommand(_))
      .WillOnce(Return(std::move(fp_mode_cmd)));

  auto protocol_cmd = std::make_unique<FakeGetProtocolInfoCommand>();
  protocol_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, GetProtocolInfoCommand())
      .WillOnce(Return(std::move(protocol_cmd)));

  auto fp_frame_cmd = std::make_unique<FakeFpFrameCommand>(5);
  fp_frame_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpFrameCommand(_, _, _))
      .WillOnce(Return(std::move(fp_frame_cmd)));

  auto [unused, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, "Frame size is not equal to width * height");
}

TEST_F(DelegateImplTest, GetFingerprintFrameSuccess) {
  auto fp_info_cmd = std::make_unique<FakeFpInfoCommand>();
  fp_info_cmd->SetRunResult(true);
  fp_info_cmd->SetSensorImageSize(/*width=*/2, /*height=*/3);
  EXPECT_CALL(mock_ec_command_factory_, FpInfoCommand())
      .WillOnce(Return(std::move(fp_info_cmd)));

  auto mkbp_event = std::make_unique<FakeMkbpEvent>();
  mkbp_event->SetEnableResult(0);
  mkbp_event->SetWaitResult(1);
  EXPECT_CALL(delegate_, CreateMkbpEvent(_, _))
      .WillOnce(Return(std::move(mkbp_event)));

  auto fp_mode_cmd = std::make_unique<FakeFpModeCommand>();
  fp_mode_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, FpModeCommand(_))
      .WillOnce(Return(std::move(fp_mode_cmd)));

  auto protocol_cmd = std::make_unique<FakeGetProtocolInfoCommand>();
  protocol_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, GetProtocolInfoCommand())
      .WillOnce(Return(std::move(protocol_cmd)));

  std::vector<uint8_t> fake_frame = {0, 1, 2, 3, 4, 5};
  auto fp_frame_cmd = std::make_unique<FakeFpFrameCommand>(6);
  fp_frame_cmd->SetRunResult(true);
  fp_frame_cmd->SetFrame(fake_frame);
  EXPECT_CALL(mock_ec_command_factory_, FpFrameCommand(_, _, _))
      .WillOnce(Return(std::move(fp_frame_cmd)));

  auto [result, err] =
      GetFingerprintFrameSync(mojom::FingerprintCaptureType::kCheckerboardTest);
  EXPECT_EQ(err, std::nullopt);
  ASSERT_TRUE(result);
  EXPECT_EQ(result->width, 2);
  EXPECT_EQ(result->height, 3);
  EXPECT_EQ(result->frame, fake_frame);
}

TEST_F(DelegateImplTest, GetFingerprintInfoSuccessRoFw) {
  auto cmd = std::make_unique<FakeGetVersionCommand>();
  cmd->SetRunResult(true);
  cmd->SetImage(ec_image::EC_IMAGE_RO);

  EXPECT_CALL(mock_ec_command_factory_, GetVersionCommand())
      .WillOnce(Return(std::move(cmd)));

  auto [info, err] = GetFingerprintInfoSync();
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->rw_fw);
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(DelegateImplTest, GetFingerprintInfoSuccessRwFw) {
  auto cmd = std::make_unique<FakeGetVersionCommand>();
  cmd->SetRunResult(true);
  cmd->SetImage(ec_image::EC_IMAGE_RW);

  EXPECT_CALL(mock_ec_command_factory_, GetVersionCommand())
      .WillOnce(Return(std::move(cmd)));

  auto [info, err] = GetFingerprintInfoSync();
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->rw_fw);
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(DelegateImplTest, GetFingerprintInfoFailed) {
  auto cmd = std::make_unique<FakeGetVersionCommand>();
  cmd->SetRunResult(false);

  EXPECT_CALL(mock_ec_command_factory_, GetVersionCommand())
      .WillOnce(Return(std::move(cmd)));

  auto [unused_info, err] = GetFingerprintInfoSync();
  EXPECT_EQ(err, "Failed to get fingerprint version");
}

TEST_F(DelegateImplTest, SetLedColorErrorUnknownLedName) {
  auto err = SetLedColorSync(mojom::LedName::kUnmappedEnumField,
                             kArbitraryValidLedColor);
  EXPECT_EQ(err, "Unknown LED name");
}

TEST_F(DelegateImplTest, SetLedColorErrorUnknownLedColor) {
  auto err = SetLedColorSync(kArbitraryValidLedName,
                             mojom::LedColor::kUnmappedEnumField);
  EXPECT_EQ(err, "Unknown LED color");
}

TEST_F(DelegateImplTest, SetLedColorErrorEcQueryCommandFailed) {
  auto cmd = std::make_unique<FakeLedControlQueryCommand>();
  cmd->SetRunResult(false);

  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce(Return(std::move(cmd)));

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, "Failed to query the LED brightness range");
}

TEST_F(DelegateImplTest, SetLedColorErrorUnsupportedColor) {
  auto cmd = std::make_unique<FakeLedControlQueryCommand>();
  cmd->SetRunResult(true);
  cmd->SetBrightness(kArbitraryValidLedColorEcEnum, 0);

  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce(Return(std::move(cmd)));

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, "Unsupported color");
}

TEST_F(DelegateImplTest, SetLedColorErrorSetCommandFailed) {
  auto query_cmd = std::make_unique<FakeLedControlQueryCommand>();
  query_cmd->SetRunResult(true);
  query_cmd->SetBrightness(kArbitraryValidLedColorEcEnum, 1);
  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce(Return(std::move(query_cmd)));

  auto set_cmd = std::make_unique<FakeLedControlSetCommand>();
  set_cmd->SetRunResult(false);
  EXPECT_CALL(mock_ec_command_factory_, LedControlSetCommand(_, _))
      .WillOnce(Return(std::move(set_cmd)));

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, "Failed to set the LED color");
}

TEST_F(DelegateImplTest, SetLedColorSuccess) {
  auto query_cmd = std::make_unique<FakeLedControlQueryCommand>();
  query_cmd->SetRunResult(true);
  query_cmd->SetBrightness(kArbitraryValidLedColorEcEnum, 1);
  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce(Return(std::move(query_cmd)));

  auto set_cmd = std::make_unique<FakeLedControlSetCommand>();
  set_cmd->SetRunResult(true);
  EXPECT_CALL(mock_ec_command_factory_, LedControlSetCommand(_, _))
      .WillOnce(Return(std::move(set_cmd)));

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, std::nullopt);
}

// The EC command to set LED brightness should respect the brightness range.
TEST_F(DelegateImplTest, SetLedColorUsesMaxBrightness) {
  auto query_cmd = std::make_unique<FakeLedControlQueryCommand>();
  query_cmd->SetRunResult(true);
  query_cmd->SetBrightness(kArbitraryValidLedColorEcEnum, 64);
  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce(Return(std::move(query_cmd)));

  auto set_cmd = std::make_unique<FakeLedControlSetCommand>();
  set_cmd->SetRunResult(true);
  std::array<uint8_t, EC_LED_COLOR_COUNT> received_brightness = {};
  EXPECT_CALL(mock_ec_command_factory_, LedControlSetCommand(_, _))
      .WillOnce(
          DoAll(SaveArg<1>(&received_brightness), Return(std::move(set_cmd))));

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, std::nullopt);

  std::array<uint8_t, EC_LED_COLOR_COUNT> expected_brightness = {
      {[kArbitraryValidLedColorEcEnum] = 64}};
  EXPECT_EQ(received_brightness, expected_brightness);
}

TEST_F(DelegateImplTest, ResetLedColorErrorUnknownLedName) {
  auto err = ResetLedColorSync(mojom::LedName::kUnmappedEnumField);
  EXPECT_EQ(err, "Unknown LED name");
}

TEST_F(DelegateImplTest, ResetLedColorErrorEcCommandFailed) {
  auto cmd = std::make_unique<FakeLedControlAutoCommand>();
  cmd->SetRunResult(false);

  EXPECT_CALL(mock_ec_command_factory_, LedControlAutoCommand(_))
      .WillOnce(Return(std::move(cmd)));

  auto err = ResetLedColorSync(kArbitraryValidLedName);
  EXPECT_EQ(err, "Failed to reset LED color");
}

TEST_F(DelegateImplTest, ResetLedColorSuccess) {
  auto cmd = std::make_unique<FakeLedControlAutoCommand>();
  cmd->SetRunResult(true);

  EXPECT_CALL(mock_ec_command_factory_, LedControlAutoCommand(_))
      .WillOnce(Return(std::move(cmd)));

  auto err = ResetLedColorSync(kArbitraryValidLedName);
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(DelegateImplTest, GetAllFanSpeedGetFeaturesCommandFailed) {
  auto cmd = std::make_unique<FakeGetFeaturesCommand>();
  cmd->SetRunResult(false);

  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(cmd)));

  auto [unused_fan_rpms, err] = GetAllFanSpeedSync();
  EXPECT_EQ(err, "Failed to get number of fans");
}

TEST_F(DelegateImplTest, GetAllFanSpeedFanNotSupported) {
  auto cmd = std::make_unique<FakeGetFeaturesCommand>();
  cmd->SetRunResult(true);
  cmd->SetFeatureUnsupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(cmd)));

  auto [fan_rpms, err] = GetAllFanSpeedSync();
  EXPECT_THAT(fan_rpms, IsEmpty());
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(DelegateImplTest, GetAllFanSpeedPwmGetFanTargetRpmCommandFailed) {
  auto features_cmd = std::make_unique<FakeGetFeaturesCommand>();
  features_cmd->SetRunResult(true);
  features_cmd->SetFeatureSupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(features_cmd)));

  auto get_fan_rpm_cmd = std::make_unique<FakePwmGetFanTargetRpmCommand>();
  get_fan_rpm_cmd->SetRunResult(false);
  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(0))
      .WillOnce(Return(std::move(get_fan_rpm_cmd)));

  auto [unused_fan_rpms, err] = GetAllFanSpeedSync();
  EXPECT_EQ(err, "Failed to get number of fans");
}

TEST_F(DelegateImplTest, GetAllFanSpeedNoFan) {
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

  auto [fan_rpms, err] = GetAllFanSpeedSync();
  EXPECT_THAT(fan_rpms, IsEmpty());
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(DelegateImplTest, GetAllFanSpeedMultipleFans) {
  auto features_cmd = std::make_unique<FakeGetFeaturesCommand>();
  features_cmd->SetRunResult(true);
  features_cmd->SetFeatureSupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(features_cmd)));

  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(0))
      .Times(2)
      .WillRepeatedly([]() {
        auto get_fan_rpm_cmd =
            std::make_unique<FakePwmGetFanTargetRpmCommand>();
        get_fan_rpm_cmd->SetRunResult(true);
        get_fan_rpm_cmd->SetRpm(2000);
        return get_fan_rpm_cmd;
      });

  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(1))
      .Times(2)
      .WillRepeatedly([]() {
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

  auto [fan_rpms, err] = GetAllFanSpeedSync();
  EXPECT_THAT(fan_rpms, ElementsAreArray({2000, 3000}));
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(DelegateImplTest, GetAllFanSpeedStalledConsideredZeroRpm) {
  auto features_cmd = std::make_unique<FakeGetFeaturesCommand>();
  features_cmd->SetRunResult(true);
  features_cmd->SetFeatureSupported(EC_FEATURE_PWM_FAN);
  EXPECT_CALL(mock_ec_command_factory_, GetFeaturesCommand())
      .WillOnce(Return(std::move(features_cmd)));

  EXPECT_CALL(mock_ec_command_factory_, PwmGetFanTargetRpmCommand(0))
      .Times(2)
      .WillRepeatedly([]() {
        auto get_fan_rpm_cmd =
            std::make_unique<FakePwmGetFanTargetRpmCommand>();
        get_fan_rpm_cmd->SetRunResult(true);
        get_fan_rpm_cmd->SetRpm(EC_FAN_SPEED_STALLED_DEPRECATED);
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

  auto [fan_rpms, err] = GetAllFanSpeedSync();
  EXPECT_THAT(fan_rpms, ElementsAreArray({0}));
  EXPECT_EQ(err, std::nullopt);
}

TEST_F(DelegateImplTest, GetSmartBatteryManufactureDateSuccess) {
  auto cmd = std::make_unique<FakeI2cReadCommand>();
  cmd->SetRunResult(true);
  cmd->SetData(0x4d06);

  uint8_t i2c_port = 5;
  EXPECT_CALL(
      mock_ec_command_factory_,
      I2cReadCommand(i2c_port, kBatteryI2cAddress,
                     kBatteryI2cManufactureDateOffset, kBatteryI2cReadLen))
      .WillOnce(Return(std::move(cmd)));

  auto output = GetSmartBatteryManufactureDateSync(i2c_port);
  EXPECT_EQ(output, 0x4d06);
}

TEST_F(DelegateImplTest, GetSmartBatteryManufactureDateFailed) {
  auto cmd = std::make_unique<FakeI2cReadCommand>();
  cmd->SetRunResult(false);

  uint8_t i2c_port = 5;
  EXPECT_CALL(
      mock_ec_command_factory_,
      I2cReadCommand(i2c_port, kBatteryI2cAddress,
                     kBatteryI2cManufactureDateOffset, kBatteryI2cReadLen))
      .WillOnce(Return(std::move(cmd)));

  auto output = GetSmartBatteryManufactureDateSync(i2c_port);
  EXPECT_EQ(output, std::nullopt);
}

TEST_F(DelegateImplTest, GetSmartBatteryTemperatureSuccess) {
  auto cmd = std::make_unique<FakeI2cReadCommand>();
  cmd->SetRunResult(true);
  cmd->SetData(0xbae);

  uint8_t i2c_port = 5;
  EXPECT_CALL(mock_ec_command_factory_,
              I2cReadCommand(i2c_port, kBatteryI2cAddress,
                             kBatteryI2cTemperatureOffset, kBatteryI2cReadLen))
      .WillOnce(Return(std::move(cmd)));

  auto output = GetSmartBatteryTemperatureSync(i2c_port);
  EXPECT_EQ(output, 0xbae);
}

TEST_F(DelegateImplTest, GetSmartBatteryTemperatureFailed) {
  auto cmd = std::make_unique<FakeI2cReadCommand>();
  cmd->SetRunResult(false);

  uint8_t i2c_port = 5;
  EXPECT_CALL(mock_ec_command_factory_,
              I2cReadCommand(i2c_port, kBatteryI2cAddress,
                             kBatteryI2cTemperatureOffset, kBatteryI2cReadLen))
      .WillOnce(Return(std::move(cmd)));

  auto output = GetSmartBatteryTemperatureSync(i2c_port);
  EXPECT_EQ(output, std::nullopt);
}

TEST_F(DelegateImplTest, GetLidAngleSuccess) {
  auto cmd = std::make_unique<FakeMotionSenseCommandLidAngle>();
  cmd->SetRunResult(true);
  cmd->SetLidAngle(180);

  EXPECT_CALL(mock_ec_command_factory_, MotionSenseCommandLidAngle())
      .WillOnce(Return(std::move(cmd)));

  auto output = GetLidAngleSync();
  EXPECT_EQ(output, 180);
}

TEST_F(DelegateImplTest, GetLidAngleFailed) {
  auto cmd = std::make_unique<FakeMotionSenseCommandLidAngle>();
  cmd->SetRunResult(false);

  EXPECT_CALL(mock_ec_command_factory_, MotionSenseCommandLidAngle())
      .WillOnce(Return(std::move(cmd)));

  auto output = GetLidAngleSync();
  EXPECT_EQ(output, std::nullopt);
}

TEST_F(DelegateImplTest, GetLidAngleUnreliableResult) {
  auto cmd = std::make_unique<FakeMotionSenseCommandLidAngle>();
  cmd->SetRunResult(false);
  cmd->SetResult(1);

  EXPECT_CALL(mock_ec_command_factory_, MotionSenseCommandLidAngle())
      .WillOnce(Return(std::move(cmd)));

  auto output = GetLidAngleSync();
  EXPECT_EQ(output, LID_ANGLE_UNRELIABLE);
}

TEST_F(DelegateImplTest, RunPrimeSearchPassed) {
  base::TimeDelta exec_duration = base::Milliseconds(500);

  auto prime_number_search = std::make_unique<MockPrimeNumberSearchDelegate>();
  EXPECT_CALL(*prime_number_search, Run())
      .WillOnce(DoAll([this, exec_duration]() { FastForwardBy(exec_duration); },
                      Return(true)));
  EXPECT_CALL(delegate_, CreatePrimeNumberSearchDelegate(_))
      .WillOnce(Return(std::move(prime_number_search)));

  EXPECT_TRUE(RunPrimeSearchSync(exec_duration, 100));
}

TEST_F(DelegateImplTest, RunPrimeSearchFailed) {
  base::TimeDelta exec_duration = base::Milliseconds(500);

  auto prime_number_search = std::make_unique<MockPrimeNumberSearchDelegate>();
  EXPECT_CALL(*prime_number_search, Run())
      .WillOnce(DoAll([this, exec_duration]() { FastForwardBy(exec_duration); },
                      Return(false)));
  EXPECT_CALL(delegate_, CreatePrimeNumberSearchDelegate(_))
      .WillOnce(Return(std::move(prime_number_search)));

  EXPECT_FALSE(RunPrimeSearchSync(exec_duration, 100));
}

}  // namespace
}  // namespace diagnostics
