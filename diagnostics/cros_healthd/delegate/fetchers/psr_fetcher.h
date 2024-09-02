// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_PSR_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_PSR_FETCHER_H_

#include "diagnostics/cros_healthd/delegate/utils/psr_cmd.h"
#include "diagnostics/cros_healthd/mojom/executor.mojom.h"

namespace diagnostics {

// Exported for testing.
namespace internal {

::ash::cros_healthd::mojom::PsrInfo::LogState ConvertLogStateToMojo(
    psr::LogState log_state);

::ash::cros_healthd::mojom::PsrEvent::EventType ConvertPsrEventTypeToMojo(
    psr::EventType event_type);

}  // namespace internal

// Returns a structure with either the PSR info or the error that occurred
// fetching the information.
::ash::cros_healthd::mojom::GetPsrResultPtr FetchPsrInfo();

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_PSR_FETCHER_H_
