// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_connmark_updater.h"

namespace patchpanel {

MockConnmarkUpdater::MockConnmarkUpdater(ConntrackMonitor* monitor)
    : ConnmarkUpdater(monitor) {}
MockConnmarkUpdater::~MockConnmarkUpdater() = default;

}  // namespace patchpanel
