// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/subprocess.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/process.h>
#include <chromeos/secure_blob.h>

using chromeos::MessageLoop;
using std::string;
using std::unique_ptr;
using std::vector;

namespace chromeos_update_engine {

namespace {

bool SetupChild(const std::map<string, string>& env, uint32_t flags) {
  // Setup the environment variables.
  clearenv();
  for (const auto& key_value : env) {
    setenv(key_value.first.c_str(), key_value.second.c_str(), 0);
  }

  if ((flags & Subprocess::kRedirectStderrToStdout) != 0) {
    if (HANDLE_EINTR(dup2(STDOUT_FILENO, STDERR_FILENO)) != STDERR_FILENO)
      return false;
  }

  int fd = HANDLE_EINTR(open("/dev/null", O_RDONLY));
  if (fd < 0)
    return false;
  if (HANDLE_EINTR(dup2(fd, STDIN_FILENO)) != STDIN_FILENO)
    return false;
  IGNORE_EINTR(close(fd));

  return true;
}

// Helper function to launch a process with the given Subprocess::Flags.
// This function only sets up and starts the process according to the |flags|.
// The caller is responsible for watching the termination of the subprocess.
// Return whether the process was successfully launched and fills in the |proc|
// Process.
bool LaunchProcess(const vector<string>& cmd,
                   uint32_t flags,
                   chromeos::Process* proc) {
  for (const string& arg : cmd)
    proc->AddArg(arg);
  proc->SetSearchPath((flags & Subprocess::kSearchPath) != 0);

  // Create an environment for the child process with just the required PATHs.
  std::map<string, string> env;
  for (const char* key : {"LD_LIBRARY_PATH", "PATH"}) {
    const char* value = getenv(key);
    if (value)
      env.emplace(key, value);
  }

  proc->RedirectUsingPipe(STDOUT_FILENO, false);
  proc->SetPreExecCallback(base::Bind(&SetupChild, env, flags));

  return proc->Start();
}

}  // namespace

void Subprocess::Init() {
  if (subprocess_singleton_ == this)
    return;
  CHECK(subprocess_singleton_ == nullptr);
  subprocess_singleton_ = this;

  async_signal_handler_.Init();
  process_reaper_.Register(&async_signal_handler_);
}

Subprocess::~Subprocess() {
  if (subprocess_singleton_ == this)
    subprocess_singleton_ = nullptr;
}

void Subprocess::OnStdoutReady(SubprocessRecord* record) {
  char buf[1024];
  ssize_t rc = 0;
  do {
    rc = HANDLE_EINTR(read(record->stdout_fd, buf, arraysize(buf)));
    if (rc < 0) {
      // EAGAIN and EWOULDBLOCK are normal return values when there's no more
      // input as we are in non-blocking mode.
      if (errno != EWOULDBLOCK && errno != EAGAIN) {
        PLOG(ERROR) << "Error reading fd " << record->stdout_fd;
        MessageLoop::current()->CancelTask(record->stdout_task_id);
        record->stdout_task_id = MessageLoop::kTaskIdNull;
      }
    } else if (rc == 0) {
      // A value of 0 means that the child closed its end of the pipe and there
      // is nothing else to read from stdout.
      MessageLoop::current()->CancelTask(record->stdout_task_id);
      record->stdout_task_id = MessageLoop::kTaskIdNull;
    } else {
      record->stdout.append(buf, rc);
    }
  } while (rc > 0);
}

void Subprocess::ChildExitedCallback(const siginfo_t& info) {
  auto pid_record = subprocess_records_.find(info.si_pid);
  if (pid_record == subprocess_records_.end())
    return;
  SubprocessRecord* record = pid_record->second.get();

  // Make sure we read any remaining process output and then close the pipe.
  OnStdoutReady(record);

  MessageLoop::current()->CancelTask(record->stdout_task_id);
  record->stdout_task_id = MessageLoop::kTaskIdNull;

  // Release and close all the pipes now.
  record->proc.Release();
  record->proc.Reset(0);

  // Don't print any log if the subprocess exited with exit code 0.
  if (info.si_code != CLD_EXITED) {
    LOG(INFO) << "Subprocess terminated with si_code " << info.si_code;
  } else if (info.si_status != 0) {
    LOG(INFO) << "Subprocess exited with si_status: " << info.si_status;
  }

  if (!record->stdout.empty()) {
    LOG(INFO) << "Subprocess output:\n" << record->stdout;
  }
  if (!record->callback.is_null()) {
    record->callback.Run(info.si_status, record->stdout);
  }
  subprocess_records_.erase(pid_record);
}

pid_t Subprocess::Exec(const vector<string>& cmd,
                       const ExecCallback& callback) {
  return ExecFlags(cmd, kRedirectStderrToStdout, callback);
}

pid_t Subprocess::ExecFlags(const vector<string>& cmd,
                            uint32_t flags,
                            const ExecCallback& callback) {
  unique_ptr<SubprocessRecord> record(new SubprocessRecord(callback));

  if (!LaunchProcess(cmd, flags, &record->proc)) {
    LOG(ERROR) << "Failed to launch subprocess";
    return 0;
  }

  pid_t pid = record->proc.pid();
  CHECK(process_reaper_.WatchForChild(FROM_HERE, pid, base::Bind(
      &Subprocess::ChildExitedCallback,
      base::Unretained(this))));

  record->stdout_fd = record->proc.GetPipe(STDOUT_FILENO);
  // Capture the subprocess output. Make our end of the pipe non-blocking.
  int fd_flags = fcntl(record->stdout_fd, F_GETFL, 0) | O_NONBLOCK;
  if (HANDLE_EINTR(fcntl(record->stdout_fd, F_SETFL, fd_flags)) < 0) {
    LOG(ERROR) << "Unable to set non-blocking I/O mode on fd "
               << record->stdout_fd << ".";
  }

  record->stdout_task_id = MessageLoop::current()->WatchFileDescriptor(
      FROM_HERE,
      record->stdout_fd,
      MessageLoop::WatchMode::kWatchRead,
      true,
      base::Bind(&Subprocess::OnStdoutReady, record.get()));

  subprocess_records_[pid].reset(record.release());
  return pid;
}

void Subprocess::KillExec(pid_t pid) {
  auto pid_record = subprocess_records_.find(pid);
  if (pid_record == subprocess_records_.end())
    return;
  pid_record->second->callback.Reset();
  kill(pid, SIGTERM);
}

bool Subprocess::SynchronousExec(const vector<string>& cmd,
                                 int* return_code,
                                 string* stdout) {
  // The default for SynchronousExec is to use kSearchPath since the code relies
  // on that.
  return SynchronousExecFlags(
      cmd,
      kRedirectStderrToStdout | kSearchPath,
      return_code,
      stdout);
}

bool Subprocess::SynchronousExecFlags(const vector<string>& cmd,
                                      uint32_t flags,
                                      int* return_code,
                                      string* stdout) {
  chromeos::ProcessImpl proc;
  if (!LaunchProcess(cmd, flags, &proc)) {
    LOG(ERROR) << "Failed to launch subprocess";
    return false;
  }

  if (stdout) {
    stdout->clear();
  }

  int fd = proc.GetPipe(STDOUT_FILENO);
  vector<char> buffer(32 * 1024);
  while (true) {
    int rc = HANDLE_EINTR(read(fd, buffer.data(), buffer.size()));
    if (rc < 0) {
      PLOG(ERROR) << "Reading from child's output";
      break;
    } else if (rc == 0) {
      break;
    } else {
      if (stdout)
        stdout->append(buffer.data(), rc);
    }
  }
  // At this point, the subprocess already closed the output, so we only need to
  // wait for it to finish.
  int proc_return_code = proc.Wait();
  if (return_code)
    *return_code = proc_return_code;
  return proc_return_code != chromeos::Process::kErrorExitStatus;
}

bool Subprocess::SubprocessInFlight() {
  for (const auto& pid_record : subprocess_records_) {
    if (!pid_record.second->callback.is_null())
      return true;
  }
  return false;
}

Subprocess* Subprocess::subprocess_singleton_ = nullptr;

}  // namespace chromeos_update_engine
