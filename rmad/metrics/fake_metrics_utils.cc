// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/metrics/fake_metrics_utils.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "rmad/constants.h"
#include "rmad/utils/json_store.h"

namespace rmad {
namespace fake {

FakeMetricsUtils::FakeMetricsUtils(const base::FilePath& working_dir_path)
    : working_dir_path_(working_dir_path) {}

bool FakeMetricsUtils::Record(scoped_refptr<JsonStore> json_store,
                              bool is_complete) {
  const base::FilePath metrics_record_success_file_path =
      working_dir_path_.AppendASCII(kMetricsRecordSuccessFilePath);
  return base::PathExists(metrics_record_success_file_path);
}

}  // namespace fake
}  // namespace rmad
