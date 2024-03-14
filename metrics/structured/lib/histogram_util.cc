// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See //platform2/metrics/structured/README.md for more details.
#include "metrics/structured/lib/histogram_util.h"

namespace metrics::structured {

// Chromium writes histograms differently from ChromiumOS. Remove the histogram
// write until this file is replaced with the appropriate write in ChromiumOS.
void LogInternalError(StructuredMetricsError error) {}

// Chromium writes histograms differently from ChromiumOS. Remove the histogram
// write until this file is replaced with the appropriate write in ChromiumOS.
void LogKeyValidation(KeyValidationState state) {}

}  // namespace metrics::structured
