// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/tpm_fetcher.h"

#include <string>
#include <utility>

#include <base/callback.h>
#include <base/check.h>
#include <brillo/errors/error.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {
namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

void TpmFetcher::SendError(const std::string& message) {
  SendResult(mojo_ipc::TpmResult::NewError(CreateAndLogProbeError(
      mojo_ipc::ErrorType::kServiceUnavailable, message)));
}

void TpmFetcher::SendResult(
    chromeos::cros_healthd::mojom::TpmResultPtr result) {
  // Invalid all weak ptrs to prevent other callbacks to be run.
  weak_factory_.InvalidateWeakPtrs();
  if (pending_callbacks_.empty())
    return;
  for (size_t i = 1; i < pending_callbacks_.size(); ++i) {
    std::move(pending_callbacks_[i]).Run(result.Clone());
  }
  std::move(pending_callbacks_[0]).Run(std::move(result));
  pending_callbacks_.clear();
}

void TpmFetcher::FetchTpmInfo(TpmFetcher::FetchTpmInfoCallback&& callback) {
  pending_callbacks_.push_back(std::move(callback));
  // Returns if there is already a pending callback. The second callback will be
  // fulfilled when the first one is fulfilled.
  if (pending_callbacks_.size() > 1)
    return;

  info_ = mojo_ipc::TpmInfo::New();
  SendError("Not implemented.");
}

}  // namespace diagnostics
