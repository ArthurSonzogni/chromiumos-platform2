// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_PROXIMITY_EVENTS_OBSERVER_H_
#define POWER_MANAGER_POWERD_SYSTEM_PROXIMITY_EVENTS_OBSERVER_H_

#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <optional>
#include <iioservice/mojo/sensor.mojom.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "power_manager/powerd/system/user_proximity_observer.h"

namespace power_manager {
namespace system {

class ProximityEventsObserver : public cros::mojom::SensorDeviceEventsObserver {
 public:
  ProximityEventsObserver(int iio_device_id,
                          std::vector<int> event_indices,
                          mojo::Remote<cros::mojom::SensorDevice> remote,
                          base::ObserverList<UserProximityObserver>* observers);

  ProximityEventsObserver(const ProximityEventsObserver&) = delete;
  ProximityEventsObserver& operator=(const ProximityEventsObserver&) = delete;
  ~ProximityEventsObserver() override;

  // cros::mojom::SensorDeviceEventsObserver overrides:
  void OnEventUpdated(cros::mojom::IioEventPtr event) override;
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override;

 private:
  void Reset();

  mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver> GetRemote();

  void OnObserverDisconnect();

  void GetAllEventsCallback(std::vector<cros::mojom::IioEventPtr> iio_events);

  void StartReading();

  int iio_device_id_;
  std::vector<int> event_indices_;
  mojo::Remote<cros::mojom::SensorDevice> sensor_device_remote_;
  base::ObserverList<UserProximityObserver>* observers_;

  mojo::Receiver<cros::mojom::SensorDeviceEventsObserver> receiver_{this};
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_PROXIMITY_EVENTS_OBSERVER_H_
