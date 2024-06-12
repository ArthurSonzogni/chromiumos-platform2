// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_SHERIFFS_INTEL_PMT_COLLECTOR_H_
#define HEARTD_DAEMON_SHERIFFS_INTEL_PMT_COLLECTOR_H_

#include <memory>

#include <base/values.h>
#include <libpmt/pmt.h>

#include "heartd/daemon/sheriffs/sheriff.h"

namespace heartd {

constexpr char kIntelPMTLogPath[] = "var/lib/heartd/intel_pmt/intel_pmt.log";
constexpr char kIntelPMTCounterPath[] = "var/lib/heartd/intel_pmt/counter";
constexpr char kIntelPMTConfigPath[] = "var/lib/heartd/intel_pmt/config";
constexpr char kIntelPMTConfigSampleFrequency[] = "sample_frequency";

class IntelPMTCollector final : public Sheriff {
 public:
  explicit IntelPMTCollector(const base::FilePath& root_dir,
                             pmt::PmtCollector* collector = nullptr,
                             pmt::Snapshot* snapshot = nullptr);
  IntelPMTCollector(const IntelPMTCollector&) = delete;
  IntelPMTCollector& operator=(const IntelPMTCollector&) = delete;
  ~IntelPMTCollector();

  // heartd::Sheriff override:
  void OneShotWork() override;
  bool HasShiftWork() override;
  void AdjustSchedule() override;
  void ShiftWork() override;
  void CleanUp() override;

  void WriteSnapshot();

 private:
  void CleanUpLogsAndSetNewHeader();

 private:
  // Path of the root directory.
  base::FilePath root_dir_;
  // Libpmt object to help us reading the telemetry data.
  std::unique_ptr<pmt::PmtCollector> collector_;
  // The pointer to the Intel PMT snapshot data.
  const pmt::Snapshot* snapshot_ = nullptr;
  // Collector config from `kIntelPMTConfigPath`.
  base::Value::Dict config_;
  // `kIntelPMTLogPath` fd.
  int log_fd_ = -1;
  // `kIntelPMTCounterPath` fd.
  int counter_fd_ = -1;
  // Current counter. It's used to locate the next position of log file.
  int counter_ = 0;
  // Size of the PMT log header in number of bytes.
  size_t header_size_ = 0;
};

}  // namespace heartd

#endif  // HEARTD_DAEMON_SHERIFFS_INTEL_PMT_COLLECTOR_H_
