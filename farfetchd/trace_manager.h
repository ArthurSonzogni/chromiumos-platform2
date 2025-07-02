// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FARFETCHD_TRACE_MANAGER_H_
#define FARFETCHD_TRACE_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <base/files/file_path.h>
#include <base/task/sequenced_task_runner.h>

#include "farfetchd/trace_reader.h"
#include "farfetchd/trace_session.h"

namespace farfetchd {

// Manages the lifecycle of all tracing sessions.
class TraceManager {
 public:
  TraceManager();
  explicit TraceManager(std::unique_ptr<TraceReader> trace_reader);
  ~TraceManager();

  // Starts a new trace atomically. Returns an empty string on failure.
  // |process_names| is a list of process command names to trace (e.g.,
  // "chrome"). |path_allowlist| and |path_denylist| are lists of regexes to
  // filter file paths included in the trace.
  std::string StartTrace(const std::string& app_name,
                         const std::vector<std::string>& process_names,
                         const std::vector<std::string>& path_allowlist,
                         const std::vector<std::string>& path_denylist);

  // Stops a trace gracefully, allowing final data processing.
  bool StopTrace(const std::string& trace_id);

  // Aborts a trace immediately, discarding any collected data.
  bool CancelTrace(const std::string& trace_id);

  // Returns the current status of a trace as a string.
  std::string GetTraceStatus(const std::string& trace_id);

  // Returns the path to the final trace file if completed successfully.
  std::string GetTracePath(const std::string& trace_id);

  // For testing purposes, to redirect traces to a temporary directory.
  void SetTraceBaseDirForTest(const base::FilePath& dir) {
    trace_base_dir_ = dir;
  }

 private:
  // Creates a TraceSession object and its directory structure.
  std::string CreateTrace(const std::string& app_name);

  // Enables tracing for the session and starts the reader task.
  bool ActivateTrace(const std::string& trace_id,
                     const std::vector<std::string>& process_names,
                     const std::vector<std::string>& path_allowlist,
                     const std::vector<std::string>& path_denylist);

  // The background task that continuously reads from the tracefs pipe.
  void ReadTracePipeTask(const std::string& trace_id);

  // Generates a unique, time-based identifier for a trace.
  std::string GenerateTraceId();

  // Creates a dedicated directory for a trace session's files.
  bool CreateTraceDirectory(const std::string& trace_id);

  // Scans /proc to find all processes matching the given process names.
  // Returns a set of PIDs whose /proc/{pid}/comm matches any name in the list.
  std::unordered_set<pid_t> GetProcessesByComm(
      const std::vector<std::string>& process_names);

  // Updates the traced_pids set for a session by rescanning processes.
  // This allows dynamic discovery of new processes that match the criteria.
  void UpdateTracedPids(std::shared_ptr<TraceSession> session);

  // Checks if enough time has passed since the last process scan to warrant
  // a rescan for new processes.
  bool ShouldRescanProcesses(std::shared_ptr<TraceSession> session);

  // Asynchronously parses raw trace data into a final, structured format.
  void ProcessTrace(const std::string& trace_id);

  // Extracts the file path from a raw trace line. Returns an empty
  // string if no path can be found.
  std::string ExtractPathFromTraceLine(const std::string& line);

  // Handles a timeout event for a given trace session.
  void OnTraceTimeout(const std::string& trace_id);

  // All active and completed trace sessions, keyed by trace ID.
  std::map<std::string, std::shared_ptr<TraceSession>> sessions_;

  // Task runner for background I/O, specifically for reading the trace pipe.
  scoped_refptr<base::SequencedTaskRunner> reader_task_runner_;

  // The base directory where all trace files are stored.
  base::FilePath trace_base_dir_;

  // Interface for reading trace data (can be mocked for testing)
  std::unique_ptr<TraceReader> trace_reader_;
};

}  // namespace farfetchd

#endif  // FARFETCHD_TRACE_MANAGER_H_
