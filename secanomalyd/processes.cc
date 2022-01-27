// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secanomalyd/processes.h"

#include <algorithm>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include <brillo/process/process.h>

namespace secanomalyd {

namespace {
constexpr char kPsPath[] = "/bin/ps";
constexpr char kInitExecutable[] = "/sbin/init";
}  // namespace

ProcEntry::ProcEntry(base::StringPiece proc_str) {
  // These entries are of the form:
  //   3295 4026531836 ps              ps ax -o pid,pidns,comm,args

  std::vector<base::StringPiece> fields =
      base::SplitStringPiece(proc_str, base::kWhitespaceASCII,
                             base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (fields.size() < 4) {
    pid_ = -1;
    pidns_ = 0;
    return;
  }

  // PIDs are signed.
  if (!base::StringToInt(fields[0], &pid_)) {
    pid_ = -1;
  }

  // Namespace ids are inode numbers, inode numbers are unsigned.
  if (!base::StringToUint64(fields[1], &pidns_)) {
    pidns_ = 0;
  }
  comm_ = std::string(fields[2]);
  args_ =
      base::JoinString(base::make_span(fields.begin() + 3, fields.end()), " ");
}

MaybeProcEntries ReadProcesses(ProcessFilter filter) {
  std::unique_ptr<brillo::Process> reader(new brillo::ProcessImpl());
  return ReadProcesses(reader.get(), filter);
}

MaybeProcEntries ReadProcesses(brillo::Process* reader, ProcessFilter filter) {
  // Collect processes.
  // Call |ps| with a user defined format listing pid namespaces.
  reader->AddArg(kPsPath);
  // List all processes.
  reader->AddArg("ax");
  // List pid, pid namespace, executable name, and full command line.
  reader->AddStringOption("-o", "pid,pidns,comm,args");

  reader->RedirectUsingMemory(STDOUT_FILENO);
  if (reader->Run() != 0) {
    PLOG(ERROR) << "Failed to execute 'ps'";
    return base::nullopt;
  }

  std::string processes = reader->GetOutputString(STDOUT_FILENO);
  if (processes.empty()) {
    LOG(ERROR) << "Failed to read 'ps' output";
    return base::nullopt;
  }

  return ReadProcessesFromString(processes, filter);
}

MaybeProcEntries ReadProcessesFromString(const std::string& processes,
                                         ProcessFilter filter) {
  std::vector<base::StringPiece> pieces = base::SplitStringPiece(
      processes, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  if (pieces.empty()) {
    return base::nullopt;
  }

  ProcEntries res;
  base::Optional<ino_t> init_pidns = base::nullopt;
  for (const auto& piece : pieces) {
    ProcEntry entry(piece);
    // Only add the entry to the list if it managed to parse a PID and a pidns.
    if (entry.pid() > 0 && entry.pidns() > 0) {
      if (filter == ProcessFilter::kInitPidNamespaceOnly &&
          entry.args() == std::string(kInitExecutable)) {
        init_pidns = entry.pidns();
        // The init process has been found, add it to the list and continue the
        // loop early.
        res.push_back(entry);
        continue;
      }

      // Add the entry to the list if:
      //    -Caller requested all processes, or
      //    -The init process hasn't yet been identified, or
      //    -The init process has been successfully identified, and the PID
      //     namespaces match.
      if (filter == ProcessFilter::kAll || !init_pidns ||
          entry.pidns() == init_pidns.value()) {
        res.push_back(entry);
      }
    }
  }

  if (filter == ProcessFilter::kInitPidNamespaceOnly) {
    if (init_pidns) {
      // Remove all processes whose |pidns| does not match init's.
      res.erase(std::remove_if(res.begin(), res.end(),
                               [init_pidns](const ProcEntry& pe) {
                                 return pe.pidns() != init_pidns.value();
                               }),
                res.end());
    } else {
      LOG(ERROR) << "Failed to find init process";
      return base::nullopt;
    }
  }

  // If we failed to parse any valid processes, return nullopt.
  return res.size() > 0 ? MaybeProcEntries(res) : base::nullopt;
}

}  // namespace secanomalyd
