// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_MMC_ERROR_PARSER_H_
#define METRICS_MMC_ERROR_PARSER_H_

#include <memory>
#include <string>
#include <string_view>

#include <base/files/file_path.h>

#include "metrics/debugd_reader.h"
#include "metrics/persistent_integer.h"

namespace chromeos_metrics {

// Record of MMC errors we care about.
struct MmcErrorRecord {
  int cmd_timeouts;
  int cmd_crcs;
  int data_timeouts;
  int data_crcs;
};

inline constexpr char kDataTimeoutName[] = "DataTimeout";
inline constexpr char kDataCRCName[] = "DataCRC";
inline constexpr char kCmdTimeoutName[] = "CmdTimeout";
inline constexpr char kCmdCRCName[] = "CmdCRC";

// Encapsulates the logic to parse MMC error counters from debugd log.
// The log data counts various controller errors that occurred since the system
// was started. Each counter needs to be stored in two persistent integers.
// One is used to keep the delta between what was already sent to UMA
// and the current value, where as the other tracks how many errors
// were seen from when the system started.
// The former is used to keep track of errors that weren't reported before
// the device was rebooted. The latter is needed in case metric_daemon
// crashes, so that we don't report the same error multiple times.
// This works under two assumptions:
// 1. persistent_dir points to a directory which contents survive a reboot.
// 2. runtime_dir points to a directory that is cleared every boot.
class MmcErrorParser {
 public:
  MmcErrorParser(const MmcErrorParser&) = delete;
  MmcErrorParser& operator=(const MmcErrorParser&) = delete;

  // Factory function is used, since initialization can fail.
  static std::unique_ptr<MmcErrorParser> Create(
      const base::FilePath& persistent_dir,
      const base::FilePath& runtime_dir,
      std::unique_ptr<DebugdReader> reader,
      std::string_view name);
  MmcErrorRecord GetAndClear();
  void Update();

 private:
  MmcErrorParser(const base::FilePath& persistent_dir,
                 const base::FilePath& runtime_dir,
                 std::unique_ptr<DebugdReader> reader,
                 std::string_view name);

  std::unique_ptr<DebugdReader> reader_;
  // Name of the MMC controller we're collecting logs from.
  // This is primarily used to figure out which part of the logs from debugd
  // we're interested in, see the definition of Update functions for details.
  // Note that we can't have two objects with the same name, or the
  // PersistentIntegers backing files will collide.
  std::string name_;

  // The following tracks how many errors haven't been sent to UMA yet.
  // The backing storage needs to survive reboot.
  std::unique_ptr<PersistentInteger> cmd_timeouts_;
  std::unique_ptr<PersistentInteger> cmd_crcs_;
  std::unique_ptr<PersistentInteger> data_timeouts_;
  std::unique_ptr<PersistentInteger> data_crcs_;

  // The following tracks how many errors were seen since boot.
  // The backing storage needs to be cleaned upon reboot.
  std::unique_ptr<PersistentInteger> cmd_timeouts_since_boot_;
  std::unique_ptr<PersistentInteger> cmd_crcs_since_boot_;
  std::unique_ptr<PersistentInteger> data_timeouts_since_boot_;
  std::unique_ptr<PersistentInteger> data_crcs_since_boot_;
};
}  // namespace chromeos_metrics

#endif  // METRICS_MMC_ERROR_PARSER_H_
