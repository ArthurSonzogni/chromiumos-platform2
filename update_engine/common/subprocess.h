// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_SUBPROCESS_H_
#define UPDATE_ENGINE_COMMON_SUBPROCESS_H_

#include <unistd.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <brillo/asynchronous_signal_handler_interface.h>
#include <brillo/message_loops/message_loop.h>
#ifdef __CHROMEOS__
#include <brillo/process/process.h>
#include <brillo/process/process_reaper.h>
#else
#include <brillo/process.h>
#include <brillo/process_reaper.h>
#endif  // __CHROMEOS__
#include <gtest/gtest_prod.h>

// The Subprocess class is a singleton. It's used to spawn off a subprocess
// and get notified when the subprocess exits. The result of Exec() can
// be saved and used to cancel the callback request and kill your process. If
// you know you won't call KillExec(), you may safely lose the return value
// from Exec().

// To create the Subprocess singleton just instantiate it with and call Init().
// You can't have two Subprocess instances initialized at the same time.

namespace chromeos_update_engine {

class Subprocess {
 public:
  enum Flags {
    kSearchPath = 1 << 0,
    kRedirectStderrToStdout = 1 << 1,
  };

  // Callback type used when an async process terminates. It receives the exit
  // code and the stdout output (and stderr if redirected).
  using ExecCallback = base::OnceCallback<void(int, const std::string&)>;

  Subprocess() = default;
  Subprocess(const Subprocess&) = delete;
  Subprocess& operator=(const Subprocess&) = delete;

  // Destroy and unregister the Subprocess singleton.
  ~Subprocess();

  // Initialize and register the Subprocess singleton.
  void Init(brillo::AsynchronousSignalHandlerInterface* async_signal_handler);

  // Launches a process in the background and calls the passed |callback| when
  // the process exits. The file descriptors specified in |output_pipes| will
  // be available in the child as the writer end of a pipe. Use GetPipeFd() to
  // know the reader end in the parent. Only stdin, stdout, stderr and the file
  // descriptors in |output_pipes| will be open in the child.
  // Returns the process id of the new launched process or 0 in case of failure.
  pid_t Exec(const std::vector<std::string>& cmd, ExecCallback callback);
  pid_t ExecFlags(const std::vector<std::string>& cmd,
                  uint32_t flags,
                  const std::vector<int>& output_pipes,
                  ExecCallback callback);

  // Kills the running process with SIGTERM and ignores the callback.
  void KillExec(pid_t pid);

  // Return the parent end of the pipe mapped onto |fd| in the child |pid|. This
  // file descriptor is available until the callback for the child |pid|
  // returns. After that the file descriptor will be closed. The passed |fd|
  // must be one of the file descriptors passed to ExecFlags() in
  // |output_pipes|, otherwise returns -1.
  int GetPipeFd(pid_t pid, int fd) const;

  // Executes a command synchronously. Returns true on success. If |stdout| is
  // non-null, the process output is stored in it, otherwise the output is
  // logged.
  static bool SynchronousExec(const std::vector<std::string>& cmd,
                              int* return_code,
                              std::string* stdout,
                              std::string* stderr);
  static bool SynchronousExecFlags(const std::vector<std::string>& cmd,
                                   uint32_t flags,
                                   int* return_code,
                                   std::string* stdout,
                                   std::string* stderr);

  // Gets the one instance.
  static Subprocess& Get() { return *subprocess_singleton_; }

  // Tries to log all in flight processes's output. It is used right before
  // exiting the update_engine, probably when the subprocess caused a system
  // shutdown.
  void FlushBufferedLogsAtExit();

 private:
  FRIEND_TEST(SubprocessTest, CancelTest);

  struct SubprocessRecord {
    explicit SubprocessRecord(ExecCallback callback)
        : callback(std::move(callback)) {}

    // The callback supplied by the caller.
    ExecCallback callback;

    // The ProcessImpl instance managing the child process. Destroying this
    // will close our end of the pipes we have open.
    brillo::ProcessImpl proc;

    // These are used to monitor the stdout of the running process, including
    // the stderr if it was redirected.
    std::unique_ptr<base::FileDescriptorWatcher::Controller> stdout_controller;

    int stdout_fd{-1};
    std::string stdout;
  };

  // Callback which runs whenever there is input available on the subprocess
  // stdout pipe.
  static void OnStdoutReady(SubprocessRecord* record);

  // Callback for when any subprocess terminates. This calls the user
  // requested callback.
  void ChildExitedCallback(const siginfo_t& info);

  // The global instance.
  static Subprocess* subprocess_singleton_;

  // A map from the asynchronous subprocess tag (see Exec) to the subprocess
  // record structure for all active asynchronous subprocesses.
  std::map<pid_t, std::unique_ptr<SubprocessRecord>> subprocess_records_;

  // Used to watch for child processes.
  brillo::ProcessReaper process_reaper_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_SUBPROCESS_H_
