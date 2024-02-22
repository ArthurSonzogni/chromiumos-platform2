// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/tc_process.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <net-base/process_manager.h>

#include "shill/logging.h"

namespace shill {
namespace Logging {
static auto kModuleLogScope = ScopeLogger::kTC;
}  // namespace Logging

std::unique_ptr<TCProcess> TCProcess::Create(
    const std::vector<std::string>& commands,
    ExitCallback exit_callback,
    net_base::ProcessManager* process_manager) {
  CHECK(process_manager != nullptr);

  std::unique_ptr<TCProcess> tc_process = base::WrapUnique(
      new TCProcess(process_manager, commands, std::move(exit_callback)));
  if (!tc_process->Initialize()) {
    return nullptr;
  }
  return tc_process;
}

TCProcess::TCProcess(net_base::ProcessManager* process_manager,
                     const std::vector<std::string>& commands,
                     ExitCallback exit_callback)
    : process_manager_(process_manager),
      commands_(commands),
      exit_callback_(std::move(exit_callback)) {}

TCProcess::~TCProcess() {
  if (tc_pid_ != net_base::ProcessManager::kInvalidPID) {
    process_manager_->StopProcess(tc_pid_);
  }
}

bool TCProcess::Initialize() {
  const std::vector<std::string> args = {
      "-f",  // Continue if there is a failure or no-op
      "-b",  // Batch mode
      "-"    // Use stdin for input
  };
  const net_base::ProcessManager::MinijailOptions minijail_options{
      .user = std::string(kTCUser),
      .group = std::string(kTCGroup),
      .capmask = CAP_TO_MASK(CAP_NET_ADMIN),
      .inherit_supplementary_groups = false,
  };

  // shill's stderr is wired to syslog, so nullptr for stderr
  // here implies the tc process's errors show up in /var/log/net.log.
  int stdin_fd = net_base::ProcessManager::kInvalidPID;
  struct net_base::std_file_descriptors std_fds {
    &stdin_fd, nullptr, nullptr
  };
  tc_pid_ = process_manager_->StartProcessInMinijailWithPipes(
      FROM_HERE, base::FilePath(kTCPath), args, {}, minijail_options,
      base::BindOnce(&TCProcess::OnProcessExited, weak_factory_.GetWeakPtr()),
      std_fds);
  if (tc_pid_ == net_base::ProcessManager::kInvalidPID) {
    LOG(ERROR) << "Failed to start TC process";
    return false;
  }
  SLOG(1) << "Spawned tc with pid: " << tc_pid_;

  tc_stdin_ = base::ScopedFD(stdin_fd);
  if (!base::SetNonBlocking(tc_stdin_.get())) {
    LOG(ERROR) << "Unable to set TC pipes to be non-blocking";
    return false;
  }
  tc_stdin_watcher_ = base::FileDescriptorWatcher::WatchWritable(
      tc_stdin_.get(), base::BindRepeating(&TCProcess::OnTCProcessWritable,
                                           base::Unretained(this)));
  return true;
}

void TCProcess::OnTCProcessWritable() {
  for (const auto& command : commands_) {
    SLOG(2) << "Issuing tc command: " << command;
    if (!base::WriteFileDescriptor(tc_stdin_.get(), command)) {
      PLOG(ERROR) << "Failed to write command to TC process: " << command;
      break;
    }
  }

  tc_stdin_watcher_.reset();
  tc_stdin_.reset();
}

void TCProcess::OnProcessExited(int exit_status) {
  tc_pid_ = net_base::ProcessManager::kInvalidPID;
  std::move(exit_callback_).Run(exit_status);
}

std::unique_ptr<TCProcess> TCProcessFactory::Create(
    const std::vector<std::string>& commands,
    TCProcess::ExitCallback exit_callback,
    net_base::ProcessManager* process_manager) {
  return TCProcess::Create(commands, std::move(exit_callback), process_manager);
}

}  // namespace shill
