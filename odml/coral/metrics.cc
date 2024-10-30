// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/metrics.h"

#include <metrics/metrics_library.h>

namespace coral {

CoralMetrics::CoralMetrics(raw_ref<MetricsLibraryInterface> metrics)
    : metrics_(metrics) {}

}  // namespace coral
