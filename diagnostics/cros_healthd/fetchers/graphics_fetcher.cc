// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include "diagnostics/cros_healthd/fetchers/graphics_fetcher.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

mojo_ipc::GraphicsResultPtr GraphicsFetcher::FetchGraphicsInfo() {
  return mojo_ipc::GraphicsResult::NewError(CreateAndLogProbeError(
      mojo_ipc::ErrorType::kServiceUnavailable, "Not implemented."));
}

}  // namespace diagnostics
