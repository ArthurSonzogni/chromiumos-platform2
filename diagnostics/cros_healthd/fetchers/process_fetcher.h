// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>

#include <base/callback.h>
#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/fetchers/base_fetcher.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace mojom = chromeos::cros_healthd::mojom;

// The ProcessFetcher class is responsible for gathering information about a
// particular process on the device.
class ProcessFetcher final : public BaseFetcher {
 public:
  // |process_id| is the PID for the process whose information will be fetched.
  // Only override |root_dir| for testing.
  ProcessFetcher(Context* context,
                 pid_t process_id,
                 const base::FilePath& root_dir = base::FilePath("/"));

  // Returns information about a particular process on the device, or the error
  // that occurred retrieving the information.
  void FetchProcessInfo(
      base::OnceCallback<void(mojom::ProcessResultPtr)> callback);

 private:
  // Parses relevant fields from /proc/|process_id_|/stat. Returns the first
  // error encountered or std::nullopt if no errors occurred. |priority|,
  // |nice|, |start_time_ticks|, |name|, |parent_process_id|,
  // |process_group_id|, |threads| and |process_id| are only valid if
  // std::nullopt was returned.
  std::optional<mojom::ProbeErrorPtr> ParseProcPidStat(
      mojom::ProcessState* state,
      int8_t* priority,
      int8_t* nice,
      uint64_t* start_time_ticks,
      std::optional<std::string>* name,
      uint32_t* parent_process_id,
      uint32_t* process_group_id,
      uint32_t* threads,
      uint32_t* process_id);

  // Parses relevant fields from /proc/|process_id_|/statm. Returns the first
  // error encountered or std::nullopt if no errors occurred.
  // |total_memory_kib|, |resident_memory_kib| and |free_memory_kib| are only
  // valid if std::nullopt was returned.
  std::optional<mojom::ProbeErrorPtr> ParseProcPidStatm(
      uint32_t* total_memory_kib,
      uint32_t* resident_memory_kib,
      uint32_t* free_memory_kib);

  // Calculates the uptime of the process in clock ticks using
  // |start_time_ticks|. Returns the first error encountered or std::nullopt if
  // no errors occurred. |process_uptime_ticks| is only valid if std::nullopt
  // was returned.
  std::optional<mojom::ProbeErrorPtr> CalculateProcessUptime(
      uint64_t start_time_ticks, uint64_t* process_uptime_ticks);

  // Fetches the real user ID of the process. Returns the first error
  // encountered or std::nullopt if no errors occurred. |user_id| is only
  // valid if std::nullopt was returned.
  std::optional<mojom::ProbeErrorPtr> GetProcessUid(uid_t* user_id);

  // File paths read will be relative to |root_dir_|. In production, this should
  // be "/", but it can be overridden for testing.
  const base::FilePath root_dir_;
  // Procfs subdirectory with files specific to the process.
  const base::FilePath proc_pid_dir_;
  // The process ID.
  const pid_t process_id_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_PROCESS_FETCHER_H_
