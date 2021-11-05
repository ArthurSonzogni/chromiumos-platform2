// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_METRICS_METRICS_UTILS_H_
#define RMAD_METRICS_METRICS_UTILS_H_

#include <base/memory/scoped_refptr.h>

#include "rmad/utils/json_store.h"

namespace rmad {

class MetricsUtils {
 public:
  MetricsUtils() = default;
  virtual ~MetricsUtils() = default;

  // Record the metrics to the event-based metrics file, and wait for upload.
  virtual bool Record(scoped_refptr<JsonStore> json_store,
                      bool is_complete) = 0;
};

}  // namespace rmad

#endif  // RMAD_METRICS_METRICS_UTILS_H_
