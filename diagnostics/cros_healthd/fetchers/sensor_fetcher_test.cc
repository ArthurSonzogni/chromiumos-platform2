// Copyright 2022 The ChromiumOS Authors
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
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

// Relative filepath used to determine whether a device has a Google EC.
constexpr char kRelativeCrosEcPath[] = "sys/class/chromeos/cros_ec";
// Acceptable error code for getting lid angle.
constexpr int kInvalidCommandCode = 1;
// Failure error code for getting lid angle.
constexpr int kExitFailureCode = 253;

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
    mock_context_.fake_mojo_service()->InitializeFakeMojoService();
  }

  const base::FilePath& root_dir() { return mock_context_.root_dir(); }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  FakeSensorService& fake_sensor_service() {
    return mock_context_.fake_mojo_service()->fake_sensor_service();
  }

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
  base::test::TaskEnvironment task_environment_;
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
  ASSERT_TRUE(sensor_info->sensors.has_value());
  ASSERT_TRUE(sensor_info->sensors.value().empty());
}

// Test that unreliable lid_angle can be handled and gets null.
TEST_F(SensorFetcherTest, FetchLidAngleUnreliable) {
  SetExecutorResponse("Lid angle: unreliable\n", EXIT_SUCCESS);

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_sensor_info());
  const auto& sensor_info = sensor_result->get_sensor_info();
  ASSERT_FALSE(sensor_info->lid_angle);
  ASSERT_TRUE(sensor_info->sensors.has_value());
  ASSERT_TRUE(sensor_info->sensors.value().empty());
}

// Test that incorredtly formatted lid_angle can be handled and gets ProbeError.
TEST_F(SensorFetcherTest, FetchLidAngleIncorrectlyFormatted) {
  SetExecutorResponse("Lid angle: incorredtly formatted\n", EXIT_SUCCESS);

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_error());
  EXPECT_EQ(sensor_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kParseError);
}

// Test that acceptable error code can be handled and gets null lid_angle.
TEST_F(SensorFetcherTest, FetchLidAngleAcceptableError) {
  SetExecutorResponse("EC result 1 (INVALID_COMMAND)\n", kInvalidCommandCode);

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_sensor_info());
  const auto& sensor_info = sensor_result->get_sensor_info();
  ASSERT_FALSE(sensor_info->lid_angle);
  ASSERT_TRUE(sensor_info->sensors.has_value());
  ASSERT_TRUE(sensor_info->sensors.value().empty());
}

// Test that the executor fails to collect lid_angle and gets ProbeError.
TEST_F(SensorFetcherTest, FetchLidAngleFailure) {
  SetExecutorResponse("Some error happened!\n", kExitFailureCode);

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_error());
  EXPECT_EQ(sensor_result->get_error()->type,
            chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError);
}

// Test that without Google EC can be handled and gets null lid_angle.
TEST_F(SensorFetcherTest, FetchLidAngleWithoutEC) {
  ASSERT_TRUE(
      base::DeletePathRecursively(root_dir().Append(kRelativeCrosEcPath)));

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_sensor_info());
  const auto& sensor_info = sensor_result->get_sensor_info();
  ASSERT_FALSE(sensor_info->lid_angle);
  ASSERT_TRUE(sensor_info->sensors.has_value());
  ASSERT_TRUE(sensor_info->sensors.value().empty());
}

// Test that single sensor's attributes can be fetched successfully.
TEST_F(SensorFetcherTest, FetchSensorAttribue) {
  SetExecutorResponse("Lid angle: 120\n", EXIT_SUCCESS);
  fake_sensor_service().SetIdsTypes({{0, {cros::mojom::DeviceType::ACCEL}}});

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_sensor_info());
  const auto& sensor_info = sensor_result->get_sensor_info();
  ASSERT_TRUE(sensor_info->sensors.has_value());
  const auto& sensors = sensor_info->sensors.value();
  ASSERT_EQ(sensors.size(), 1);
  ASSERT_FALSE(sensors[0]->name.has_value());
  ASSERT_EQ(sensors[0]->device_id, 0);
  ASSERT_EQ(sensors[0]->type, mojom::Sensor::Type::kAccel);
  ASSERT_EQ(sensors[0]->location, mojom::Sensor::Location::kUnknown);
}

// Test that multiple sensors' attributes can be fetched successfully.
TEST_F(SensorFetcherTest, FetchMultipleSensorAttribue) {
  SetExecutorResponse("Lid angle: 120\n", EXIT_SUCCESS);

  fake_sensor_service().SetIdsTypes(
      {{1, {cros::mojom::DeviceType::ANGL}},
       {3, {cros::mojom::DeviceType::ANGLVEL}},
       {4, {cros::mojom::DeviceType::LIGHT}},
       {10000, {cros::mojom::DeviceType::GRAVITY}}});

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_sensor_info());
  const auto& sensor_info = sensor_result->get_sensor_info();
  ASSERT_TRUE(sensor_info->sensors.has_value());
  const auto& sensors = sensor_info->sensors.value();
  ASSERT_EQ(sensors.size(), 4);
  ASSERT_FALSE(sensors[0]->name.has_value());
  ASSERT_EQ(sensors[0]->device_id, 1);
  ASSERT_EQ(sensors[0]->type, mojom::Sensor::Type::kAngle);
  ASSERT_EQ(sensors[0]->location, mojom::Sensor::Location::kUnknown);

  ASSERT_FALSE(sensors[1]->name.has_value());
  ASSERT_EQ(sensors[1]->device_id, 3);
  ASSERT_EQ(sensors[1]->type, mojom::Sensor::Type::kGyro);
  ASSERT_EQ(sensors[1]->location, mojom::Sensor::Location::kUnknown);

  ASSERT_FALSE(sensors[2]->name.has_value());
  ASSERT_EQ(sensors[2]->device_id, 4);
  ASSERT_EQ(sensors[2]->type, mojom::Sensor::Type::kLight);
  ASSERT_EQ(sensors[2]->location, mojom::Sensor::Location::kUnknown);

  ASSERT_FALSE(sensors[3]->name.has_value());
  ASSERT_EQ(sensors[3]->device_id, 10000);
  ASSERT_EQ(sensors[3]->type, mojom::Sensor::Type::kGravity);
  ASSERT_EQ(sensors[3]->location, mojom::Sensor::Location::kUnknown);
}

// Test that combo sensor's attributes can be fetched successfully.
TEST_F(SensorFetcherTest, FetchSensorAttribueComboSensor) {
  SetExecutorResponse("Lid angle: 120\n", EXIT_SUCCESS);
  fake_sensor_service().SetIdsTypes(
      {{100, {cros::mojom::DeviceType::ANGL, cros::mojom::DeviceType::ACCEL}}});

  auto sensor_result = FetchSensorInfoSync();
  ASSERT_TRUE(sensor_result->is_sensor_info());
  const auto& sensor_info = sensor_result->get_sensor_info();
  ASSERT_TRUE(sensor_info->sensors.has_value());
  const auto& sensors = sensor_info->sensors.value();
  ASSERT_EQ(sensors.size(), 2);
  ASSERT_FALSE(sensors[0]->name.has_value());
  ASSERT_EQ(sensors[0]->device_id, 100);
  ASSERT_EQ(sensors[0]->type, mojom::Sensor::Type::kAngle);
  ASSERT_EQ(sensors[0]->location, mojom::Sensor::Location::kUnknown);

  ASSERT_FALSE(sensors[1]->name.has_value());
  ASSERT_EQ(sensors[1]->device_id, 100);
  ASSERT_EQ(sensors[1]->type, mojom::Sensor::Type::kAccel);
  ASSERT_EQ(sensors[1]->location, mojom::Sensor::Location::kUnknown);
}

}  // namespace
}  // namespace diagnostics
