// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_INTERFACE_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_INTERFACE_FETCHER_H_

#include <base/callback_forward.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <string>
#include <vector>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "mojo/cros_healthd_executor.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {
constexpr char kRelativeWirelessPowerSchemePath[] =
    "sys/module/iwlmvm/parameters/power_scheme";

class NetworkInterfaceFetcher final : public BaseFetcher {
 public:
  using FetchNetworkInterfaceInfoCallback = base::OnceCallback<void(
      chromeos::cros_healthd::mojom::NetworkInterfaceResultPtr)>;

  using BaseFetcher::BaseFetcher;
  void FetchNetworkInterfaceInfo(FetchNetworkInterfaceInfoCallback callback);

 private:
  void CreateResultToSendBack(void);

  void CreateErrorToSendBack(
      chromeos::cros_healthd::mojom::ErrorType error_type,
      const std::string& message);

  void SendBackResult(
      chromeos::cros_healthd::mojom::NetworkInterfaceResultPtr result);

  void FetchWirelessInterfaceInfo(void);

  void HandleInterfaceNameAndExecuteGetLink(
      chromeos::cros_healthd_executor::mojom::ProcessResultPtr result);

  void HandleLinkAndExecuteIwExecuteGetInfo(
      chromeos::cros_healthd_executor::mojom::ProcessResultPtr result);

  void HandleInfoAndExecuteGetScanDump(
      chromeos::cros_healthd_executor::mojom::ProcessResultPtr result);

  void HandleScanDump(
      chromeos::cros_healthd_executor::mojom::ProcessResultPtr result);

  chromeos::cros_healthd::mojom::WirelessInterfaceInfoPtr wireless_info_;
  std::vector<FetchNetworkInterfaceInfoCallback> pending_callbacks_;
  base::WeakPtrFactory<NetworkInterfaceFetcher> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_NETWORK_INTERFACE_FETCHER_H_
