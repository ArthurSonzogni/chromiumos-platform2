// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/mock_modem.h"

namespace shill {

MockModem::MockModem(const std::string &owner,
                     const std::string &service,
                     const std::string &path,
                     ModemInfo *modem_info)
    : Modem(owner, service, path, modem_info) {}

MockModem::~MockModem() {}

}  // namespace shill
