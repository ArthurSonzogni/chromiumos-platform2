// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/status_macros.h"

#include <optional>
#include <utility>

#include <base/types/expected.h>

#include "missive/util/status.h"

namespace reporting::internal {

std::optional<Status> ShouldReturnStatus(const Status& status) {
  return status.ok() ? std::optional<Status>(std::nullopt) : status;
}

std::optional<Status> ShouldReturnStatus(Status&& status) {
  return status.ok() ? std::optional<Status>(std::nullopt) : std::move(status);
}

std::optional<base::unexpected<Status>> ShouldReturnStatus(
    const base::unexpected<Status>& status) {
  return status.error().ok()
             ? std::optional<base::unexpected<Status>>(std::nullopt)
             : status;
}

std::optional<base::unexpected<Status>> ShouldReturnStatus(
    base::unexpected<Status>&& status) {
  return status.error().ok()
             ? std::optional<base::unexpected<Status>>(std::nullopt)
             : std::move(status);
}

}  // namespace reporting::internal
