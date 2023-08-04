// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_METRICS_H_
#define SECANOMALYD_METRICS_H_

#include <cstddef>

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class SecurityAnomaly {
  kMountInitNsWx = 0,  // deprecated
  kMount_InitNs_WxInUsrLocal = 1,
  kMount_InitNs_WxNotInUsrLocal = 2,
  kSuccessfulMemfdCreateSyscall = 3,
  kBlockedMemoryFileExecAttempt = 4,
  kMaxValue = kBlockedMemoryFileExecAttempt,
};

bool SendSecurityAnomalyToUMA(SecurityAnomaly secanomaly);

bool SendWXMountCountToUMA(size_t wx_mount_count);

// These functions are used for emitting anomalous process count metrics.
bool SendForbiddenIntersectionProcCountToUMA(size_t proc_count);
bool SendAttemptedMemfdExecProcCountToUMA(size_t proc_count);

bool SendLandlockStatusToUMA(bool enabled);

bool SendSecCompCoverageToUMA(unsigned int coverage_percentage);

bool SendNnpProcPercentageToUMA(unsigned int proc_percentage);

bool SendNonRootProcPercentageToUMA(unsigned int proc_percentage);

bool SendUnprivProcPercentageToUMA(unsigned int proc_percentage);

bool SendNonInitNsProcPercentageToUMA(unsigned int proc_percentage);

bool SendAnomalyUploadResultToUMA(bool success);

#endif  // SECANOMALYD_METRICS_H_
