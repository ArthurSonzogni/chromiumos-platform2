// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_FAKE_SENSOR_DEVICE_H_
#define POWER_MANAGER_POWERD_SYSTEM_FAKE_SENSOR_DEVICE_H_

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <iioservice/mojo/sensor.mojom.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

namespace power_manager {
namespace system {

class FakeSensorDevice : public cros::mojom::SensorDevice {
 public:
  FakeSensorDevice(bool is_color_sensor,
                   std::optional<std::string> name,
                   std::optional<std::string> location);

  mojo::ReceiverId AddReceiver(
      mojo::PendingReceiver<cros::mojom::SensorDevice> pending_receiver);
  bool HasReceivers() const;
  void ClearReceiverWithReason(
      cros::mojom::SensorDeviceDisconnectReason reason =
          cros::mojom::SensorDeviceDisconnectReason::IIOSERVICE_CRASHED,
      const std::string& description = "");

  void ResetObserverRemote(mojo::ReceiverId id);

  void SetAttribute(std::string attr_name, std::string value);

  // Implementation of cros::mojom::SensorDevice.
  void SetTimeout(uint32_t timeout) override {}
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

  bool is_color_sensor_;
  std::map<std::string, std::string> attributes_;

  std::map<mojo::ReceiverId,
           mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver>>
      observers_;

  mojo::ReceiverSet<cros::mojom::SensorDevice> receiver_set_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_FAKE_SENSOR_DEVICE_H_
