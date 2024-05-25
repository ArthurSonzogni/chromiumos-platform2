// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_counters_service.h"

#include "patchpanel/conntrack_monitor.h"
#include "patchpanel/counters_service.h"
#include "patchpanel/datapath.h"

namespace patchpanel {

MockCountersService::MockCountersService(Datapath* datapath,
                                         ConntrackMonitor* monitor)
    : CountersService(datapath, monitor) {}
MockCountersService::~MockCountersService() = default;

}  // namespace patchpanel
