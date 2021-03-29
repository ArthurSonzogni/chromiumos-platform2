// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_METRICS_H_
#define SECANOMALYD_METRICS_H_

#include <string>

bool SendSecurityAnomalyToUMA(const std::string& anomaly);

#endif  // SECANOMALYD_METRICS_H_
