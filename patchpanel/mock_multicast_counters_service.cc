// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_multicast_counters_service.h"

namespace patchpanel {

MockMulticastCountersService::MockMulticastCountersService()
    : MulticastCountersService(nullptr) {}

MockMulticastCountersService::~MockMulticastCountersService() = default;

}  // namespace patchpanel
