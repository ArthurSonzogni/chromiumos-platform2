// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/garcon/service_impl.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/sockios.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>

#include "vm_tools/garcon/desktop_file.h"

namespace vm_tools {
namespace garcon {
namespace {

constexpr char kForkedProcessConsole[] = "/dev/null";

// Information about any errors that happen in the child process before the exec
// call.  This is sent back to the parent process via a socket.
struct __attribute__((packed)) ChildErrorInfo {
  enum class Reason : uint8_t {
    // Failed to set session id.
    SESSION_ID = 0,
    // Unable to open console.
    CONSOLE = 1,
    // Unable to set stdio fds.
    STDIO_FD = 2,
    // Unable to set environment variable.
    SETENV = 3,
    // Unable to reset signal handlers.
    SIGNAL_RESET = 4,
    // Failed to exec the requested program.
    EXEC = 5,
    // Failed to set the working directory.
    WORKING_DIR = 6,
  };

  union {
    // If |reason| is STDIO_FD, the fd that we failed to dup.
    int32_t fd;

    // If |reason| is SETENV, then the child process will append the key and
    // value of the environment variable pair that failed to this struct.  This
    // value tells the parent process the length of the 2 strings, including the
    // '\0' byte for each string.
    uint16_t env_length;

    // If |reason| is SIGNAL_RESET, the signal number for which we failed to set
    // the default disposition.
    int32_t signo;
  } details;

  // The errno value after the failed action.
  int32_t err;

  // Error reason.
  Reason reason;
};

// Number of defined signals that the process could receive (not including
// real time signals).
constexpr int kNumSignals = 32;

// Resets all signal handlers to the default.  This is called in child processes
// immediately before exec-ing so that signals are not unexpectedly blocked.
// Returns 0 if all signal handlers were successfully set to their default
// dispositions.  Returns the signal number of the signal for which resetting
// the signal handler failed, if any.  Callers should inspect errno for the
// error.
int ResetSignalHandlers() {
  for (int signo = 1; signo < kNumSignals; ++signo) {
    if (signo == SIGKILL || signo == SIGSTOP) {
      // sigaction returns an error if we try to set the disposition of these
      // signals to SIG_DFL.
      continue;
    }
    struct sigaction act = {
        .sa_handler = SIG_DFL, .sa_flags = 0,
    };
    sigemptyset(&act.sa_mask);

    if (sigaction(signo, &act, nullptr) != 0) {
      return signo;
    }
  }

  return 0;
}

// Performs various setup steps in the child process after calling fork() but
// before calling exec(). |error_fd| should be a valid file descriptor for a
// socket and will be used to send error information back to the parent process
// if any of the setup steps fail.
void DoChildSetup(const std::map<std::string, std::string>& env,
                  std::string working_dir,
                  int error_fd) {
  // Create a new session and process group.
  if (setsid() == -1) {
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::SESSION_ID, .err = errno,
    };

    send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
    _exit(errno);
  }

  // File descriptor for the child's stdio.
  int fd = open(kForkedProcessConsole, O_RDWR | O_NOCTTY);
  if (fd < 0) {
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::CONSOLE, .err = errno,
    };

    send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
    _exit(errno);
  }

  // Override the parent's stdio fds with the console fd.
  for (int newfd = 0; newfd < 3; ++newfd) {
    if (dup2(fd, newfd) < 0) {
      struct ChildErrorInfo info = {
          .reason = ChildErrorInfo::Reason::STDIO_FD,
          .err = errno,
          .details = {.fd = newfd},
      };

      send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
      _exit(errno);
    }
  }

  // Close the console fd, if necessary.
  if (fd >= 3) {
    close(fd);
  }

  // Set the umask back to a reasonable default.
  umask(0022);

  // Set the environment variables.
  for (const auto& pair : env) {
    if (setenv(pair.first.c_str(), pair.second.c_str(), 1) == 0) {
      continue;
    }

    // Failed to set an environment variable.  Send the error back to the
    // parent process.
    uint16_t env_length = 0;
    if (pair.first.size() + pair.second.size() + 2 <
        std::numeric_limits<uint16_t>::max()) {
      env_length =
          static_cast<uint16_t>(pair.first.size() + pair.second.size() + 2);
    }
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::SETENV,
        .err = errno,
        .details = {.env_length = env_length},
    };
    send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);

    // Also send back the offending (key, value) pair if it's not too long.
    // The pair is sent back in the format: <key>\0<value>\0.
    if (env_length != 0) {
      struct iovec iovs[] = {
          {
              .iov_base =
                  static_cast<void*>(const_cast<char*>(pair.first.data())),
              .iov_len = pair.first.size() + 1,
          },
          {
              .iov_base =
                  static_cast<void*>(const_cast<char*>(pair.second.data())),
              .iov_len = pair.second.size() + 1,
          },
      };
      struct msghdr hdr = {
          .msg_name = nullptr,
          .msg_namelen = 0,
          .msg_iov = iovs,
          .msg_iovlen = sizeof(iovs) / sizeof(iovs[0]),
          .msg_control = nullptr,
          .msg_controllen = 0,
          .msg_flags = 0,
      };
      sendmsg(error_fd, &hdr, MSG_NOSIGNAL);
    }
    _exit(errno);
  }

  // Set the working directory if requested.
  if (!working_dir.empty()) {
    if (chdir(working_dir.c_str())) {
      // If we failed, then send a failure message back to the parent process
      // and terminate the child process.
      struct ChildErrorInfo info = {
          .reason = ChildErrorInfo::Reason::WORKING_DIR, .err = errno,
      };
      send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
      _exit(errno);
    }
  }

  // Restore signal handlers and unblock all signals.
  int signo = ResetSignalHandlers();
  if (signo != 0) {
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::SIGNAL_RESET,
        .err = errno,
        .details = {.signo = signo},
    };

    send(error_fd, &info, sizeof(info), MSG_NOSIGNAL);
    _exit(errno);
  }

  // Unblock all signals.
  sigset_t mask;
  sigfillset(&mask);
  sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

// Logs information about the error that occurred in the child process.
void LogChildError(const struct ChildErrorInfo& child_info,
                   int fd,
                   const std::string& working_dir) {
  const char* msg = nullptr;
  switch (child_info.reason) {
    case ChildErrorInfo::Reason::SESSION_ID:
      msg = "Failed to set session id in child process: ";
      break;
    case ChildErrorInfo::Reason::CONSOLE:
      msg = "Failed to open console in child process: ";
      break;
    case ChildErrorInfo::Reason::STDIO_FD:
      msg = "Failed to setup stdio file descriptors in child process: ";
      break;
    case ChildErrorInfo::Reason::SETENV:
      msg = "Failed to set environment variable in child process: ";
      break;
    case ChildErrorInfo::Reason::SIGNAL_RESET:
      msg = "Failed to reset signal handler disposition in child process: ";
      break;
    case ChildErrorInfo::Reason::EXEC:
      msg = "Failed to execute requested program in child process: ";
      break;
    case ChildErrorInfo::Reason::WORKING_DIR:
      msg = "Failed to set working directory in child process: ";
      break;
  }

  LOG(ERROR) << msg << strerror(child_info.err);

  if (child_info.reason == ChildErrorInfo::Reason::STDIO_FD) {
    LOG(ERROR) << "Unable to dup console fd to " << child_info.details.fd;
    return;
  }

  if (child_info.reason == ChildErrorInfo::Reason::SIGNAL_RESET) {
    LOG(ERROR) << "Unable to set signal disposition for signal "
               << child_info.details.signo << " to SIG_DFL";
    return;
  }

  if (child_info.reason == ChildErrorInfo::Reason::SETENV &&
      child_info.details.env_length > 0) {
    auto buf = std::make_unique<char[]>(child_info.details.env_length + 1);
    if (recv(fd, buf.get(), child_info.details.env_length, 0) !=
        child_info.details.env_length) {
      PLOG(ERROR) << "Unable to fetch error details from child process";
      return;
    }
    buf[child_info.details.env_length] = '\0';

    char* key = buf.get();
    char* value = strchr(buf.get(), '\0');
    if (value - key == child_info.details.env_length) {
      LOG(ERROR) << "Missing value in SETENV error details";
      return;
    }

    // Step over the nullptr at the end of |key|.
    ++value;

    LOG(ERROR) << "Unable to set " << key << " to " << value;
  }

  if (child_info.reason == ChildErrorInfo::Reason::WORKING_DIR) {
    LOG(ERROR) << "Unable to change to dir " << working_dir;
  }
}

// Executed a process with the specified |argv| arguments using the |env|
// environment. If |working_dir| is not empty, it is executed inside of that
// working directory. Returns true on successful execution, false otherwise.
bool Spawn(std::vector<std::string> argv,
           std::map<std::string, std::string> env,
           const std::string& working_dir) {
  CHECK(!argv.empty());

  // Build the argv.
  std::vector<const char*> argv_c(argv.size());
  std::transform(
      argv.begin(), argv.end(), argv_c.begin(),
      [](const std::string& arg) -> const char* { return arg.c_str(); });
  argv_c.emplace_back(nullptr);

  // Create a pair of sockets for communicating information about the child
  // process setup.  If there was an error in any of the steps performed before
  // running execvp, then the child process will send back a ChildErrorInfo
  // struct with the error details over the socket.  If the execvp runs
  // successful then the socket will automatically be closed (because of
  // the SOCK_CLOEXEC flag) and the parent will read 0 bytes from its end
  // of the socketpair.
  int info_fds[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, info_fds) != 0) {
    PLOG(ERROR) << "Failed to create socketpair for child process";
    return false;
  }

  // Block all signals before forking to prevent signals from arriving in the
  // child.
  sigset_t mask, omask;
  sigfillset(&mask);
  sigprocmask(SIG_BLOCK, &mask, &omask);

  pid_t pid = fork();
  if (pid < 0) {
    PLOG(ERROR) << "Failed to fork";
    return false;
  }

  if (pid == 0) {
    // Child process.
    close(info_fds[0]);

    DoChildSetup(env, working_dir, info_fds[1]);

    // Launch the process.
    execvp(argv_c[0], const_cast<char* const*>(argv_c.data()));

    // execvp never returns except in case of an error.
    struct ChildErrorInfo info = {
        .reason = ChildErrorInfo::Reason::EXEC, .err = errno,
    };

    send(info_fds[1], &info, sizeof(info), MSG_NOSIGNAL);
    _exit(errno);
  }

  // Parent process.
  close(info_fds[1]);
  struct ChildErrorInfo child_info = {};
  ssize_t ret = recv(info_fds[0], &child_info, sizeof(child_info), 0);

  bool retval = false;
  // There are 3 possibilities here:
  //   - The process setup completed successfully and the program was launched.
  //     In this case the socket fd in the child process will be closed on
  //     exec and ret will be 0.
  //   - An error occurred during setup.  ret will be sizeof(child_info).
  //   - An error occurred during the recv.  In this case we assume the child
  //     setup was successful.  If it wasn't, we'll find out about it through
  //     the normal child reaping mechanism.
  if (ret == sizeof(child_info)) {
    // Error occurred in the child.
    LogChildError(child_info, info_fds[0], working_dir);

    // Reap the child process here since we know it already failed.
    int status = 0;
    pid_t child = waitpid(pid, &status, 0);
    DCHECK_EQ(child, pid);
  } else if (ret < 0) {
    PLOG(ERROR) << "Failed to receive information about child process setup";
  } else {
    CHECK_EQ(ret, 0);
    retval = true;
  }
  close(info_fds[0]);
  // Restore the signal mask.
  sigprocmask(SIG_SETMASK, &omask, nullptr);
  return retval;
}

}  // namespace

grpc::Status ServiceImpl::LaunchApplication(
    grpc::ServerContext* ctx,
    const vm_tools::container::LaunchApplicationRequest* request,
    vm_tools::container::LaunchApplicationResponse* response) {
  LOG(INFO) << "Received request to launch application in container";

  if (request->desktop_file_id().empty()) {
    return grpc::Status(grpc::INVALID_ARGUMENT, "missing desktop_file_id");
  }

  // Find the actual file path that corresponds to this desktop file id.
  base::FilePath file_path =
      DesktopFile::FindFileForDesktopId(request->desktop_file_id());
  if (file_path.empty()) {
    response->set_success(false);
    response->set_failure_reason("Desktop file does not exist");
    return grpc::Status::OK;
  }

  // Now parse the actual desktop file.
  std::unique_ptr<DesktopFile> desktop_file =
      DesktopFile::ParseDesktopFile(file_path);
  if (!desktop_file) {
    response->set_success(false);
    response->set_failure_reason("Desktop file contents are invalid");
    return grpc::Status::OK;
  }

  // Make sure this desktop file is for an application.
  if (!desktop_file->IsApplication()) {
    response->set_success(false);
    response->set_failure_reason("Desktop file is not for an application");
    return grpc::Status::OK;
  }

  // Get the argv string from the desktop file we need for execution.
  // TODO(jkardatzke): Add handling of file/URL arguments for applications.
  std::vector<std::string> argv =
      desktop_file->GenerateArgvWithFiles(std::vector<std::string>());
  if (argv.empty()) {
    response->set_success(false);
    response->set_failure_reason(
        "Failure in generating argv list for application");
    return grpc::Status::OK;
  }

  if (!Spawn(std::move(argv), std::map<std::string, std::string>(),
             desktop_file->path())) {
    response->set_success(false);
    response->set_failure_reason("Failure in execution of application");
  } else {
    response->set_success(true);
  }

  // Return OK no matter what because the RPC itself succeeded even if there
  // was an issue with launching the process.
  return grpc::Status::OK;
}

}  // namespace garcon
}  // namespace vm_tools
