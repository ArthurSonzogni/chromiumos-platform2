// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_modem.h"

namespace shill {

MockModem::MockModem(const std::string &owner,
                     const std::string &service,
                     const std::string &path,
                     ControlInterface *control_interface,
                     EventDispatcher *dispatcher,
                     Metrics *metrics,
                     Manager *manager,
                     CellularOperatorInfo *cellular_operator_info,
                     mobile_provider_db *provider_db)
    : Modem(owner, service, path, control_interface, dispatcher, metrics,
            manager, cellular_operator_info, provider_db) {}

MockModem::~MockModem() {}

}  // namespace shill
