// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "farfetchd/trace_manager.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/task/thread_pool.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <base/uuid.h>
#include <brillo/files/file_util.h>
#include <re2/re2.h>

#include "farfetchd/kernel_trace_reader.h"

namespace farfetchd {

namespace {
constexpr char kTraceBaseDir[] = "/var/cache/farfetchd";
// Set a default timeout for traces to prevent them from running indefinitely.
constexpr base::TimeDelta kDefaultTraceTimeout = base::Minutes(5);
// Minimum interval between process rescans to avoid excessive overhead.
constexpr base::TimeDelta kProcessRescanInterval = base::Seconds(5);
}  // namespace

TraceManager::TraceManager()
    : TraceManager(std::make_unique<KernelTraceReader>()) {}

TraceManager::TraceManager(std::unique_ptr<TraceReader> trace_reader)
    : trace_base_dir_(kTraceBaseDir), trace_reader_(std::move(trace_reader)) {
  // Ensure the base directory exists.
  if (!base::CreateDirectory(trace_base_dir_)) {
    LOG(ERROR) << "Failed to create trace base directory: " << trace_base_dir_;
  }
  // The task runner for reading the trace pipe, which is a blocking operation.
  reader_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
      {base::TaskPriority::USER_VISIBLE, base::MayBlock()});
}

TraceManager::~TraceManager() {
  // Stop all active traces
  for (const auto& [trace_id, session] : sessions_) {
    if (session->state == TraceState::kTracing) {
      StopTrace(trace_id);
    }
  }
}

std::string TraceManager::CreateTrace(const std::string& app_name) {
  std::string trace_id = GenerateTraceId();

  if (!CreateTraceDirectory(trace_id)) {
    LOG(ERROR) << "Failed to create directory for trace: " << trace_id;
    return "";
  }

  auto session = std::make_shared<TraceSession>();
  session->id = trace_id;
  session->app_name = app_name;
  session->state = TraceState::kCreated;
  session->raw_trace_path =
      trace_base_dir_.Append(trace_id).Append("trace.raw");
  session->final_trace_path =
      trace_base_dir_.Append(trace_id).Append("trace.log");

  sessions_[trace_id] = session;

  LOG(INFO) << "Created trace session: " << trace_id
            << " for app: " << app_name;
  return trace_id;
}

bool TraceManager::ActivateTrace(
    const std::string& trace_id,
    const std::vector<std::string>& process_names,
    const std::vector<std::string>& path_allowlist,
    const std::vector<std::string>& path_denylist) {
  auto it = sessions_.find(trace_id);
  if (it == sessions_.end()) {
    LOG(ERROR) << "ActivateTrace: session not found: " << trace_id;
    return false;
  }

  auto session = it->second;
  if (session->state != TraceState::kCreated) {
    LOG(ERROR) << "Trace session " << trace_id << " is not in created state.";
    return false;
  }

  session->process_names = process_names;
  session->path_allowlist = path_allowlist;
  session->path_denylist = path_denylist;
  session->start_time = base::Time::Now();
  session->last_process_scan = base::TimeTicks::Now();
  session->state = TraceState::kTracing;

  // Get all PIDs matching the process names for userspace filtering.
  session->traced_pids = GetProcessesByComm(process_names);

  LOG(INFO) << "Found " << session->traced_pids.size()
            << " processes matching criteria for session " << trace_id;

  if (session->traced_pids.empty()) {
    LOG(WARNING) << "No processes found matching the specified names for "
                 << "session " << trace_id << ". Trace may be empty until "
                 << "matching processes are started.";
  }

  // Start the watchdog timer.
  session->watchdog_timer = std::make_unique<base::OneShotTimer>();
  session->watchdog_timer->Start(
      FROM_HERE, kDefaultTraceTimeout,
      base::BindOnce(&TraceManager::OnTraceTimeout, base::Unretained(this),
                     trace_id));

  // Post the blocking trace pipe reading task to the background runner.
  reader_task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&TraceManager::ReadTracePipeTask,
                                base::Unretained(this), trace_id));

  LOG(INFO) << "Trace session " << trace_id << " is now active for processes: "
            << base::JoinString(process_names, ", ");
  return true;
}

void TraceManager::ReadTracePipeTask(const std::string& trace_id) {
  auto it = sessions_.find(trace_id);
  if (it == sessions_.end()) {
    // Session was likely cancelled before this task could run.
    LOG(WARNING) << "ReadTracePipeTask: Session " << trace_id << " not found.";
    return;
  }
  auto session = it->second;

  // Open the raw log file for appending.
  std::ofstream raw_log_file(session->raw_trace_path.value(), std::ios::app);
  if (!raw_log_file) {
    LOG(ERROR) << "Failed to open raw trace file for writing: "
               << session->raw_trace_path;
    session->state = TraceState::kError;
    return;
  }

  // Open the trace reader for reading trace events.
  if (!trace_reader_->Open()) {
    LOG(ERROR) << "Failed to open trace reader. "
               << "Check permissions and if tracing is enabled.";
    session->state = TraceState::kError;
    return;
  }

  LOG(INFO) << "Starting to read trace pipe for session " << trace_id;

  std::string line;
  while (session->state == TraceState::kTracing) {
    if (trace_reader_->ReadLine(&line)) {
      // Parse the PID from the "comm-pid" field.
      // A typical trace line starts with: " a.out-1234  [000] .... timestamp:
      // event_name: ..." Find the first non-whitespace token (comm-pid).
      size_t start_pos = line.find_first_not_of(" \t");
      if (start_pos == std::string::npos) {
        continue;
      }

      size_t end_pos = line.find_first_of(" \t", start_pos);
      if (end_pos == std::string::npos) {
        continue;
      }

      std::string comm_pid = line.substr(start_pos, end_pos - start_pos);
      size_t pid_separator = comm_pid.find_last_of('-');
      if (pid_separator == std::string::npos) {
        continue;
      }

      pid_t event_pid;
      if (!base::StringToInt(comm_pid.substr(pid_separator + 1), &event_pid)) {
        continue;
      }

      // Check if this PID is in our current traced set.
      if (session->traced_pids.count(event_pid)) {
        // This event belongs to one of our target processes.
        // Write all matching events to raw file - filtering will be done
        // offline.
        raw_log_file << line << std::endl;
      } else if (ShouldRescanProcesses(session)) {
        // Periodically rescan for new processes matching our criteria.
        UpdateTracedPids(session);

        // Check again after the rescan.
        if (session->traced_pids.count(event_pid)) {
          raw_log_file << line << std::endl;
        }
      }
    } else {
      // If ReadLine fails, it could be an error or a signal interruption.
      // Check the session state again to see if we were told to stop.
      if (session->state == TraceState::kTracing) {
        LOG(ERROR) << "Failed to read from trace reader while session "
                   << trace_id << " was active.";
        session->state = TraceState::kError;
        break;
      }
    }
  }

  trace_reader_->Close();
  LOG(INFO) << "Finished reading trace pipe for session " << trace_id
            << ". Final state: " << static_cast<int>(session->state);
}

bool TraceManager::StopTrace(const std::string& trace_id) {
  auto it = sessions_.find(trace_id);
  if (it == sessions_.end()) {
    LOG(ERROR) << "Trace session not found: " << trace_id;
    return false;
  }

  auto session = it->second;
  if (session->state != TraceState::kTracing) {
    LOG(ERROR) << "Trace session " << trace_id << " is not in tracing state";
    return false;
  }

  // Stop the watchdog timer as we are stopping gracefully.
  if (session->watchdog_timer) {
    session->watchdog_timer->Stop();
  }

  session->state = TraceState::kProcessing;

  // Schedule asynchronous processing
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::BEST_EFFORT, base::MayBlock()},
      base::BindOnce(&TraceManager::ProcessTrace, base::Unretained(this),
                     trace_id));

  LOG(INFO) << "Stopped tracing for session: " << trace_id;
  return true;
}

bool TraceManager::CancelTrace(const std::string& trace_id) {
  auto it = sessions_.find(trace_id);
  if (it == sessions_.end()) {
    LOG(ERROR) << "CancelTrace: session not found: " << trace_id;
    return false;
  }

  auto session = it->second;

  // Stop the watchdog timer since the trace is being explicitly cancelled.
  if (session->watchdog_timer) {
    session->watchdog_timer->Stop();
  }

  // If still tracing, mark cancelled; no graceful processing.
  if (session->state == TraceState::kTracing) {
    session->state = TraceState::kCancelled;
  } else if (session->state == TraceState::kProcessing) {
    session->state = TraceState::kCancelled;
  }

  // Remove any intermediate files.
  brillo::DeleteFile(session->raw_trace_path);
  brillo::DeleteFile(session->final_trace_path);

  LOG(INFO) << "Trace session cancelled: " << trace_id;
  return true;
}

std::string TraceManager::GetTraceStatus(const std::string& trace_id) {
  auto it = sessions_.find(trace_id);
  if (it == sessions_.end()) {
    return "";
  }
  switch (it->second->state) {
    case TraceState::kCreated:
      return "Created";
    case TraceState::kTracing:
      return "Tracing";
    case TraceState::kProcessing:
      return "Processing";
    case TraceState::kCompleted:
      return "Completed";
    case TraceState::kCancelled:
      return "Cancelled";
    case TraceState::kError:
      return "Error";
    default:
      return "Unknown";
  }
}

std::string TraceManager::GetTracePath(const std::string& trace_id) {
  auto it = sessions_.find(trace_id);
  if (it == sessions_.end()) {
    LOG(INFO) << "GetTracePath: session not found for trace_id: " << trace_id;
    return "";
  }

  auto session = it->second;
  if (session->state == TraceState::kCompleted) {
    return session->final_trace_path.value();
  } else if (session->state == TraceState::kError) {
    LOG(ERROR) << "Trace session " << trace_id << " is in error state";
    return "";
  }

  // Still processing or not completed
  LOG(INFO) << "Trace path not available for session " << trace_id
            << " in state: " << static_cast<int>(session->state);
  return "";
}

std::string TraceManager::GenerateTraceId() {
  // Generate a unique ID using timestamp + UUID
  auto now = base::Time::Now();
  auto uuid = base::Uuid::GenerateRandomV4().AsLowercaseString();

  std::stringstream ss;
  ss << "trace_" << now.ToTimeT() << "_" << uuid.substr(0, 8);
  return ss.str();
}

bool TraceManager::CreateTraceDirectory(const std::string& trace_id) {
  base::FilePath trace_dir = trace_base_dir_.Append(trace_id);

  if (!base::CreateDirectory(trace_dir)) {
    LOG(ERROR) << "Failed to create trace directory: " << trace_dir;
    return false;
  }

  // Set proper permissions (readable only by farfetchd)
  if (chmod(trace_dir.value().c_str(), S_IRWXU) != 0) {
    LOG(WARNING) << "Failed to set permissions on trace directory: "
                 << trace_dir;
  }

  return true;
}

std::unordered_set<pid_t> TraceManager::GetProcessesByComm(
    const std::vector<std::string>& process_names) {
  std::unordered_set<pid_t> pids;

  if (process_names.empty()) {
    LOG(ERROR) << "GetProcessesByComm called with empty process_names list.";
    return pids;
  }

  // Iterate through all numeric directories in /proc
  base::FileEnumerator enumerator(base::FilePath("/proc"), false,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath entry = enumerator.Next(); !entry.empty();
       entry = enumerator.Next()) {
    // Check if the directory name is a number (PID)
    pid_t pid;
    if (!base::StringToInt(entry.BaseName().value(), &pid)) {
      continue;
    }

    // Read the comm file for this process
    base::FilePath comm_path = entry.Append("comm");
    std::string comm;
    if (!base::ReadFileToString(comm_path, &comm)) {
      // Process might have disappeared, skip it
      continue;
    }

    // Remove trailing newline
    base::TrimWhitespaceASCII(comm, base::TRIM_ALL, &comm);

    // Check if this process name matches any of our targets
    for (const auto& target_name : process_names) {
      if (comm == target_name) {
        pids.insert(pid);
        break;  // Found a match, no need to check other target names
      }
    }
  }

  return pids;
}

void TraceManager::UpdateTracedPids(std::shared_ptr<TraceSession> session) {
  if (!session) {
    return;
  }

  // Update the timestamp to prevent frequent rescans
  session->last_process_scan = base::TimeTicks::Now();

  // Get the current set of matching processes
  auto new_pids = GetProcessesByComm(session->process_names);

  // Log changes for debugging if the set changed significantly
  if (new_pids.size() != session->traced_pids.size()) {
    LOG(INFO) << "Process count changed for session " << session->id << ": "
              << session->traced_pids.size() << " -> " << new_pids.size();
  }

  session->traced_pids = std::move(new_pids);
}

bool TraceManager::ShouldRescanProcesses(
    std::shared_ptr<TraceSession> session) {
  if (!session) {
    return false;
  }

  base::TimeTicks now = base::TimeTicks::Now();
  return (now - session->last_process_scan) >= kProcessRescanInterval;
}

void TraceManager::ProcessTrace(const std::string& trace_id) {
  auto it = sessions_.find(trace_id);
  if (it == sessions_.end()) {
    LOG(ERROR) << "ProcessTrace: session not found: " << trace_id;
    return;
  }

  auto session = it->second;
  if (session->state != TraceState::kProcessing) {
    LOG(WARNING) << "ProcessTrace: session " << trace_id << " is in state "
                 << static_cast<int>(session->state)
                 << ", not in processing state";
    return;
  }

  std::ifstream raw_file(session->raw_trace_path.value());
  if (!raw_file.is_open()) {
    LOG(ERROR) << "Failed to open raw trace file for reading: "
               << session->raw_trace_path;
    session->state = TraceState::kError;
    return;
  }

  std::ofstream final_file(session->final_trace_path.value());
  if (!final_file.is_open()) {
    LOG(ERROR) << "Failed to open final trace file for writing: "
               << session->final_trace_path;
    session->state = TraceState::kError;
    return;
  }

  // Write metadata header to the final trace file.
  final_file << "# Farfetchd Trace File" << std::endl;
  final_file << "# App: " << session->app_name << std::endl;
  final_file << "# Process Names: "
             << base::JoinString(session->process_names, ", ") << std::endl;
  final_file << "# Start Time: "
             << session->start_time.InSecondsFSinceUnixEpoch() << std::endl;
  final_file << "# Processing Time: "
             << base::Time::Now().InSecondsFSinceUnixEpoch() << std::endl;

  // Pre-compile allow / deny regex patterns using RE2.
  re2::RE2::Options opts;
  opts.set_log_errors(false);

  std::vector<std::unique_ptr<re2::RE2>> allow_patterns;
  for (const auto& pat : session->path_allowlist) {
    auto re = std::make_unique<re2::RE2>(pat, opts);
    if (!re->ok()) {
      LOG(ERROR) << "Invalid allow regex pattern '" << pat
                 << "': " << re->error();
    }
    allow_patterns.push_back(std::move(re));
  }

  std::vector<std::unique_ptr<re2::RE2>> deny_patterns;
  for (const auto& pat : session->path_denylist) {
    auto re = std::make_unique<re2::RE2>(pat, opts);
    if (!re->ok()) {
      LOG(ERROR) << "Invalid deny regex pattern '" << pat
                 << "': " << re->error();
    }
    deny_patterns.push_back(std::move(re));
  }

  // Include filtering criteria in the metadata for reference.
  if (!session->path_allowlist.empty()) {
    final_file << "# Allow Path Patterns: "
               << base::JoinString(session->path_allowlist, ", ") << std::endl;
  }
  if (!session->path_denylist.empty()) {
    final_file << "# Deny Path Patterns: "
               << base::JoinString(session->path_denylist, ", ") << std::endl;
  }
  final_file << std::endl;

  // Lambda that decides if |path| should be kept using pre-compiled RE2.
  auto IsPathAllowed = [&](const std::string& p) -> bool {
    for (const auto& re : deny_patterns) {
      if (re->ok() && re2::RE2::PartialMatch(p, *re)) {
        return false;  // Denied.
      }
    }
    if (!allow_patterns.empty()) {
      for (const auto& re : allow_patterns) {
        if (re->ok() && re2::RE2::PartialMatch(p, *re)) {
          return true;  // Allowed.
        }
      }
      return false;  // No allow rule matched.
    }
    // No allow list specified and not denied ⇒ keep.
    return true;
  };

  // Process the raw trace data with offline filtering.
  std::string line;
  int lines_processed = 0;
  int kept_lines = 0;
  int filtered_lines = 0;
  std::unordered_set<std::string> unique_paths;

  while (std::getline(raw_file, line)) {
    lines_processed++;

    // Keep comments and blank lines from the trace header.
    if (line.empty() || line[0] == '#') {
      final_file << line << std::endl;
      kept_lines++;
      continue;
    }

    // Extract the file path from the trace line.
    std::string path = ExtractPathFromTraceLine(line);

    // If a line has no path, keep it by default.
    // This handles trace metadata lines and events without file context.
    if (path.empty()) {
      final_file << line << std::endl;
      kept_lines++;
      continue;
    }

    // Track unique paths for statistics.
    unique_paths.insert(path);

    // Apply offline path filtering.
    if (IsPathAllowed(path)) {
      final_file << line << std::endl;
      kept_lines++;
    } else {
      filtered_lines++;
    }
  }

  raw_file.close();
  final_file.close();

  // Update session state and log processing statistics.
  session->state = TraceState::kCompleted;

  LOG(INFO) << "Completed processing trace: " << trace_id << ". "
            << "Lines processed: " << lines_processed << ", "
            << "Lines kept: " << kept_lines << ", "
            << "Lines filtered: " << filtered_lines << ", "
            << "Unique file paths: " << unique_paths.size();

  // Clean up the raw trace file to save disk space.
  if (!brillo::DeleteFile(session->raw_trace_path)) {
    LOG(WARNING) << "Failed to delete raw trace file: "
                 << session->raw_trace_path;
  }
}

std::string TraceManager::ExtractPathFromTraceLine(const std::string& line) {
  // This is a simplified parser. It assumes a format like:
  // ... event_name: ... some_field=... file="/path/to/file" ...
  // It looks for the 'file="' or 'filename="' pattern.
  // A more robust implementation would depend on the exact tracepoints enabled.
  std::string_view line_view(line);
  size_t pos = line_view.find(" file=\"");
  if (pos == std::string_view::npos) {
    pos = line_view.find(" filename=\"");
    if (pos == std::string_view::npos) {
      return "";
    }
    pos += sizeof(" filename=\"") - 1;
  } else {
    pos += sizeof(" file=\"") - 1;
  }

  size_t end_pos = line_view.find('"', pos);
  if (end_pos == std::string_view::npos) {
    return "";
  }

  return std::string(line_view.substr(pos, end_pos - pos));
}

std::string TraceManager::StartTrace(
    const std::string& app_name,
    const std::vector<std::string>& process_names,
    const std::vector<std::string>& path_allowlist,
    const std::vector<std::string>& path_denylist) {
  if (process_names.empty()) {
    LOG(ERROR) << "Cannot start trace with empty process names list";
    return "";
  }

  // Create session first.
  std::string trace_id = CreateTrace(app_name);
  if (trace_id.empty()) {
    return "";
  }

  if (!ActivateTrace(trace_id, process_names, path_allowlist, path_denylist)) {
    // Clean up the session on failure.
    sessions_.erase(trace_id);
    return "";
  }

  return trace_id;
}

void TraceManager::OnTraceTimeout(const std::string& trace_id) {
  LOG(WARNING) << "Trace session " << trace_id << " timed out after "
               << kDefaultTraceTimeout.InSeconds()
               << " seconds. Stopping automatically.";

  // Call StopTrace to perform a graceful shutdown and process any data that
  // was collected before the timeout.
  StopTrace(trace_id);
}

}  // namespace farfetchd
