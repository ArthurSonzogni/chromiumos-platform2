// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_INTERFACE_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_INTERFACE_FETCHER_H_

#include <base/callback_forward.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <string>
#include <vector>

#include "diagnostics/cros_healthd/executor/mojom/executor.mojom.h"
#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace mojom = chromeos::cros_healthd::mojom;

constexpr char kRelativeWirelessPowerSchemePath[] =
    "sys/module/iwlmvm/parameters/power_scheme";

class NetworkInterfaceFetcher final : public BaseFetcher {
 public:
  using FetchNetworkInterfaceInfoCallback =
      base::OnceCallback<void(mojom::NetworkInterfaceResultPtr)>;

  using BaseFetcher::BaseFetcher;
  void FetchNetworkInterfaceInfo(FetchNetworkInterfaceInfoCallback callback);

 private:
  void CreateResultToSendBack(void);

  void CreateErrorToSendBack(mojom::ErrorType error_type,
                             const std::string& message);

  void SendBackResult(mojom::NetworkInterfaceResultPtr result);

  void FetchWirelessInterfaceInfo(void);

  void HandleInterfaceNameAndExecuteGetLink(
      mojom::ExecutedProcessResultPtr result);

  void HandleLinkAndExecuteIwExecuteGetInfo(
      mojom::ExecutedProcessResultPtr result);

  void HandleInfoAndExecuteGetScanDump(mojom::ExecutedProcessResultPtr result);

  void HandleScanDump(mojom::ExecutedProcessResultPtr result);

  mojom::WirelessInterfaceInfoPtr wireless_info_;
  std::vector<FetchNetworkInterfaceInfoCallback> pending_callbacks_;
  base::WeakPtrFactory<NetworkInterfaceFetcher> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_INTERFACE_FETCHER_H_
