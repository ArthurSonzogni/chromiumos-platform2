// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_guest_ipv6_service.h"

#include "patchpanel/guest_ipv6_service.h"

namespace patchpanel {

MockGuestIPv6Service::MockGuestIPv6Service()
    : GuestIPv6Service(
          /*nd_proxy=*/nullptr, /*datapath=*/nullptr, /*system=*/nullptr) {}
MockGuestIPv6Service::~MockGuestIPv6Service() = default;

}  // namespace patchpanel
