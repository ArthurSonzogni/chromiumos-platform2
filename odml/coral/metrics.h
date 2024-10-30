// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_METRICS_H_
#define ODML_CORAL_METRICS_H_

#include <metrics/metrics_library.h>

namespace coral {

class CoralMetrics {
 public:
  explicit CoralMetrics(raw_ref<MetricsLibraryInterface> metrics);

 private:
  const raw_ref<MetricsLibraryInterface> metrics_;
};

}  // namespace coral

#endif  // ODML_CORAL_METRICS_H_
