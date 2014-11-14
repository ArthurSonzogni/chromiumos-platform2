// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/mock_cellular_service.h"

#include <chromeos/dbus/service_constants.h>

using testing::ReturnRef;

namespace shill {

MockCellularService::MockCellularService(ModemInfo *modem_info,
                                         const CellularRefPtr &device)
    : CellularService(modem_info, device),
      default_activation_state_(kActivationStateUnknown) {
  ON_CALL(*this, activation_state())
      .WillByDefault(ReturnRef(default_activation_state_));
}

MockCellularService::~MockCellularService() {}

}  // namespace shill
