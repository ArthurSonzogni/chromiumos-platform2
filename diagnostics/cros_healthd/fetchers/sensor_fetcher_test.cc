// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/fetchers/sensor_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

// Relative filepath used to determine whether a device has a Google EC.
constexpr char kRelativeCrosEcPath[] = "sys/class/chromeos/cros_ec";

// Saves |response| to |response_destination|.
void OnGetSensorResponseReceived(mojom::SensorResultPtr* response_destination,
                                 base::OnceClosure quit_closure,
                                 mojom::SensorResultPtr response) {
  *response_destination = std::move(response);
  std::move(quit_closure).Run();
}

class SensorFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(base::CreateDirectory(root_dir().Append(kRelativeCrosEcPath)));
  }

  const base::FilePath& root_dir() { return mock_context_.root_dir(); }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  mojom::SensorResultPtr FetchSensorInfoSync() {
    base::RunLoop run_loop;
    mojom::SensorResultPtr result;
    FetchSensorInfo(&mock_context_,
                    base::BindOnce(&OnGetSensorResponseReceived, &result,
                                   run_loop.QuitClosure()));
    run_loop.Run();
    return result;
  }

  void SetExecutorResponse(const std::string& out, int32_t return_code) {
    EXPECT_CALL(*mock_executor(), GetLidAngle(_))
        .WillOnce(WithArg<0>(
            Invoke([=](mojom::Executor::GetLidAngleCallback callback) {
              mojom::ExecutedProcessResult result;
              result.return_code = return_code;
              result.out = out;
              std::move(callback).Run(result.Clone());
            })));
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  MockContext mock_context_;
};

// Test that lid_angle can be fetched successfully.
TEST_F(SensorFetcherTest, FetchLidAngle) {
  SetExecutorResponse("Lid angle: 120\n", EXIT_SUCCESS);

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_sensor_info());
  const auto& sensor_info = sensor_result->get_sensor_info();
  ASSERT_TRUE(sensor_info->lid_angle);
  ASSERT_EQ(sensor_info->lid_angle->value, 120);
}

// Test that unreliable lid_angle can be handled and gets null.
TEST_F(SensorFetcherTest, FetchLidAngleUnreliable) {
  SetExecutorResponse("Lid angle: unreliable\n", EXIT_SUCCESS);

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_sensor_info());
  const auto& sensor_info = sensor_result->get_sensor_info();
  ASSERT_FALSE(sensor_info->lid_angle);
}

// Test that incorredtly formatted lid_angle can be handled and gets ProbeError.
TEST_F(SensorFetcherTest, FetchLidAngleIncorrectlyFormatted) {
  SetExecutorResponse("Lid angle: incorredtly formatted\n", EXIT_SUCCESS);

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_error());
  EXPECT_EQ(sensor_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that the executor fails to collect lid_angle and gets ProbeError.
TEST_F(SensorFetcherTest, FetchLidAngleFailure) {
  SetExecutorResponse("Some error happened!\n", EXIT_FAILURE);

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_error());
  EXPECT_EQ(sensor_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError);
}

}  // namespace
}  // namespace diagnostics
