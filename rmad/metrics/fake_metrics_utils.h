// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_METRICS_FAKE_METRICS_UTILS_H_
#define RMAD_METRICS_FAKE_METRICS_UTILS_H_

#include "rmad/metrics/metrics_utils.h"

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>

#include "rmad/utils/json_store.h"

namespace rmad {
namespace fake {

class FakeMetricsUtils : public MetricsUtils {
 public:
  explicit FakeMetricsUtils(const base::FilePath& working_dir_path);
  ~FakeMetricsUtils() override = default;

  bool Record(scoped_refptr<JsonStore> json_store, bool is_complete) override;

 private:
  base::FilePath working_dir_path_;
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_METRICS_FAKE_METRICS_UTILS_H_
