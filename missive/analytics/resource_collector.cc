// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/analytics/resource_collector.h"

#include <base/time/time.h>
#include <base/timer/timer.h>

namespace reporting::analytics {

ResourceCollector::~ResourceCollector() = default;

ResourceCollector::ResourceCollector(base::TimeDelta interval) {
  timer_.Start(FROM_HERE, interval, this, &ResourceCollector::Collect);
}

}  // namespace reporting::analytics
