// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/sensor/sensitive_sensor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "diagnostics/cros_healthd/fake/fake_sensor_device.h"
#include "diagnostics/cros_healthd/fake/fake_sensor_service.h"
#include "diagnostics/cros_healthd/routines/base_routine_control.h"
#include "diagnostics/cros_healthd/routines/routine_observer_for_testing.h"
#include "diagnostics/cros_healthd/routines/routine_v2_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/fake_system_config.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

class SensitiveSensorRoutineTest : public ::testing::Test {
 public:
  SensitiveSensorRoutineTest(const SensitiveSensorRoutineTest&) = delete;
  SensitiveSensorRoutineTest& operator=(const SensitiveSensorRoutineTest&) =
      delete;

 protected:
  SensitiveSensorRoutineTest() = default;

  void SetUp() override {
    mock_context_.fake_mojo_service()->InitializeFakeMojoService();
    routine_ = std::make_unique<SensitiveSensorRoutine>(&mock_context_);
  }

  mojom::SensitiveSensorRoutineDetailPtr ConstructDefaultOutput() {
    auto output = mojom::SensitiveSensorRoutineDetail::New();
    auto default_report = mojom::SensitiveSensorReport::New();
    default_report->sensor_presence_status =
        mojom::HardwarePresenceStatus::kNotConfigured;
    output->base_accelerometer = default_report->Clone();
    output->lid_accelerometer = default_report->Clone();
    output->base_gyroscope = default_report->Clone();
    output->lid_gyroscope = default_report->Clone();
    output->base_magnetometer = default_report->Clone();
    output->lid_magnetometer = default_report->Clone();
    output->base_gravity_sensor = default_report->Clone();
    output->lid_gravity_sensor = std::move(default_report);
    return output;
  }

  std::unique_ptr<FakeSensorDevice> MakeSensorDevice(
      std::vector<std::string> channels = {},
      base::OnceClosure remote_on_bound = base::DoNothing()) {
    return std::make_unique<FakeSensorDevice>(
        /*name=*/std::nullopt, /*location=*/cros::mojom::kLocationBase,
        channels, std::move(remote_on_bound));
  }

  mojo::Remote<cros::mojom::SensorDeviceSamplesObserver>&
  SetupSensorDeviceAndGetObserverRemote(
      int32_t device_id, std::unique_ptr<FakeSensorDevice> device) {
    auto& remote = device->observer();
    fake_sensor_service().SetSensorDevice(device_id, std::move(device));
    return remote;
  }

  // Helper function for creating a sensor info pointer with given properties.
  mojom::SensitiveSensorInfoPtr CreateSensorInfo(
      int32_t id,
      std::vector<mojom::SensitiveSensorInfo::Type> types,
      std::vector<std::string> channels) {
    auto output = mojom::SensitiveSensorInfo::New();
    output->id = id;
    output->types = types;
    output->channels = channels;
    return output;
  }

  // Helper function for creating a accelerometer.
  mojom::SensitiveSensorInfoPtr CreateAccelerometerInfo() {
    return CreateSensorInfo(
        0, {mojom::SensitiveSensorInfo::Type::kAccel},
        {cros::mojom::kTimestampChannel, "accel_x", "accel_y", "accel_z"});
  }

  std::unique_ptr<RoutineObserverForTesting> RunRoutineAndGetObserver() {
    auto observer = std::make_unique<RoutineObserverForTesting>();
    routine_->SetObserver(observer->receiver_.BindNewPipeAndPassRemote());
    routine_->Start();
    return observer;
  }

  mojom::RoutineStatePtr RunRoutineAndWaitForExit() {
    routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
    auto observer = RunRoutineAndGetObserver();
    observer->WaitUntilRoutineFinished();
    return std::move(observer->state_);
  }

  void RunRoutineAndWaitForException(const std::string& expected_reason) {
    base::test::TestFuture<uint32_t, const std::string&> future;
    routine_->SetOnExceptionCallback(future.GetCallback());
    routine_->Start();
    EXPECT_EQ(future.Get<std::string>(), expected_reason)
        << "Unexpected reason in exception.";
  }

  FakeSensorService& fake_sensor_service() {
    return mock_context_.fake_mojo_service()->fake_sensor_service();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  std::unique_ptr<BaseRoutineControl> routine_;
};

// Test that the sensitive sensor routine can be run successfully.
TEST_F(SensitiveSensorRoutineTest, RoutineSuccess) {
  fake_sensor_service().SetIdsTypes({{0, {cros::mojom::DeviceType::ACCEL}}});
  base::test::TestFuture<void> future;
  auto& remote = SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/0, MakeSensorDevice({cros::mojom::kTimestampChannel,
                                         "accel_x", "accel_y", "accel_z"},
                                        future.GetCallback()));

  routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  auto observer = RunRoutineAndGetObserver();

  // Wait for the observer remote to be bound.
  EXPECT_TRUE(future.Wait());

  // Send sample data.
  remote->OnSampleUpdated({{0, 21}, {1, 14624}, {2, 6373}, {3, 2389718579704}});
  remote->OnSampleUpdated({{0, 5}, {1, 14613}, {2, 6336}, {3, 2389880497684}});
  remote.FlushForTesting();

  observer->WaitUntilRoutineFinished();
  mojom::RoutineStatePtr result = std::move(observer->state_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  ASSERT_TRUE(state->detail->is_sensitive_sensor());

  const auto& detail = state->detail->get_sensitive_sensor();
  auto expected_output = ConstructDefaultOutput();
  expected_output->base_accelerometer->passed_sensors.push_back(
      CreateAccelerometerInfo());
  EXPECT_EQ(detail, expected_output);
}

// Test that the sensitive sensor routine can be run successfully with multiple
// sensor devices.
TEST_F(SensitiveSensorRoutineTest, RoutineSuccessWithMultipleSensors) {
  // Set up multiple sensors.
  fake_sensor_service().SetIdsTypes({
      {0, {cros::mojom::DeviceType::ACCEL}},
      {1, {cros::mojom::DeviceType::LIGHT}},  // Unsupported.
      {4, {cros::mojom::DeviceType::ANGLVEL}},
      {5, {cros::mojom::DeviceType::MAGN}},
      {10000, {cros::mojom::DeviceType::GRAVITY}},
  });

  base::test::TestFuture<void> future;
  auto barrier =
      std::make_unique<CallbackBarrier>(/*on_success=*/future.GetCallback(),
                                        /*on_error=*/base::DoNothing());
  auto& remote1 = SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/0, MakeSensorDevice({cros::mojom::kTimestampChannel,
                                         "accel_x", "accel_y", "accel_z"},
                                        barrier->CreateDependencyClosure()));
  auto& remote2 = SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/4, MakeSensorDevice({cros::mojom::kTimestampChannel,
                                         "anglvel_x", "anglvel_y", "anglvel_z"},
                                        barrier->CreateDependencyClosure()));
  auto& remote3 = SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/5, MakeSensorDevice({cros::mojom::kTimestampChannel,
                                         "magn_x", "magn_y", "magn_z"},
                                        barrier->CreateDependencyClosure()));
  auto& remote4 = SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/10000,
      MakeSensorDevice({cros::mojom::kTimestampChannel, "gravity_x",
                        "gravity_y", "gravity_z"},
                       barrier->CreateDependencyClosure()));
  barrier.reset();

  routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  auto observer = RunRoutineAndGetObserver();

  // Wait for the observer remotes to be bound.
  EXPECT_TRUE(future.Wait());

  // Send sample data.
  remote1->OnSampleUpdated({{0, 2}, {1, 14624}, {2, 6373}, {3, 2389718579704}});
  remote1->OnSampleUpdated({{0, 5}, {1, 14613}, {2, 6336}, {3, 2389880497684}});

  remote2->OnSampleUpdated({{0, 12}, {1, 1}, {2, -9}, {3, 2389839652059}});
  remote2->OnSampleUpdated({{0, 13}, {1, 1}, {2, -8}, {3, 2390042356277}});
  remote2->OnSampleUpdated({{0, 13}, {1, 1}, {2, -9}, {3, 2390244860172}});
  remote2->OnSampleUpdated({{0, 13}, {1, 0}, {2, -8}, {3, 2390453843393}});

  remote3->OnSampleUpdated({{0, 144}, {1, -178}, {2, 311}, {3, 2389922994702}});
  remote3->OnSampleUpdated({{0, 146}, {1, -178}, {2, 290}, {3, 2390085944536}});
  remote3->OnSampleUpdated({{0, 145}, {1, -179}, {2, 311}, {3, 2390285345718}});

  remote4->OnSampleUpdated({{0, 270}, {1, -98}, {2, 8186}, {3, 2390085944536}});
  remote4->OnSampleUpdated({{0, 269}, {1, -87}, {2, 8187}, {3, 2390285345717}});

  remote1.FlushForTesting();
  remote2.FlushForTesting();
  remote3.FlushForTesting();
  remote4.FlushForTesting();

  observer->WaitUntilRoutineFinished();
  mojom::RoutineStatePtr result = std::move(observer->state_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  ASSERT_TRUE(state->detail->is_sensitive_sensor());

  const auto& detail = state->detail->get_sensitive_sensor();
  auto expected_output = ConstructDefaultOutput();
  expected_output->base_accelerometer->passed_sensors.push_back(
      CreateAccelerometerInfo());
  expected_output->base_gyroscope->passed_sensors.push_back(CreateSensorInfo(
      4, {mojom::SensitiveSensorInfo::Type::kGyro},
      {cros::mojom::kTimestampChannel, "anglvel_x", "anglvel_y", "anglvel_z"}));
  expected_output->base_magnetometer->passed_sensors.push_back(CreateSensorInfo(
      5, {mojom::SensitiveSensorInfo::Type::kMagn},
      {cros::mojom::kTimestampChannel, "magn_x", "magn_y", "magn_z"}));
  expected_output->base_gravity_sensor->passed_sensors.push_back(
      CreateSensorInfo(10000, {mojom::SensitiveSensorInfo::Type::kGravity},
                       {cros::mojom::kTimestampChannel, "gravity_x",
                        "gravity_y", "gravity_z"}));
  EXPECT_EQ(detail, expected_output);
}

// Test that the sensitive sensor routine can be run successfully without
// sensor.
TEST_F(SensitiveSensorRoutineTest, RoutineSuccessWithoutSensor) {
  fake_sensor_service().SetIdsTypes({});

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_TRUE(state->has_passed);
  ASSERT_TRUE(state->detail->is_sensitive_sensor());

  const auto& detail = state->detail->get_sensitive_sensor();
  auto expected_output = ConstructDefaultOutput();
  EXPECT_EQ(detail, expected_output);
}

// Test that the sensitive sensor routine reports failure when the existence
// check is failed.
TEST_F(SensitiveSensorRoutineTest, RoutineExistenceCheckFailure) {
  fake_sensor_service().SetIdsTypes({});
  // Setup wrong configuration.
  mock_context_.fake_system_config()->SetSensor(SensorType::kBaseAccelerometer,
                                                true);

  mojom::RoutineStatePtr result = RunRoutineAndWaitForExit();
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  ASSERT_TRUE(state->detail->is_sensitive_sensor());

  const auto& detail = state->detail->get_sensitive_sensor();
  auto expected_output = ConstructDefaultOutput();
  expected_output->base_accelerometer->sensor_presence_status =
      mojom::HardwarePresenceStatus::kNotMatched;
  EXPECT_EQ(detail, expected_output);
}

// Test that the sensitive sensor routine raises exception when sensor device
// failed to set frequency.
TEST_F(SensitiveSensorRoutineTest, RoutineSetFrequencyError) {
  fake_sensor_service().SetIdsTypes({{0, {cros::mojom::DeviceType::ACCEL}}});
  auto device = MakeSensorDevice();
  device->set_return_frequency(-1);
  SetupSensorDeviceAndGetObserverRemote(/*device_id=*/0, std::move(device));

  RunRoutineAndWaitForException("Routine failed to set frequency.");
}

// Test that the sensitive sensor routine raises exception when sensor device
// doesn't have required channels.
TEST_F(SensitiveSensorRoutineTest, RoutineGetRequiredChannelsError) {
  fake_sensor_service().SetIdsTypes({{0, {cros::mojom::DeviceType::ACCEL}}});
  SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/0,
      MakeSensorDevice({cros::mojom::kTimestampChannel, "accel_x", "accel_z"}));

  RunRoutineAndWaitForException("Routine failed to get required channels.");
}

// Test that the sensitive sensor routine raises exception when sensor device
// failed to set all channels enabled.
TEST_F(SensitiveSensorRoutineTest, RoutineSetChannelsEnabledError) {
  fake_sensor_service().SetIdsTypes({{0, {cros::mojom::DeviceType::ACCEL}}});
  auto device = MakeSensorDevice(
      {cros::mojom::kTimestampChannel, "accel_x", "accel_y", "accel_z"});
  device->set_failed_channel_indices({0});
  SetupSensorDeviceAndGetObserverRemote(/*device_id=*/0, std::move(device));

  RunRoutineAndWaitForException("Routine failed to set channels enabled.");
}

// Test that the sensitive sensor routine raises exception when sensor device
// return error.
TEST_F(SensitiveSensorRoutineTest, RoutineReadSampleError) {
  fake_sensor_service().SetIdsTypes({{0, {cros::mojom::DeviceType::ACCEL}}});
  base::test::TestFuture<void> future;
  auto& remote = SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/0, MakeSensorDevice({cros::mojom::kTimestampChannel,
                                         "accel_x", "accel_y", "accel_z"},
                                        future.GetCallback()));

  base::test::TestFuture<uint32_t, const std::string&> exception_future;
  routine_->SetOnExceptionCallback(exception_future.GetCallback());
  routine_->Start();

  // Wait for the observer remote to be bound.
  EXPECT_TRUE(future.Wait());

  // Send observer error.
  remote->OnErrorOccurred(cros::mojom::ObserverErrorType::READ_TIMEOUT);
  remote.FlushForTesting();

  EXPECT_EQ(exception_future.Get<std::string>(),
            "Observer error occurred while reading sample.");
}

// Test that the sensitive sensor routine reports failure if sensor device
// cannot read changed sample before timeout.
TEST_F(SensitiveSensorRoutineTest, RoutineNoChangedSampleFailure) {
  fake_sensor_service().SetIdsTypes({{0, {cros::mojom::DeviceType::ACCEL}}});
  base::test::TestFuture<void> future;
  auto& remote = SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/0, MakeSensorDevice({cros::mojom::kTimestampChannel,
                                         "accel_x", "accel_y", "accel_z"},
                                        future.GetCallback()));
  routine_->SetOnExceptionCallback(UnexpectedRoutineExceptionCallback());
  auto observer = RunRoutineAndGetObserver();

  // Wait for the observer remote to be bound.
  EXPECT_TRUE(future.Wait());

  remote->OnSampleUpdated({{0, 2}, {1, 14624}, {2, 6373}, {3, 2389718579704}});
  remote->OnSampleUpdated({{0, 2}, {1, 14624}, {2, 6373}, {3, 2389718579704}});
  remote.FlushForTesting();

  observer->WaitUntilRoutineFinished();
  mojom::RoutineStatePtr result = std::move(observer->state_);
  EXPECT_EQ(result->percentage, 100);
  ASSERT_TRUE(result->state_union->is_finished());

  const auto& state = result->state_union->get_finished();
  EXPECT_FALSE(state->has_passed);
  ASSERT_TRUE(state->detail->is_sensitive_sensor());

  const auto& detail = state->detail->get_sensitive_sensor();
  auto expected_output = ConstructDefaultOutput();
  expected_output->base_accelerometer->failed_sensors.push_back(
      CreateAccelerometerInfo());
  EXPECT_EQ(detail, expected_output);
}

// Test that the sensitive sensor routine raises exception if sensor device
// cannot read any samples before timeout.
TEST_F(SensitiveSensorRoutineTest, RoutineNoSamplesError) {
  fake_sensor_service().SetIdsTypes({{0, {cros::mojom::DeviceType::ACCEL}}});
  SetupSensorDeviceAndGetObserverRemote(
      /*device_id=*/0, MakeSensorDevice({cros::mojom::kTimestampChannel,
                                         "accel_x", "accel_y", "accel_z"}));

  RunRoutineAndWaitForException(
      "Routine failed to read sample from sensor device.");
}

}  // namespace
}  // namespace diagnostics
