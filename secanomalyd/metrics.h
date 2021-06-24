// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_METRICS_H_
#define SECANOMALYD_METRICS_H_

#include <string>

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class SecurityAnomaly {
  kMountInitNsWx = 0,
  kMountInitNsWxUsrLocal = 1,
  kMaxValue = kMountInitNsWxUsrLocal,
};

bool SendSecurityAnomalyToUMA(SecurityAnomaly secanomaly);

#endif  // SECANOMALYD_METRICS_H_
