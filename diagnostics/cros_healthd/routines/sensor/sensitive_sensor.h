// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSITIVE_SENSOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSITIVE_SENSOR_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <iioservice/mojo/sensor.mojom.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "diagnostics/cros_healthd/routines/noninteractive_routine_control.h"
#include "diagnostics/cros_healthd/routines/sensor/sensor_existence_checker.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {
class Context;
class SensorDetail;

// The sensitive sensor routine checks that the device's sensors are working
// correctly by acquiring dynamic sensor sample data without user interaction.
class SensitiveSensorRoutine final
    : public NoninteractiveRoutineControl,
      public cros::mojom::SensorDeviceSamplesObserver {
 public:
  explicit SensitiveSensorRoutine(Context* context);
  SensitiveSensorRoutine(const SensitiveSensorRoutine&) = delete;
  SensitiveSensorRoutine& operator=(const SensitiveSensorRoutine&) = delete;
  ~SensitiveSensorRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

  // cros::mojom::SensorDeviceSamplesObserver overrides:
  void OnSampleUpdated(const base::flat_map<int32_t, int64_t>& sample) override;
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override;

 private:
  void RunNextStep();

  // Handle the response of sensor ids and types from the sensor service.
  void HandleGetAllDeviceIdsResponse(
      const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
          ids_types);

  // Handle the response of sensor verification from the config checker.
  void HandleVerificationResponse(
      const base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>>&
          ids_types,
      std::map<SensorType, SensorExistenceChecker::Result>
          existence_check_result);

  // Initialize sensor devices to read samples.
  void InitSensorDevices();

  // Handle the response of frequency from the sensor device after setting
  // reading frequency.
  void HandleFrequencyResponse(int32_t sensor_id,
                               base::OnceClosure on_init_finished,
                               double frequency);

  // Handle the response of channels from the sensor device.
  void HandleChannelIdsResponse(int32_t sensor_id,
                                base::OnceClosure on_init_finished,
                                const std::vector<std::string>& channels);

  // Handle the response of failed channel indices from the sensor device after
  // setting channels enabled. Then invoke `on_init_finished`.
  void HandleSetChannelsEnabledResponse(
      int32_t sensor_id,
      base::OnceClosure on_init_finished,
      const std::vector<int32_t>& failed_indices);

  // Update the routine percentage.
  void UpdatePercentage();

  // Stop all sensor devices and complete the routine on timeout.
  void OnTimeoutOccurred();

  // Routine completion function.
  void OnRoutineFinished();

  // Get the sensor report pointer by parsing the result of a sensor type.
  ash::cros_healthd::mojom::SensitiveSensorReportPtr GetSensorReport(
      SensorType sensor);

  // Set the routine result and stop other callbacks.
  void SetResultAndStop(base::expected<bool, std::string> result);

  enum class TestStep : int32_t {
    kInitialize = 0,
    kFetchSensorsAndRunExistenceCheck = 1,
    kInitSensorDevices = 2,
    kReadingSample = 3,
    kComplete = 4,  // Should be the last one. New step should be added before
                    // it.
  };
  TestStep step_ = TestStep::kInitialize;

  // Unowned pointer that should outlive this instance.
  Context* const context_;

  // Used to check if any sensor is missing by iioservice by checking static
  // configuration.
  SensorExistenceChecker sensor_checker_;

  // Details of the result from |sensor_checker_|.
  std::map<SensorType, SensorExistenceChecker::Result> existence_check_result_;

  // First is unfinished sensor id and second is the sensor detail. Also used to
  // handle timeout and calculate the percentage.
  std::map<int32_t, std::unique_ptr<SensorDetail>> pending_sensors_;

  // Details of the passed sensors and failed sensors. Also used to calculate
  // the percentage.
  std::map<int32_t, ash::cros_healthd::mojom::SensitiveSensorInfoPtr>
      passed_sensors_;
  std::map<int32_t, ash::cros_healthd::mojom::SensitiveSensorInfoPtr>
      failed_sensors_;

  // Routine start time, used to calculate the progress percentage.
  base::TimeTicks start_ticks_;

  // Mojo receiver set for binding pipe, which context is sensor id.
  mojo::ReceiverSet<cros::mojom::SensorDeviceSamplesObserver, int32_t>
      observer_receiver_set_;

  // Must be the last class member.
  base::WeakPtrFactory<SensitiveSensorRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SENSOR_SENSITIVE_SENSOR_H_
