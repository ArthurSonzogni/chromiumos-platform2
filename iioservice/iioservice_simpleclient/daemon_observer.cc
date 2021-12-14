// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/daemon_observer.h"

#include <utility>

#include <base/bind.h>

#include "iioservice/iioservice_simpleclient/observer_impl.h"

namespace iioservice {

DaemonObserver::DaemonObserver(int device_id,
                               cros::mojom::DeviceType device_type,
                               std::vector<std::string> channel_ids,
                               double frequency,
                               int timeout,
                               int samples)
    : device_id_(device_id),
      device_type_(device_type),
      channel_ids_(std::move(channel_ids)),
      frequency_(frequency),
      timeout_(timeout),
      samples_(samples),
      weak_ptr_factory_(this) {}

DaemonObserver::~DaemonObserver() = default;

void DaemonObserver::SetSensorClient() {
  sensor_client_ = ObserverImpl::Create(
      base::ThreadTaskRunnerHandle::Get(), device_id_, device_type_,
      std::move(channel_ids_), frequency_, timeout_, samples_,
      base::BindOnce(&DaemonObserver::OnMojoDisconnect,
                     weak_ptr_factory_.GetWeakPtr()));
}

}  // namespace iioservice
