// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/fake_vpn_service.h"

#include <memory>

#include "shill/vpn/mock_vpn_driver.h"
#include "shill/vpn/vpn_driver.h"
#include "shill/vpn/vpn_types.h"

namespace shill {

// Note: the injected VPNDriver does not have to be a mock one. Using it just
// for simplicity now since we don't have a stub/fake driver at the moment.
FakeVPNService::FakeVPNService(Manager* manager)
    : VPNService(manager,
                 std::make_unique<MockVPNDriver>(manager, VPNType::kOpenVPN)) {}

FakeVPNService::~FakeVPNService() = default;

}  // namespace shill
