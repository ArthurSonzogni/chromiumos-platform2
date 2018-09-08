// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/mock_vpn_provider.h"

namespace shill {

MockVPNProvider::MockVPNProvider()
    : VPNProvider(nullptr, nullptr, nullptr, nullptr) {}

MockVPNProvider::~MockVPNProvider() {}

}  // namespace shill
