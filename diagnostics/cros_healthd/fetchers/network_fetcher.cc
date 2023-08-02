// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/network_fetcher.h"

#include <utility>

#include <base/functional/callback.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/mojom/external/network_health_types.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace cros_healthd_ipc = ::ash::cros_healthd::mojom;
namespace network_health_ipc = ::chromeos::network_health::mojom;

// Forwards the response from Chrome's NetworkHealthService to the caller.
void HandleNetworkHealthStateResponse(
    base::OnceCallback<void(cros_healthd_ipc::NetworkResultPtr)> callback,
    network_health_ipc::NetworkHealthStatePtr result) {
  auto info =
      cros_healthd_ipc::NetworkResult::NewNetworkHealth(std::move(result));
  std::move(callback).Run(std::move(info));
}

}  // namespace

void FetchNetworkInfo(Context* context, FetchNetworkInfoCallback callback) {
  auto* network_health = context->mojo_service()->GetNetworkHealth();

  if (!network_health) {
    std::move(callback).Run(cros_healthd_ipc::NetworkResult::NewError(
        CreateAndLogProbeError(cros_healthd_ipc::ErrorType::kServiceUnavailable,
                               "Network Health Service unavailable")));
    return;
  }

  network_health->GetHealthSnapshot(
      base::BindOnce(&HandleNetworkHealthStateResponse, std::move(callback)));
}

}  // namespace diagnostics
