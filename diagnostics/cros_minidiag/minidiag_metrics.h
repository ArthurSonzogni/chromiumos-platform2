// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_MINIDIAG_MINIDIAG_METRICS_H_
#define DIAGNOSTICS_CROS_MINIDIAG_MINIDIAG_METRICS_H_

#include <metrics/metrics_library.h>

#include "diagnostics/cros_minidiag/minidiag_metrics_names.h"

namespace cros_minidiag {

// This class provides wrapping functions for callers to report ChromeOS
// elog-related metrics without bothering to know all the constant declarations.
class MiniDiagMetrics : private MetricsLibrary {
 public:
  MiniDiagMetrics();
  MiniDiagMetrics(const MiniDiagMetrics&) = delete;
  MiniDiagMetrics operator=(const MiniDiagMetrics&) = delete;
  ~MiniDiagMetrics();

  // Report Platform.MiniDiag.Launch event.
  void RecordLaunch(int count) const;

  void SetMetricsLibraryForTesting(MetricsLibraryInterface* metrics_library) {
    metrics_library_ = metrics_library;
  }

 private:
  MetricsLibraryInterface* metrics_library_{this};
};
}  // namespace cros_minidiag

#endif  // DIAGNOSTICS_CROS_MINIDIAG_MINIDIAG_METRICS_H_
