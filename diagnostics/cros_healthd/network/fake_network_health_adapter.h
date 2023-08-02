// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_NETWORK_FAKE_NETWORK_HEALTH_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_NETWORK_FAKE_NETWORK_HEALTH_ADAPTER_H_

#include "diagnostics/cros_healthd/network/network_health_adapter.h"
#include "diagnostics/mojom/external/network_health.mojom.h"

namespace diagnostics {

// Fake implementation of the NetworkHealthAdapter interface used for testing.
class FakeNetworkHealthAdapter final : public NetworkHealthAdapter {
 public:
  FakeNetworkHealthAdapter();
  FakeNetworkHealthAdapter(const FakeNetworkHealthAdapter&) = delete;
  FakeNetworkHealthAdapter& operator=(const FakeNetworkHealthAdapter&) = delete;
  ~FakeNetworkHealthAdapter() override;

  // NetworkHealthAdapterInterface overrides:
  // Unimplemented. The fake implementation is not going to use the service
  // remote, so nothing needs to be done.
  void SetServiceRemote(
      mojo::PendingRemote<chromeos::network_health::mojom::NetworkHealthService>
          remote) override {}
  void AddObserver(mojo::PendingRemote<
                   chromeos::network_health::mojom::NetworkEventsObserver>
                       observer) override {}
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_NETWORK_FAKE_NETWORK_HEALTH_ADAPTER_H_
