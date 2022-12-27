// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_MINIDIAG_MINIDIAG_METRICS_NAMES_H_
#define DIAGNOSTICS_CROS_MINIDIAG_MINIDIAG_METRICS_NAMES_H_

namespace cros_minidiag {

namespace metrics {
// General metrics
inline constexpr char kLaunchHistogram[] = "Platform.MiniDiag.Launch";
inline constexpr int kLaunchCountMax = 50;

}  // namespace metrics
}  // namespace cros_minidiag

#endif  // DIAGNOSTICS_CROS_MINIDIAG_MINIDIAG_METRICS_NAMES_H_
