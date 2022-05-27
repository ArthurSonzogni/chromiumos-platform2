// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_IIOSERVICE_SIMPLECLIENT_EVENTS_OBSERVER_H_
#define IIOSERVICE_IIOSERVICE_SIMPLECLIENT_EVENTS_OBSERVER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "iioservice/iioservice_simpleclient/observer.h"
#include "iioservice/mojo/cros_sensor_service.mojom.h"
#include "iioservice/mojo/sensor.mojom.h"

namespace iioservice {

class EventsObserver final : public Observer,
                             public cros::mojom::SensorDeviceEventsObserver {
 public:
  using ScopedEventsObserver =
      std::unique_ptr<EventsObserver, decltype(&SensorClientDeleter)>;

  // The task runner should be the same as the one provided to SensorClient.
  static ScopedEventsObserver Create(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      int device_id,
      cros::mojom::DeviceType device_type,
      std::vector<int> event_indices,
      int events,
      OnMojoDisconnectCallback on_mojo_disconnect_callback,
      QuitCallback quit_callback);

  // cros::mojom::SensorDeviceEventsObserver overrides:
  void OnEventUpdated(cros::mojom::IioEventPtr event) override;
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override;

 private:
  EventsObserver(scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
                 int device_id,
                 cros::mojom::DeviceType device_type,
                 std::vector<int> event_indices,
                 int events,
                 OnMojoDisconnectCallback on_mojo_disconnect_callback,
                 QuitCallback quit_callback);

  // SensorClient overrides:
  void Reset() override;

  mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver> GetRemote();

  void GetSensorDevice() override;

  void GetAllChannelIds();
  void GetAllChannelIdsCallback(const std::vector<std::string>& iio_chn_ids);

  void StartReading();

  void SetEventsEnabledCallback(const std::vector<int32_t>& failed_indices);

  std::vector<int> event_indices_;

  mojo::Receiver<cros::mojom::SensorDeviceEventsObserver> receiver_;

  base::WeakPtrFactory<EventsObserver> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_IIOSERVICE_SIMPLECLIENT_EVENTS_OBSERVER_H_
