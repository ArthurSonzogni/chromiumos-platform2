// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/sensor/sensitive_sensor.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>

#include "diagnostics/cros_healthd/routines/sensor/sensor_detail.h"
#include "diagnostics/cros_healthd/routines/sensor/sensor_existence_checker.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/cros_healthd/utils/callback_barrier.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// Frequency to read sensor sample.
constexpr double kSampleReadingFrequency = 5;

// Frequency to update the routine percentage.
constexpr base::TimeDelta kSensitiveSensorRoutineUpdatePeriod =
    base::Milliseconds(500);

// Routine timeout.
constexpr base::TimeDelta kSensitiveSensorRoutineTimeout = base::Seconds(20);

// Convert the result enum to `HardwarePresenceStatus`.
mojom::HardwarePresenceStatus Convert(
    SensorExistenceChecker::Result::State state) {
  switch (state) {
    case SensorExistenceChecker::Result::State::kPassed:
      return mojom::HardwarePresenceStatus::kMatched;
    case SensorExistenceChecker::Result::State::kSkipped:
      return mojom::HardwarePresenceStatus::kNotConfigured;
    case SensorExistenceChecker::Result::State::kMissing:
    case SensorExistenceChecker::Result::State::kUnexpected:
      return mojom::HardwarePresenceStatus::kNotMatched;
  }
}

}  // namespace

SensitiveSensorRoutine::SensitiveSensorRoutine(Context* const context)
    : context_(context),
      sensor_checker_{context->mojo_service(), context->system_config()} {
  CHECK(context);

  observer_receiver_set_.set_disconnect_handler(base::BindRepeating(
      []() { LOG(ERROR) << "Observer connection closed"; }));
}

SensitiveSensorRoutine::~SensitiveSensorRoutine() = default;

void SensitiveSensorRoutine::OnStart() {
  SetRunningState();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SensitiveSensorRoutine::OnTimeoutOccurred,
                     weak_ptr_factory_.GetWeakPtr()),
      kSensitiveSensorRoutineTimeout);

  RunNextStep();
}

void SensitiveSensorRoutine::RunNextStep() {
  step_ = static_cast<TestStep>(static_cast<int>(step_) + 1);
  start_ticks_ = base::TimeTicks::Now();

  switch (step_) {
    case TestStep::kInitialize:
      SetResultAndStop(base::unexpected("Unexpected flow in routine."));
      break;
    case TestStep::kFetchSensorsAndRunExistenceCheck:
      context_->mojo_service()->GetSensorService()->GetAllDeviceIds(
          base::BindOnce(&SensitiveSensorRoutine::HandleGetAllDeviceIdsResponse,
                         weak_ptr_factory_.GetWeakPtr()));
      break;
    case TestStep::kInitSensorDevices:
      if (pending_sensors_.empty()) {
        OnRoutineFinished();
        return;
      }
      InitSensorDevices();
      break;
    case TestStep::kReadingSample:
      UpdatePercentage();
      for (const auto& [sensor_id, _] : pending_sensors_) {
        auto remote =
            mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver>();
        observer_receiver_set_.Add(
            this, remote.InitWithNewPipeAndPassReceiver(), sensor_id);
        context_->mojo_service()
            ->GetSensorDevice(sensor_id)
            ->StartReadingSamples(std::move(remote));
      }
      break;
    case TestStep::kComplete:
      OnRoutineFinished();
      break;
  }
}

void SensitiveSensorRoutine::HandleGetAllDeviceIdsResponse(
    const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
        ids_types) {
  sensor_checker_.VerifySensorInfo(
      ids_types,
      base::BindOnce(&SensitiveSensorRoutine::HandleVerificationResponse,
                     weak_ptr_factory_.GetWeakPtr(), ids_types));
}

void SensitiveSensorRoutine::HandleVerificationResponse(
    const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
        ids_types,
    std::map<SensorType, SensorExistenceChecker::Result>
        existence_check_result) {
  existence_check_result_ = std::move(existence_check_result);
  if (existence_check_result_.empty()) {
    SetResultAndStop(
        base::unexpected("Routine failed to complete existence check."));
    return;
  }

  for (const auto& [sensor_id, sensor_types] : ids_types) {
    auto sensor = SensorDetail::Create(sensor_id, sensor_types);
    // Skip unsupported sensors.
    if (sensor) {
      pending_sensors_[sensor_id] = std::move(sensor);
    }
  }

  RunNextStep();
}

void SensitiveSensorRoutine::InitSensorDevices() {
  auto barrier = std::make_unique<CallbackBarrier>(
      /*on_success=*/base::BindOnce(&SensitiveSensorRoutine::RunNextStep,
                                    weak_ptr_factory_.GetWeakPtr()),
      /*on_error=*/base::BindOnce(
          &SensitiveSensorRoutine::SetResultAndStop,
          weak_ptr_factory_.GetWeakPtr(),
          base::unexpected("Routine failed to initialize sensor devices.")));
  for (const auto& [sensor_id, _] : pending_sensors_) {
    context_->mojo_service()->GetSensorDevice(sensor_id)->SetFrequency(
        kSampleReadingFrequency,
        base::BindOnce(&SensitiveSensorRoutine::HandleFrequencyResponse,
                       weak_ptr_factory_.GetWeakPtr(), sensor_id,
                       barrier->CreateDependencyClosure()));
  }
  barrier.reset();
}

void SensitiveSensorRoutine::HandleFrequencyResponse(
    int32_t sensor_id, base::OnceClosure on_init_finished, double frequency) {
  if (frequency <= 0.0) {
    LOG(ERROR) << "Failed to set frequency on sensor with ID: " << sensor_id;
    SetResultAndStop(base::unexpected("Routine failed to set frequency."));
    return;
  }

  context_->mojo_service()->GetSensorDevice(sensor_id)->GetAllChannelIds(
      base::BindOnce(&SensitiveSensorRoutine::HandleChannelIdsResponse,
                     weak_ptr_factory_.GetWeakPtr(), sensor_id,
                     std::move(on_init_finished)));
}

void SensitiveSensorRoutine::HandleChannelIdsResponse(
    int32_t sensor_id,
    base::OnceClosure on_init_finished,
    const std::vector<std::string>& channels) {
  auto channel_indices =
      pending_sensors_[sensor_id]->CheckRequiredChannelsAndGetIndices(channels);
  if (!channel_indices.has_value()) {
    LOG(ERROR) << "Failed to get required channels on sensor with ID: "
               << sensor_id;
    SetResultAndStop(
        base::unexpected("Routine failed to get required channels."));
    return;
  }

  context_->mojo_service()->GetSensorDevice(sensor_id)->SetChannelsEnabled(
      channel_indices.value(), true,
      base::BindOnce(&SensitiveSensorRoutine::HandleSetChannelsEnabledResponse,
                     weak_ptr_factory_.GetWeakPtr(), sensor_id,
                     std::move(on_init_finished)));
}

void SensitiveSensorRoutine::HandleSetChannelsEnabledResponse(
    int32_t sensor_id,
    base::OnceClosure on_init_finished,
    const std::vector<int32_t>& failed_indices) {
  if (!failed_indices.empty()) {
    LOG(ERROR) << "Failed to set channels enabled on sensor with ID: "
               << sensor_id;
    SetResultAndStop(
        base::unexpected("Routine failed to set channels enabled."));
    return;
  }
  std::move(on_init_finished).Run();
}

void SensitiveSensorRoutine::OnSampleUpdated(
    const base::flat_map<int32_t, int64_t>& sample) {
  if (step_ != TestStep::kReadingSample) {
    return;
  }
  const auto& sensor_id = observer_receiver_set_.current_context();
  const auto& sensor = pending_sensors_[sensor_id];

  for (const auto& [channel_indice, channel_value] : sample) {
    sensor->UpdateChannelSample(channel_indice, channel_value);
  }

  if (sensor->AllChannelsChecked()) {
    context_->mojo_service()->GetSensorDevice(sensor_id)->StopReadingSamples();

    // Store detail of passed sensor.
    passed_sensors_[sensor_id] = sensor->ToMojo();
    pending_sensors_.erase(sensor_id);
    observer_receiver_set_.Remove(observer_receiver_set_.current_receiver());

    if (pending_sensors_.empty()) {
      RunNextStep();
    }
  }
}

void SensitiveSensorRoutine::OnErrorOccurred(
    cros::mojom::ObserverErrorType type) {
  if (step_ != TestStep::kReadingSample) {
    return;
  }
  const auto& sensor_id = observer_receiver_set_.current_context();
  LOG(ERROR) << "Observer error occurred while reading sample: " << type
             << ", sensor ID: " << sensor_id;
  SetResultAndStop(
      base::unexpected("Observer error occurred while reading sample."));
}

void SensitiveSensorRoutine::UpdatePercentage() {
  int total_sensor_num =
      passed_sensors_.size() + pending_sensors_.size() + failed_sensors_.size();
  if (total_sensor_num == 0) {
    return;
  }

  double tested_sensor_percent =
      100.0 * (total_sensor_num - pending_sensors_.size()) / total_sensor_num;
  double running_time_ratio =
      std::min(1.0, (base::TimeTicks::Now() - start_ticks_) /
                        kSensitiveSensorRoutineTimeout);
  uint8_t new_percentage = tested_sensor_percent +
                           (100.0 - tested_sensor_percent) * running_time_ratio;

  if (new_percentage > state()->percentage && new_percentage < 100) {
    SetPercentage(new_percentage);
  }

  if (new_percentage < 100) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&SensitiveSensorRoutine::UpdatePercentage,
                       weak_ptr_factory_.GetWeakPtr()),
        kSensitiveSensorRoutineUpdatePeriod);
  }
}

void SensitiveSensorRoutine::OnTimeoutOccurred() {
  // No pending sensors, or number of pending sensors is inconsistent.
  if (pending_sensors_.empty() ||
      pending_sensors_.size() != observer_receiver_set_.size()) {
    LOG(ERROR) << "Mojo connection lost between Healthd and Iioservice";
    SetResultAndStop(base::unexpected("Mojo connection lost."));
    return;
  }

  // Sensor failed to pass the routine.
  for (const auto& [sensor_id, _] : pending_sensors_) {
    context_->mojo_service()->GetSensorDevice(sensor_id)->StopReadingSamples();

    // Store detail of failed sensor.
    failed_sensors_[sensor_id] = pending_sensors_[sensor_id]->ToMojo();
    if (pending_sensors_[sensor_id]->IsErrorOccurred()) {
      // SetResultAndStop(/*result=*/base::ok(false));
      SetResultAndStop(base::unexpected(
          "Routine failed to read sample from sensor device."));
      return;
    }
  }
  OnRoutineFinished();
}

void SensitiveSensorRoutine::OnRoutineFinished() {
  for (const auto& [_, result] : existence_check_result_) {
    if (result.state == SensorExistenceChecker::Result::State::kMissing ||
        result.state == SensorExistenceChecker::Result::State::kUnexpected) {
      SetResultAndStop(/*result=*/base::ok(false));
      return;
    }
  }
  SetResultAndStop(/*result=*/base::ok(failed_sensors_.empty()));
}

mojom::SensitiveSensorReportPtr SensitiveSensorRoutine::GetSensorReport(
    SensorType sensor) {
  auto report = mojom::SensitiveSensorReport::New();
  const auto& result = existence_check_result_[sensor];
  report->sensor_presence_status = Convert(result.state);

  for (const auto& sensor_id : result.sensor_ids) {
    if (passed_sensors_.contains(sensor_id)) {
      report->passed_sensors.push_back(passed_sensors_[sensor_id]->Clone());
    }
    if (failed_sensors_.contains(sensor_id)) {
      report->failed_sensors.push_back(failed_sensors_[sensor_id]->Clone());
    }
  }
  return report;
}

void SensitiveSensorRoutine::SetResultAndStop(
    base::expected<bool, std::string> result) {
  // Cancel all pending callbacks.
  weak_ptr_factory_.InvalidateWeakPtrs();
  // Clear sensor observers.
  observer_receiver_set_.Clear();
  if (!result.has_value()) {
    RaiseException(result.error());
    return;
  }

  auto detail = mojom::SensitiveSensorRoutineDetail::New();
  detail->base_accelerometer = GetSensorReport(SensorType::kBaseAccelerometer);
  detail->lid_accelerometer = GetSensorReport(SensorType::kLidAccelerometer);
  detail->base_gyroscope = GetSensorReport(SensorType::kBaseGyroscope);
  detail->lid_gyroscope = GetSensorReport(SensorType::kLidGyroscope);
  detail->base_magnetometer = GetSensorReport(SensorType::kBaseMagnetometer);
  detail->lid_magnetometer = GetSensorReport(SensorType::kLidMagnetometer);
  detail->base_gravity_sensor = GetSensorReport(SensorType::kBaseGravitySensor);
  detail->lid_gravity_sensor = GetSensorReport(SensorType::kLidGravitySensor);
  SetFinishedState(result.value(),
                   mojom::RoutineDetail::NewSensitiveSensor(std::move(detail)));
}

}  // namespace diagnostics
