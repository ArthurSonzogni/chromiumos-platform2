// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/files/file_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/fetchers/fan_fetcher.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;

// Test values for fan speed.
constexpr uint16_t kFirstFanSpeedRpm = 2255;
constexpr uint16_t kSecondFanSpeedRpm = 1263;

class FanUtilsTest : public ::testing::Test {
 protected:
  FanUtilsTest() = default;

  void SetUp() override {
    ASSERT_TRUE(base::CreateDirectory(root_dir().Append(kRelativeCrosEcPath)));
  }

  const base::FilePath& root_dir() { return mock_context_.root_dir(); }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  mojom::FanResultPtr FetchFanInfo() {
    base::test::TestFuture<mojom::FanResultPtr> future;
    fan_fetcher_.FetchFanInfo(future.GetCallback());
    return future.Take();
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
  FanFetcher fan_fetcher_{&mock_context_};
};

// Test that fan information can be fetched successfully.
TEST_F(FanUtilsTest, FetchFanInfo) {
  // Set the mock executor response.
  std::vector<uint16_t> fan_rpms = {kFirstFanSpeedRpm, kSecondFanSpeedRpm};
  std::optional<std::string> error = std::nullopt;
  EXPECT_CALL(*mock_executor(), GetAllFanSpeed(_))
      .WillOnce(base::test::RunOnceCallback<0>(fan_rpms, error));

  auto fan_result = FetchFanInfo();

  ASSERT_TRUE(fan_result->is_fan_info());
  const auto& fan_info = fan_result->get_fan_info();
  ASSERT_EQ(fan_info.size(), 2);
  EXPECT_EQ(fan_info[0]->speed_rpm, kFirstFanSpeedRpm);
  EXPECT_EQ(fan_info[1]->speed_rpm, kSecondFanSpeedRpm);
}

// Test that no fan information is returned for a device that has no fan.
TEST_F(FanUtilsTest, NoFan) {
  // Set the mock executor response.
  std::vector<uint16_t> fan_rpms = {};
  std::optional<std::string> error = std::nullopt;
  EXPECT_CALL(*mock_executor(), GetAllFanSpeed(_))
      .WillOnce(base::test::RunOnceCallback<0>(fan_rpms, error));

  auto fan_result = FetchFanInfo();

  ASSERT_TRUE(fan_result->is_fan_info());
  EXPECT_EQ(fan_result->get_fan_info().size(), 0);
}

// Test that the executor failing to collect fan speed fails gracefully and
// returns a ProbeError.
TEST_F(FanUtilsTest, CollectFanSpeedFailure) {
  // Set the mock executor response.
  std::vector<uint16_t> fan_rpms = {};
  std::optional<std::string> error = "Some error happened!";
  EXPECT_CALL(*mock_executor(), GetAllFanSpeed(_))
      .WillOnce(base::test::RunOnceCallback<0>(fan_rpms, error));

  auto fan_result = FetchFanInfo();

  ASSERT_TRUE(fan_result->is_error());
  EXPECT_EQ(fan_result->get_error()->type,
            mojom::ErrorType::kSystemUtilityError);
}

// Test that no fan info is fetched for a device that does not have a Google EC.
TEST_F(FanUtilsTest, NoGoogleEc) {
  ASSERT_TRUE(
      brillo::DeletePathRecursively(root_dir().Append(kRelativeCrosEcPath)));

  auto fan_result = FetchFanInfo();

  ASSERT_TRUE(fan_result->is_fan_info());
  EXPECT_EQ(fan_result->get_fan_info().size(), 0);
}

}  // namespace
}  // namespace diagnostics
