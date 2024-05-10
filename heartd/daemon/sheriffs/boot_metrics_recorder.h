// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_SHERIFFS_BOOT_METRICS_RECORDER_H_
#define HEARTD_DAEMON_SHERIFFS_BOOT_METRICS_RECORDER_H_

#include <base/files/file_path.h>

#include "heartd/daemon/database.h"
#include "heartd/daemon/sheriffs/sheriff.h"

namespace heartd {

constexpr char kMetricsPath[] = "var/log/metrics/";
constexpr char kBootIDPath[] = "var/log/boot_id.log";

class BootMetricsRecorder final : public Sheriff {
 public:
  explicit BootMetricsRecorder(const base::FilePath& root_dir,
                               const Database* database);
  BootMetricsRecorder(const BootMetricsRecorder&) = delete;
  BootMetricsRecorder& operator=(const BootMetricsRecorder&) = delete;
  ~BootMetricsRecorder() override;

  // heartd::Sheriff override:
  void OneShotWork() override;
  bool HasShiftWork() override;
  void AdjustSchedule() override;
  void ShiftWork() override;
  void CleanUp() override;

 private:
  void CollectShutdownTime();
  void CollectBootID();

 private:
  base::FilePath root_dir_;
  const Database* const database_ = nullptr;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_SHERIFFS_BOOT_METRICS_RECORDER_H_
