// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

#include <utility>

namespace diagnostics {

namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

void DisplayFetcher::FetchDisplayInfo(
    DisplayFetcher::FetchDisplayInfoCallback&& callback) {
  std::move(callback).Run(
      mojo_ipc::DisplayResult::NewError(CreateAndLogProbeError(
          mojo_ipc::ErrorType::kServiceUnavailable, "Not implemented.")));
}

}  // namespace diagnostics
