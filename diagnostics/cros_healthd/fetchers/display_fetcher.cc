// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include "diagnostics/cros_healthd/fetchers/display_fetcher.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

void DisplayFetcher::FetchDisplayInfo(
    DisplayFetcher::FetchDisplayInfoCallback&& callback) {
  auto libdrm_util = context_->CreateLibdrmUtil();
  if (!libdrm_util->Initialize()) {
    std::move(callback).Run(mojo_ipc::DisplayResult::NewError(
        CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                               "Failed to initialize libdrm_util object.")));
    return;
  }

  auto display_info = mojo_ipc::DisplayInfo::New();

  // Fetch EmbeddedDisplayInfo.
  auto edp_info = mojo_ipc::EmbeddedDisplayInfo::New();
  auto edp_connector_id = libdrm_util->GetEmbeddedDisplayConnectorID();
  libdrm_util->FillPrivacyScreenInfo(edp_connector_id,
                                     &edp_info->privacy_screen_supported,
                                     &edp_info->privacy_screen_enabled);

  display_info->edp_info = std::move(edp_info);

  std::move(callback).Run(
      mojo_ipc::DisplayResult::NewDisplayInfo(std::move(display_info)));
}

}  // namespace diagnostics
