// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/fetchers/display_fetcher.h"

#include <memory>
#include <utility>
#include <vector>

#include "diagnostics/cros_healthd/delegate/utils/display_util.h"
#include "diagnostics/cros_healthd/delegate/utils/display_util_factory.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

mojom::DisplayResultPtr GetDisplayInfo(
    DisplayUtilFactory* display_util_factory) {
  std::unique_ptr<DisplayUtil> display_util = display_util_factory->Create();
  if (!display_util) {
    return mojom::DisplayResult::NewError(
        mojom::ProbeError::New(mojom::ErrorType::kSystemUtilityError,
                               "Failed to create DisplayUtil object."));
  }

  auto display_info = mojom::DisplayInfo::New();
  display_info->embedded_display = display_util->GetEmbeddedDisplayInfo();

  std::vector<uint32_t> connector_ids =
      display_util->GetExternalDisplayConnectorIDs();
  if (connector_ids.size() != 0) {
    std::vector<mojom::ExternalDisplayInfoPtr> external_display_infos;
    for (const auto& connector_id : connector_ids) {
      external_display_infos.push_back(
          display_util->GetExternalDisplayInfo(connector_id));
    }
    display_info->external_displays = std::move(external_display_infos);
  }

  return mojom::DisplayResult::NewDisplayInfo(std::move(display_info));
}

}  // namespace diagnostics
