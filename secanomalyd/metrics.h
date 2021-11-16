// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_METRICS_H_
#define SECANOMALYD_METRICS_H_

#include <string>

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class SecurityAnomaly {
  kMountInitNsWx = 0,  // deprecated
  kMount_InitNs_WxInUsrLocal = 1,
  kMount_InitNs_WxNotInUsrLocal = 2,
  kMaxValue = kMount_InitNs_WxNotInUsrLocal,
};

bool SendSecurityAnomalyToUMA(SecurityAnomaly secanomaly);

bool SendWXMountCountToUMA(size_t wx_mount_count);

bool SendAnomalyUploadResultToUMA(bool success);

#endif  // SECANOMALYD_METRICS_H_
