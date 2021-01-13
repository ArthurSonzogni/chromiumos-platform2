// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/process_manager.h"

using std::string;
using std::vector;

std::unique_ptr<brillo::Process> ProcessManager::CreateProcess(
    const vector<string>& cmd, const IORedirection& io_redirection) {
  std::unique_ptr<brillo::Process> process(new brillo::ProcessImpl);
  for (const auto& arg : cmd)
    process->AddArg(arg);
  if (!io_redirection.input.empty())
    process->RedirectInput(io_redirection.input);
  if (!io_redirection.output.empty())
    process->RedirectOutput(io_redirection.output);
  return process;
}

int ProcessManager::RunCommand(const vector<string>& cmd,
                               const IORedirection& io_redirection) {
  auto process = CreateProcess(cmd, io_redirection);
  return process->Run();
}

bool ProcessManager::RunBackgroundCommand(const vector<string>& cmd,
                                          const IORedirection& io_redirection,
                                          pid_t* pid) {
  auto process = CreateProcess(cmd, io_redirection);
  if (!process->Start())
    return false;
  *pid = process->pid();
  // Need to release the process so it's not destructed at return.
  process->Release();
  return true;
}
