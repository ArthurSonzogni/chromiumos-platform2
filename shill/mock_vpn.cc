// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_vpn.h"

namespace shill {

MockVPN::MockVPN(ControlInterface *control,
                 EventDispatcher *dispatcher,
                 Metrics *metrics,
                 Manager *manager,
                 const std::string &link_name,
                 int interface_index)
    : VPN(control, dispatcher, metrics, manager, link_name, interface_index) {}

MockVPN::~MockVPN() {}

}  // namespace shill
