// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/check.h>

#include "diagnostics/cros_healthd/fetchers/bus_fetcher.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

mojo_ipc::BusResultPtr BusFetcher::FetchBusDevices() {
  return mojo_ipc::BusResult::NewError(CreateAndLogProbeError(
      mojo_ipc::ErrorType::kServiceUnavailable, "Not yet implemented."));
}

}  // namespace diagnostics
