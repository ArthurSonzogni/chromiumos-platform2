// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_DELEGATE_MOJO_H_
#define POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_DELEGATE_MOJO_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/containers/flat_map.h>
#include <base/gtest_prod_util.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <base/optional.h>
#include <base/sequence_checker.h>
#include <base/stl_util.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/sensor.mojom.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/ambient_light_observer.h"
#include "power_manager/powerd/system/ambient_light_sensor.h"
#include "power_manager/powerd/system/async_file_reader.h"

namespace power_manager {
namespace system {

class AmbientLightSensorDelegateMojo
    : public AmbientLightSensorDelegate,
      public cros::mojom::SensorDeviceSamplesObserver {
 public:
  static std::string GetChannelIlluminanceColorId(const char* rgb_name);

  static std::unique_ptr<AmbientLightSensorDelegateMojo> Create(
      int iio_device_id,
      mojo::Remote<cros::mojom::SensorDevice> remote,
      bool enable_color_support = false);

  AmbientLightSensorDelegateMojo(const AmbientLightSensorDelegateMojo&) =
      delete;
  AmbientLightSensorDelegateMojo& operator=(
      const AmbientLightSensorDelegateMojo&) = delete;
  ~AmbientLightSensorDelegateMojo() override;

  // AmbientLightSensorDelegate overrides:
  bool IsColorSensor() const override;
  base::FilePath GetIlluminancePath() const override;

  // cros::mojom::SensorDeviceSamplesObserver overrides:
  void OnSampleUpdated(const base::flat_map<int32_t, int64_t>& sample) override;
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override;

 private:
  // Allow the test to construct the class directly.
  friend class AmbientLightSensorDelegateMojoTest;
  friend class FakeManager;
  friend class FakeSensorDevice;

  static constexpr uint32_t kNumFailedReadsBeforeGivingUp = 20;
  // Number of successful reads to recover |num_failed_reads_| by one.
  static constexpr uint32_t kNumRecoveryReads = 2;

  AmbientLightSensorDelegateMojo(int iio_device_id,
                                 mojo::Remote<cros::mojom::SensorDevice> remote,
                                 bool enable_color_support);

  void Reset();

  void GetAllChannelIds();
  void GetAllChannelIdsCallback(
      const std::vector<std::string>& iio_channel_ids);
  void StartReading();

  mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> GetRemote();

  // Extracts the lux value of the specific color axis |index| from |sample|,
  // which is from OnSampleUpdated.
  base::Optional<int> GetColorValue(
      const base::flat_map<int32_t, int64_t>& sample, ChannelType type);
  // Gets the color temperature with |sample| by calling
  // AmbientLightSensorDelegate::CalculateColorTemperature.
  base::Optional<int> GetColorTemperature(
      const base::flat_map<int32_t, int64_t>& sample);

  void OnObserverDisconnect();

  void SetFrequencyCallback(double result_freq);
  void SetChannelsEnabledCallback(const std::vector<int32_t>& failed_indices);

  void ReadError();

  int iio_device_id_;
  mojo::Remote<cros::mojom::SensorDevice> sensor_device_remote_;

  // Boolean to indicate if color support should be enabled on this ambient
  // light sensor. Color support should only be enabled if sensor is properly
  // calibrated. Only search for color support if true.
  bool enable_color_support_;

  std::set<std::string> channel_ids_to_enable_;

  // True if |enable_color_support_| and all color channel enabled.
  bool color_channels_enabled_ = false;

  // The list of channel ids retrieved from iioservice. Use channels' indices in
  // this list to identify them.
  std::vector<std::string> iio_channel_ids_;
  // The indices of channels in |iio_channel_ids_| to query data from. This is a
  // combination of indices in |color_indices_| and |illuminance_index_|, if
  // applicable.
  // Ex: [1, 2, 3, 0].
  std::vector<int32_t> channel_indices_;
  // The channel index of channel: illuminance.
  base::Optional<int32_t> illuminance_index_ = base::nullopt;
  // The channel indices of red, green, and blue channels respectively.
  std::map<ChannelType, int32_t> color_indices_;

  // Number of failed reads. Triggers an error if it reaches
  // kNumFailedReadsBeforeGivingUp.
  uint32_t num_failed_reads_ = 0;
  // Every time this reaches kNumRecoveryReads, |num_failed_reads_| is
  // decremented by 1.
  uint32_t num_recovery_reads_ = 0;

  mojo::Receiver<cros::mojom::SensorDeviceSamplesObserver> receiver_;

  base::WeakPtrFactory<AmbientLightSensorDelegateMojo> weak_factory_{this};

  SEQUENCE_CHECKER(sequence_checker_);

  FRIEND_TEST_ALL_PREFIXES(AmbientLightSensorDelegateMojoTest,
                           GiveUpAfterTooManyFailures);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_AMBIENT_LIGHT_SENSOR_DELEGATE_MOJO_H_
