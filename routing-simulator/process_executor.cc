// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/process_executor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>

namespace routing_simulator {
namespace {

class ProcessExecutorImpl : public ProcessExecutor {
 public:
  ProcessExecutorImpl() = default;
  ~ProcessExecutorImpl() override = default;

  std::optional<std::string> RunAndGetStdout(
      const base::FilePath& program,
      const std::vector<std::string> args) override;
};

std::optional<std::string> ProcessExecutorImpl::RunAndGetStdout(
    const base::FilePath& program, const std::vector<std::string> args) {
  brillo::ProcessImpl process;

  process.AddArg(program.value());
  for (const auto& arg : args) {
    process.AddArg(arg);
  }

  // Redirect stdout and stderr to memory.
  process.RedirectOutputToMemory(/*combine=*/false);

  int process_ret = process.Run();
  std::string stdout_str = process.GetOutputString(STDOUT_FILENO);
  std::string stderr_str = process.GetOutputString(STDERR_FILENO);

  std::string logging_tag = base::StrCat(
      {"`", program.value(), " ", base::JoinString(args, " "), "`"});

  if (process_ret == 0) {
    if (!stderr_str.empty()) {
      LOG(WARNING) << logging_tag << " stderr: " << stderr_str;
    }
    return std::move(stdout_str);
  }

  LOG(ERROR) << "Failed to execute " << logging_tag
             << ", Process::Run() returned " << process_ret;
  LOG(ERROR) << "stdout: " << stdout_str;
  LOG(ERROR) << "stderr: " << stderr_str;
  return std::nullopt;
}

}  // namespace

std::unique_ptr<ProcessExecutor> ProcessExecutor::Create() {
  return std::make_unique<ProcessExecutorImpl>();
}

}  // namespace routing_simulator
