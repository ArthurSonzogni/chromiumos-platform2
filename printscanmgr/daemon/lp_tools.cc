// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printscanmgr/daemon/lp_tools.h"

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <brillo/process/process.h>

#include "printscanmgr/cups_uri_helper/cups_uri_helper_utils.h"

namespace printscanmgr {

namespace {

constexpr char kLpadminCommand[] = "/usr/sbin/lpadmin";
constexpr char kLpstatCommand[] = "/usr/bin/lpstat";
constexpr char kTestPPDCommand[] = "/usr/bin/cupstestppd";

constexpr char kLanguageEnvironmentVariable[] = "CROS_CUPS_LANGUAGE";

}  // namespace

int LpToolsImpl::RunCommand(const std::string& command,
                            const std::vector<std::string>& arg_list,
                            const std::vector<uint8_t>* std_input,
                            std::string* out) const {
  brillo::ProcessImpl process;
  process.RedirectOutputToMemory(/*combine=*/false);

  // TODO(b/340126451): Remove once the root cause of printscanmgr not being
  // able to run lpadmin when the printer is behind a VPN has been fixed. Also
  // remove the other sandboxing additions from crrev.com/c/5664042 and
  // crrev.com/c/5676420.
  if (command == kLpadminCommand) {
    process.SetUid(269);  // lpadmin user
    process.SetGid(7);    // lp group
  }

  process.AddArg(command);
  for (const std::string& arg : arg_list) {
    process.AddArg(arg);
  }

  // Starts a process, writes data from the buffer to its standard input and
  // waits for the process to finish.
  int result = kRunError;
  process.RedirectUsingPipe(STDIN_FILENO, true);
  if (process.Start()) {
    // Ignore SIGPIPE.
    const struct sigaction kSigIgn = {.sa_handler = SIG_IGN,
                                      .sa_flags = SA_RESTART};
    struct sigaction old_sa;
    if (sigaction(SIGPIPE, &kSigIgn, &old_sa)) {
      PLOG(ERROR) << "sigaction failed";
      return 1;
    }
    // Restore the old signal handler at the end of the scope.
    const base::ScopedClosureRunner kRestoreSignal(base::BindOnce(
        [](const struct sigaction& sa) {
          if (sigaction(SIGPIPE, &sa, nullptr)) {
            PLOG(ERROR) << "sigaction failed";
          }
        },
        old_sa));
    int stdin_fd = process.GetPipe(STDIN_FILENO);

    if (std_input) {
      if (!base::WriteFileDescriptor(stdin_fd, *std_input)) {
        LOG(ERROR) << "Writing file descriptor failed for process: " << command;
      }
    }
    if (IGNORE_EINTR(close(stdin_fd)) != 0) {
      LOG(ERROR) << "Closing file descriptor failed with errno: " << errno;
    }
    result = process.Wait();
    if (out) {
      *out = process.GetOutputString(STDOUT_FILENO);
    }
  }

  if (result != 0) {
    std::string error_msg = process.GetOutputString(STDERR_FILENO);
    LOG(ERROR) << "Child process exited with status " << result;
    LOG(ERROR) << "stderr was: " << error_msg;
  }

  return result;
}

// Runs lpadmin with the provided |arg_list| and |std_input|.
int LpToolsImpl::Lpadmin(const std::vector<std::string>& arg_list,
                         const std::optional<std::string>& language,
                         const std::vector<uint8_t>* std_input) {
  if (!language.has_value()) {
    return RunCommand(kLpadminCommand, arg_list, std_input);
  }

  const char* prev_language = getenv(kLanguageEnvironmentVariable);
  setenv(kLanguageEnvironmentVariable, language->c_str(), /*overwrite=*/1);
  int ret = RunCommand(kLpadminCommand, arg_list, std_input);
  if (prev_language) {
    setenv(kLanguageEnvironmentVariable, prev_language, /*overwrite=*/1);
  } else {
    unsetenv(kLanguageEnvironmentVariable);
  }
  return ret;
}

// Runs lpstat with the provided |arg_list| and |std_input|.
int LpToolsImpl::Lpstat(const std::vector<std::string>& arg_list,
                        std::string* output) {
  return RunCommand(kLpstatCommand, arg_list, /*std_input=*/nullptr, output);
}

int LpToolsImpl::CupsTestPpd(const std::vector<uint8_t>& ppd_content) const {
  std::string output;
  int retval = RunCommand(kTestPPDCommand,
                          {"-W", "translations", "-W", "constraints", "-"},
                          &ppd_content, &output);
  // If there was an error running cupstestppd, log just the failure lines since
  // logging all of the output can be too noisy.  However, if there are no
  // failure lines, just log everything.
  if (retval != 0) {
    const std::string fail_string = "FAIL";
    const bool log_everything = output.find(fail_string) == std::string::npos;
    LOG(ERROR) << "CupsTestPpd failures: ";
    for (const std::string& line : base::SplitString(
             output, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      if (log_everything || line.find(fail_string) != std::string::npos) {
        LOG(ERROR) << line;
      }
    }
  }
  return retval;
}

bool LpToolsImpl::CupsUriHelper(const std::string& uri) const {
  return cups_helper::UriSeemsReasonable(uri);
}

}  // namespace printscanmgr
