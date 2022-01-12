// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/proximity_events_observer.h"

#include <utility>

#include <base/functional/bind.h>

namespace power_manager {
namespace system {

ProximityEventsObserver::ProximityEventsObserver(
    int iio_device_id,
    std::vector<int> event_indices,
    mojo::Remote<cros::mojom::SensorDevice> remote,
    base::ObserverList<UserProximityObserver>* observers)
    : iio_device_id_(iio_device_id),
      event_indices_(std::move(event_indices)),
      sensor_device_remote_(std::move(remote)),
      observers_(observers) {
  DCHECK(sensor_device_remote_.is_bound());

  sensor_device_remote_->GetAllEvents(base::BindOnce(
      &ProximityEventsObserver::GetAllEventsCallback, base::Unretained(this)));
}

ProximityEventsObserver::~ProximityEventsObserver() = default;

void ProximityEventsObserver::OnEventUpdated(cros::mojom::IioEventPtr event) {
  UserProximity proximity = UserProximity::UNKNOWN;
  switch (event->direction) {
    case cros::mojom::IioEventDirection::IIO_EV_DIR_RISING:
      proximity = UserProximity::FAR;
      break;
    case cros::mojom::IioEventDirection::IIO_EV_DIR_FALLING:
      proximity = UserProximity::NEAR;
      break;
    default:
      LOG(ERROR) << "Unknown proximity value " << event->direction;
      return;
  }

  // This log is also used for tast-test: hardware.SensorActivity
  LOG(INFO) << "User proximity: "
            << (proximity == UserProximity::FAR ? "Far" : "Near");
  for (auto& observer : *observers_)
    observer.OnProximityEvent(iio_device_id_, proximity);
}

void ProximityEventsObserver::OnErrorOccurred(
    cros::mojom::ObserverErrorType type) {
  switch (type) {
    case cros::mojom::ObserverErrorType::ALREADY_STARTED:
      LOG(ERROR) << "Device " << iio_device_id_
                 << ": Another observer has already started to read events";
      Reset();
      break;

    case cros::mojom::ObserverErrorType::NO_ENABLED_CHANNELS:
      LOG(ERROR) << "Device " << iio_device_id_
                 << ": Observer started with no events enabled";
      Reset();
      break;

    case cros::mojom::ObserverErrorType::GET_FD_FAILED:
      LOG(ERROR) << "Device " << iio_device_id_
                 << ": Failed to get the device's fd to poll on";
      break;

    case cros::mojom::ObserverErrorType::READ_FAILED:
      LOG(ERROR) << "Device " << iio_device_id_ << ": Failed to read a sample";
      break;

    default:
      LOG(ERROR) << "Device " << iio_device_id_ << ": error "
                 << static_cast<int>(type);
      break;
  }
}

void ProximityEventsObserver::Reset() {
  LOG(INFO) << "Resetting AmbientLightSensorDelegateMojo";

  receiver_.reset();
  sensor_device_remote_.reset();
}

mojo::PendingRemote<cros::mojom::SensorDeviceEventsObserver>
ProximityEventsObserver::GetRemote() {
  auto remote = receiver_.BindNewPipeAndPassRemote();
  receiver_.set_disconnect_handler(base::BindOnce(
      &ProximityEventsObserver::OnObserverDisconnect, base::Unretained(this)));

  return remote;
}

void ProximityEventsObserver::OnObserverDisconnect() {
  LOG(ERROR) << "OnObserverDisconnect error, assuming IIO Service crashes and "
                "waiting for it to relaunch";
  // Don't reset |sensor_device_remote_| so that UserProximityWatcherMojo can
  // get the disconnection.
  receiver_.reset();
}

void ProximityEventsObserver::GetAllEventsCallback(
    std::vector<cros::mojom::IioEventPtr> iio_events) {
  for (auto it = event_indices_.begin(); it != event_indices_.end();) {
    if (*it >= iio_events.size()) {
      LOG(WARNING) << "Invalid event index: " << *it;
      it = event_indices_.erase(it);
    } else {
      ++it;
    }
  }

  if (event_indices_.empty()) {
    LOG(ERROR)
        << "No event index to be enabled. Resetting ProximityEventsObserver.";
    Reset();
    return;
  }

  StartReading();
}

void ProximityEventsObserver::StartReading() {
  sensor_device_remote_->StartReadingEvents(event_indices_, GetRemote());
}

}  // namespace system
}  // namespace power_manager
