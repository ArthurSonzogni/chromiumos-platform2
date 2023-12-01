// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/delegate_impl.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

#include <base/test/test_future.h>
#include <chromeos/ec/ec_commands.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/led_control_command.h>
#include <libec/mock_ec_command_factory.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::WithArg;

namespace mojom = ::ash::cros_healthd::mojom;

inline constexpr mojom::LedName kArbitraryValidLedName =
    mojom::LedName::kBattery;
inline constexpr mojom::LedColor kArbitraryValidLedColor =
    mojom::LedColor::kAmber;
// The ec_led_colors corresponding to |kArbitraryValidLedColor|.
inline constexpr ec_led_colors kArbitraryValidLedColorEcEnum =
    EC_LED_COLOR_AMBER;

class FakeLedControlAutoCommand : public ec::LedControlAutoCommand {
 public:
  explicit FakeLedControlAutoCommand(enum ec_led_id led_id)
      : ec::LedControlAutoCommand(led_id) {}

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

 private:
  bool fake_run_result_ = false;
};

class FakeLedControlQueryCommand : public ec::LedControlQueryCommand {
 public:
  explicit FakeLedControlQueryCommand(enum ec_led_id led_id)
      : ec::LedControlQueryCommand(led_id) {}

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
  explicit FakeLedControlSetCommand(enum ec_led_id led_id)
      : ec::LedControlSetCommand(led_id, /*brightness=*/{}) {}

  // ec::EcCommand overrides.
  bool Run(int fd) override { return fake_run_result_; }

  void SetRunResult(bool result) { fake_run_result_ = result; }

 private:
  bool fake_run_result_ = false;
};

class DelegateImplTest : public BaseFileTest {
 public:
  DelegateImplTest(const DelegateImplTest&) = delete;
  DelegateImplTest& operator=(const DelegateImplTest&) = delete;

 protected:
  DelegateImplTest() = default;

  void SetUp() override { SetFile(ec::kCrosEcPath, ""); }

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

  ec::MockEcCommandFactory mock_ec_command_factory_;
  DelegateImpl delegate_{&mock_ec_command_factory_};
};

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
  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlQueryCommand>(led_id);
        cmd->SetRunResult(false);
        return cmd;
      });

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, "Failed to query the LED brightness range");
}

TEST_F(DelegateImplTest, SetLedColorErrorUnsupportedColor) {
  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlQueryCommand>(led_id);
        cmd->SetRunResult(true);
        cmd->SetBrightness(kArbitraryValidLedColorEcEnum, 0);
        return cmd;
      });

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, "Unsupported color");
}

TEST_F(DelegateImplTest, SetLedColorErrorSetCommandFailed) {
  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlQueryCommand>(led_id);
        cmd->SetRunResult(true);
        cmd->SetBrightness(kArbitraryValidLedColorEcEnum, 1);
        return cmd;
      });

  EXPECT_CALL(mock_ec_command_factory_, LedControlSetCommand(_, _))
      .WillOnce(WithArg<0>([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlSetCommand>(led_id);
        cmd->SetRunResult(false);
        return cmd;
      }));

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, "Failed to set the LED color");
}

TEST_F(DelegateImplTest, SetLedColorSuccess) {
  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlQueryCommand>(led_id);
        cmd->SetRunResult(true);
        cmd->SetBrightness(kArbitraryValidLedColorEcEnum, 1);
        return cmd;
      });

  EXPECT_CALL(mock_ec_command_factory_, LedControlSetCommand(_, _))
      .WillOnce(WithArg<0>([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlSetCommand>(led_id);
        cmd->SetRunResult(true);
        return cmd;
      }));

  auto err = SetLedColorSync(kArbitraryValidLedName, kArbitraryValidLedColor);
  EXPECT_EQ(err, std::nullopt);
}

// The EC command to set LED brightness should respect the brightness range.
TEST_F(DelegateImplTest, SetLedColorUsesMaxBrightness) {
  EXPECT_CALL(mock_ec_command_factory_, LedControlQueryCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlQueryCommand>(led_id);
        cmd->SetRunResult(true);
        cmd->SetBrightness(kArbitraryValidLedColorEcEnum, 64);
        return cmd;
      });

  std::array<uint8_t, EC_LED_COLOR_COUNT> received_brightness = {};
  EXPECT_CALL(mock_ec_command_factory_, LedControlSetCommand(_, _))
      .WillOnce(DoAll(SaveArg<1>(&received_brightness),
                      WithArg<0>([](enum ec_led_id led_id) {
                        auto cmd =
                            std::make_unique<FakeLedControlSetCommand>(led_id);
                        cmd->SetRunResult(true);
                        return cmd;
                      })));

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
  EXPECT_CALL(mock_ec_command_factory_, LedControlAutoCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlAutoCommand>(led_id);
        cmd->SetRunResult(false);
        return cmd;
      });

  auto err = ResetLedColorSync(kArbitraryValidLedName);
  EXPECT_EQ(err, "Failed to reset LED color");
}

TEST_F(DelegateImplTest, ResetLedColorSuccess) {
  EXPECT_CALL(mock_ec_command_factory_, LedControlAutoCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<FakeLedControlAutoCommand>(led_id);
        cmd->SetRunResult(true);
        return cmd;
      });

  auto err = ResetLedColorSync(kArbitraryValidLedName);
  EXPECT_EQ(err, std::nullopt);
}

}  // namespace
}  // namespace diagnostics
