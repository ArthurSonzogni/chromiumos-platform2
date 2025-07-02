// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FARFETCHD_TRACE_SESSION_H_
#define FARFETCHD_TRACE_SESSION_H_

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

namespace farfetchd {

// Describes the lifecycle of a trace session.
enum class TraceState {
  kCreated,     // Session object exists but is not tracing.
  kTracing,     // Actively collecting trace data.
  kProcessing,  // Trace stopped, now parsing raw data.
  kCompleted,   // Processing finished, final trace is ready.
  kError,       // An unrecoverable error occurred.
  kCancelled,   // Trace was aborted by the client; data may be incomplete.
};

// Holds all state for a single tracing session.
struct TraceSession {
  std::string id;
  std::string app_name;
  TraceState state = TraceState::kCreated;

  // List of process names (comm) to trace (e.g., "chrome", "chrome_renderer")
  std::vector<std::string> process_names;

  // Regex patterns for path filtering
  std::vector<std::string> path_allowlist;
  std::vector<std::string> path_denylist;

  // Timestamp of the last process scan for dynamic discovery
  base::TimeTicks last_process_scan;

  base::Time start_time;

  // Path to the raw trace file from tracefs
  base::FilePath raw_trace_path;

  // Path to the final, processed trace file
  base::FilePath final_trace_path;

  // A watchdog timer to prevent runaway traces. This is started when tracing
  // begins and stopped upon graceful completion or cancellation.
  std::unique_ptr<base::OneShotTimer> watchdog_timer;

  // The set of process IDs currently being traced. This is dynamically
  // updated by scanning /proc/{pid}/comm for processes matching process_names.
  std::unordered_set<pid_t> traced_pids;
};

}  // namespace farfetchd

#endif  // FARFETCHD_TRACE_SESSION_H_
