// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TPM_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TPM_FETCHER_H_

#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/memory/weak_ptr.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

class TpmFetcher final : public BaseFetcher {
 public:
  using BaseFetcher::BaseFetcher;

  using FetchTpmInfoCallback =
      base::OnceCallback<void(chromeos::cros_healthd::mojom::TpmResultPtr)>;
  // Returns a structure with either the device's tpm data or the error
  // that occurred fetching the information.
  void FetchTpmInfo(FetchTpmInfoCallback&& callback);

 private:
  void SendError(const std::string& message);
  void SendResult(chromeos::cros_healthd::mojom::TpmResultPtr result);

 private:
  // Pending callbacks to be fulfilled.
  std::vector<FetchTpmInfoCallback> pending_callbacks_;
  // The fetched info.
  chromeos::cros_healthd::mojom::TpmInfoPtr info_;
  // Must be the last member of the class, so that it's destroyed first when an
  // instance of the class is destroyed. This will prevent any outstanding
  // callbacks from being run and segfaulting.
  base::WeakPtrFactory<TpmFetcher> weak_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_TPM_FETCHER_H_
