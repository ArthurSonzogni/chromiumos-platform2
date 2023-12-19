// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/process_manager.h"

#include <unistd.h>

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>

using std::string;
using std::vector;

std::unique_ptr<brillo::Process> ProcessManager::CreateProcess(
    const vector<string>& cmd,
    const ProcessManagerInterface::IORedirection& io_redirection) {
  std::unique_ptr<brillo::Process> process(new brillo::ProcessImpl);
  for (const auto& arg : cmd)
    process->AddArg(arg);
  if (!io_redirection.input.empty())
    process->RedirectInput(io_redirection.input);
  if (!io_redirection.output.empty())
    process->RedirectOutput(io_redirection.output);
  return process;
}

int ProcessManager::RunCommand(
    const vector<string>& cmd,
    const ProcessManagerInterface::IORedirection& io_redirection) {
  auto process = CreateProcess(cmd, io_redirection);
  return process->Run();
}

bool ProcessManager::RunBackgroundCommand(
    const vector<string>& cmd,
    const ProcessManagerInterface::IORedirection& io_redirection,
    pid_t* pid) {
  auto process = CreateProcess(cmd, io_redirection);
  if (!process->Start())
    return false;
  *pid = process->pid();
  // Need to release the process so it's not destructed at return.
  process->Release();
  return true;
}

bool ProcessManager::RunCommandWithOutput(const vector<string>& cmd,
                                          int* return_code,
                                          string* stdout_out,
                                          string* stderr_out) {
  brillo::ProcessImpl process;
  for (const auto& arg : cmd)
    process.AddArg(arg);

  if (stdout_out)
    process.RedirectUsingMemory(STDOUT_FILENO);
  if (stderr_out)
    process.RedirectUsingMemory(STDERR_FILENO);
  const auto exit_code = process.Run();

  if (stdout_out)
    *stdout_out = process.GetOutputString(STDOUT_FILENO);
  if (stderr_out)
    *stderr_out = process.GetOutputString(STDERR_FILENO);

  if (return_code)
    *return_code = exit_code;
  return exit_code != brillo::Process::kErrorExitStatus;
}
