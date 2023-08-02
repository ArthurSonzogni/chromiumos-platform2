// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_NETWORK_HEALTH_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_NETWORK_HEALTH_SERVICE_H_

#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "diagnostics/mojom/external/network_health.mojom.h"
#include "diagnostics/mojom/external/network_health_types.mojom.h"

namespace diagnostics {

// Fake implementation of NetworkHealthService.
class FakeNetworkHealthService
    : public chromeos::network_health::mojom::NetworkHealthService {
 public:
  FakeNetworkHealthService();
  FakeNetworkHealthService(const FakeNetworkHealthService&) = delete;
  FakeNetworkHealthService& operator=(const FakeNetworkHealthService&) = delete;
  ~FakeNetworkHealthService() override;

  // Modifiers.
  mojo::Receiver<chromeos::network_health::mojom::NetworkHealthService>&
  receiver() {
    return receiver_;
  }

  void SetHealthSnapshotResponse(
      chromeos::network_health::mojom::NetworkHealthStatePtr
          network_health_state);

 private:
  // chromeos::network_health::mojom::NetworkHealthService overrides.
  void AddObserver(mojo::PendingRemote<
                   chromeos::network_health::mojom::NetworkEventsObserver>
                       observer) override;
  void GetNetworkList(GetNetworkListCallback callback) override;
  void GetHealthSnapshot(GetHealthSnapshotCallback callback) override;

  chromeos::network_health::mojom::NetworkHealthStatePtr network_health_state_;

  // Mojo receiver for binding pipe.
  mojo::Receiver<chromeos::network_health::mojom::NetworkHealthService>
      receiver_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_FAKE_NETWORK_HEALTH_SERVICE_H_
