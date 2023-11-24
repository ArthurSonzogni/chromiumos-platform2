// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>

#include <base/test/test_future.h>
#include <chromeos/ec/ec_commands.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libec/mock_ec_command_factory.h>

#include "diagnostics/base/file_test_utils.h"
#include "diagnostics/cros_healthd/delegate/delegate_impl.h"
#include "diagnostics/cros_healthd/mojom/delegate.mojom.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::Return;

namespace mojom = ::ash::cros_healthd::mojom;

class MockLedControlAutoCommand : public ec::LedControlAutoCommand {
 public:
  explicit MockLedControlAutoCommand(enum ec_led_id led_id)
      : ec::LedControlAutoCommand(led_id) {}
  MOCK_METHOD(bool, Run, (int fd));
};

class DelegateImplTest : public ::testing::Test {
 public:
  DelegateImplTest(const DelegateImplTest&) = delete;
  DelegateImplTest& operator=(const DelegateImplTest&) = delete;

 protected:
  DelegateImplTest() = default;

  void CreateEcFile() {
    const auto path = GetRootedPath(ec::kCrosEcPath);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(path, ""));
  }

  ec::MockEcCommandFactory mock_ec_command_factory_;
  DelegateImpl delegate_{&mock_ec_command_factory_};

 private:
  ScopedRootDirOverrides root_overrides_;
};

TEST_F(DelegateImplTest, ResetLedColorErrorUnknownLedName) {
  base::test::TestFuture<const std::optional<std::string>&> err_future;
  delegate_.ResetLedColor(mojom::LedName::kUnmappedEnumField,
                          err_future.GetCallback());
  auto err = err_future.Take();
  EXPECT_EQ(err, "Unknown LED name");
}

TEST_F(DelegateImplTest, ResetLedColorErrorEcCommandFailed) {
  const mojom::LedName arbitrary_led_name = mojom::LedName::kBattery;

  CreateEcFile();

  EXPECT_CALL(mock_ec_command_factory_, LedControlAutoCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<MockLedControlAutoCommand>(led_id);
        EXPECT_CALL(*cmd, Run(_)).WillOnce(Return(false));
        return cmd;
      });

  base::test::TestFuture<const std::optional<std::string>&> err_future;
  delegate_.ResetLedColor(arbitrary_led_name, err_future.GetCallback());
  auto err = err_future.Take();
  EXPECT_EQ(err, "Failed to reset LED color");
}

TEST_F(DelegateImplTest, ResetLedColorSuccess) {
  const mojom::LedName arbitrary_led_name = mojom::LedName::kBattery;

  CreateEcFile();

  EXPECT_CALL(mock_ec_command_factory_, LedControlAutoCommand(_))
      .WillOnce([](enum ec_led_id led_id) {
        auto cmd = std::make_unique<MockLedControlAutoCommand>(led_id);
        EXPECT_CALL(*cmd, Run(_)).WillOnce(Return(true));
        return cmd;
      });

  base::test::TestFuture<const std::optional<std::string>&> err_future;
  delegate_.ResetLedColor(arbitrary_led_name, err_future.GetCallback());
  auto err = err_future.Take();
  EXPECT_EQ(err, std::nullopt);
}

}  // namespace
}  // namespace diagnostics
