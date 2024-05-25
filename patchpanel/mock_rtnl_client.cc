// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_rtnl_client.h"

#include "patchpanel/rtnl_client.h"

namespace patchpanel {

MockRTNLClient::MockRTNLClient() : RTNLClient(/*rtnl_fd=*/base::ScopedFD()) {}
MockRTNLClient::~MockRTNLClient() = default;

}  // namespace patchpanel
