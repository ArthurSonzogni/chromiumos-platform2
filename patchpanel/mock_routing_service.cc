// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_routing_service.h"

#include "patchpanel/lifeline_fd_service.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/system.h"

namespace patchpanel {

MockRoutingService::MockRoutingService()
    : RoutingService(
          /*system=*/nullptr, /*lifeline_fd_service=*/nullptr) {}
MockRoutingService::~MockRoutingService() = default;

}  // namespace patchpanel
