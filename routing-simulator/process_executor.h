// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ROUTING_SIMULATOR_PROCESS_EXECUTOR_H_
#define ROUTING_SIMULATOR_PROCESS_EXECUTOR_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>

namespace routing_simulator {

class ProcessExecutor {
 public:
  static std::unique_ptr<ProcessExecutor> Create();

  virtual ~ProcessExecutor() = default;

  // ProcessExecutor is neither copyable nor movable.
  ProcessExecutor(const ProcessExecutor&) = delete;
  ProcessExecutor& operator=(const ProcessExecutor&) = delete;

  // Executes `program` with `args`.
  // - If the process exits successfully (exit with 0), return the contents of
  //   stdout.
  // - If not, return std::nullopt, and LOG stdout and stderr if available.
  virtual std::optional<std::string> RunAndGetStdout(
      const base::FilePath& program, const std::vector<std::string>& args) = 0;

 protected:
  ProcessExecutor() = default;
};

}  // namespace routing_simulator

#endif  // ROUTING_SIMULATOR_PROCESS_EXECUTOR_H_
