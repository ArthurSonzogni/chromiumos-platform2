// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_SENSOR_DEVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_SENSOR_DEVICE_H_

#include <string>
#include <vector>

#include <iioservice/mojo/sensor.mojom.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

namespace diagnostics {

// Fake implementation of SensorDevice.
class FakeSensorDevice : public cros::mojom::SensorDevice {
 public:
  explicit FakeSensorDevice(const std::optional<std::string>& name,
                            const std::optional<std::string>& location);
  FakeSensorDevice(const FakeSensorDevice&) = delete;
  FakeSensorDevice& operator=(const FakeSensorDevice&) = delete;
  ~FakeSensorDevice() override = default;

  // Getter for the mojo receiver.
  mojo::Receiver<cros::mojom::SensorDevice>& receiver() { return receiver_; }

 private:
  // cros::mojom::SensorDevice overrides.
  void SetTimeout(uint32_t timeout) override;
  void GetAttributes(const std::vector<std::string>& attr_names,
                     GetAttributesCallback callback) override;
  void SetFrequency(double frequency, SetFrequencyCallback callback) override;
  void StartReadingSamples(
      mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> observer)
      override;
  void StopReadingSamples() override;
  void GetAllChannelIds(GetAllChannelIdsCallback callback) override;
  void SetChannelsEnabled(const std::vector<int32_t>& iio_chn_indices,
                          bool en,
                          SetChannelsEnabledCallback callback) override;
  void GetChannelsEnabled(const std::vector<int32_t>& iio_chn_indices,
                          GetChannelsEnabledCallback callback) override;
  void GetChannelsAttributes(const std::vector<int32_t>& iio_chn_indices,
                             const std::string& attr_name,
                             GetChannelsAttributesCallback callback) override;
  void GetAllEvents(GetAllEventsCallback callback) override;
  void SetEventsEnabled(const std::vector<int32_t>& iio_event_indices,
                        bool en,
                        SetEventsEnabledCallback callback) override;
  void GetEventsEnabled(const std::vector<int32_t>& iio_event_indices,
                        GetEventsEnabledCallback callback) override;
  void GetEventsAttributes(const std::vector<int32_t>& iio_event_indices,
                           const std::string& attr_name,
                           GetEventsAttributesCallback callback) override;
  void StartReadingEvents(
      mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver> observer)
      override;
  void StopReadingEvents() override;

  // Mojo receiver for binding pipe.
  mojo::Receiver<cros::mojom::SensorDevice> receiver_{this};
  // Sensor attributes.
  std::optional<std::string> sensor_name_;
  std::optional<std::string> sensor_location_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_SENSOR_DEVICE_H_
