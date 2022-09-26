// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_METRICS_UTILS_H_
#define SHILL_WIFI_WIFI_METRICS_UTILS_H_

namespace shill::WiFiMetricsUtils {

// Given a specific AP OUI, can the client add it to the reported metrics?
bool CanReportOUI(int oui);

// This is only used by tests. It returns an AP OUI that is in the allowlist of
// OUIs that can be reported. go/totw/135.
int AllowlistedOUIForTesting();

}  // namespace shill::WiFiMetricsUtils

#endif  // SHILL_WIFI_WIFI_METRICS_UTILS_H_
