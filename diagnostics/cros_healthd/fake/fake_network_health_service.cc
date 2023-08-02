// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_network_health_service.h"

#include <utility>

#include <base/notreached.h>

#include "diagnostics/mojom/external/network_health.mojom.h"
#include "diagnostics/mojom/external/network_health_types.mojom.h"

namespace diagnostics {

FakeNetworkHealthService::FakeNetworkHealthService() = default;

FakeNetworkHealthService::~FakeNetworkHealthService() = default;

void FakeNetworkHealthService::SetHealthSnapshotResponse(
    chromeos::network_health::mojom::NetworkHealthStatePtr
        network_health_state) {
  network_health_state_ = std::move(network_health_state);
}

void FakeNetworkHealthService::AddObserver(
    mojo::PendingRemote<chromeos::network_health::mojom::NetworkEventsObserver>
        observer) {
  NOTIMPLEMENTED();
}

void FakeNetworkHealthService::GetNetworkList(GetNetworkListCallback callback) {
  NOTIMPLEMENTED();
}

void FakeNetworkHealthService::GetHealthSnapshot(
    GetHealthSnapshotCallback callback) {
  std::move(callback).Run(network_health_state_.Clone());
}

}  // namespace diagnostics
