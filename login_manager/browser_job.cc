// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class is most definitely NOT re-entrant.

#include "login_manager/browser_job.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <queue>
#include <utility>

#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <chromeos/switches/chrome_switches.h>

#include "login_manager/file_checker.h"
#include "login_manager/login_metrics.h"
#include "login_manager/subprocess.h"
#include "login_manager/system_utils.h"

namespace login_manager {

const char BrowserJobInterface::kLoginManagerFlag[] = "--login-manager";
const char BrowserJobInterface::kLoginUserFlag[] = "--login-user=";
const char BrowserJobInterface::kLoginProfileFlag[] = "--login-profile=";

const char BrowserJob::kFirstExecAfterBootFlag[] = "--first-exec-after-boot";

const int BrowserJob::kUseExtraArgsRuns = 3;
static_assert(BrowserJob::kUseExtraArgsRuns > 1,
              "kUseExtraArgsRuns should be greater than 1 because extra "
              "arguments could need one restart to apply them.");

const int BrowserJob::kRestartTries = BrowserJob::kUseExtraArgsRuns + 2;
const time_t BrowserJob::kRestartWindowSeconds = 60;

namespace {

constexpr char kVmoduleFlag[] = "--vmodule=";
constexpr char kEnableFeaturesFlag[] = "--enable-features=";
constexpr char kDisableFeaturesFlag[] = "--disable-features=";
constexpr char kEnableBlinkFeaturesFlag[] = "--enable-blink-features=";
constexpr char kSafeModeFlag[] = "--safe-mode";

// Erases all occurrences of |arg| within |args|. Returns true if any entries
// were removed or false otherwise.
bool RemoveArgs(std::vector<std::string>* args, const std::string& arg) {
  std::vector<std::string>::iterator new_end =
      std::remove(args->begin(), args->end(), arg);
  if (new_end == args->end())
    return false;

  args->erase(new_end, args->end());
  return true;
}

// Joins the values of all switches in |args| prefixed by |prefix| using
// |separator| and appends a merged version of the switch. If |keep_existing| is
// true, all earlier occurrences of the switch are preserved; otherwise, they
// are removed.
void MergeSwitches(std::vector<std::string>* args,
                   const std::string& prefix,
                   const std::string& separator,
                   bool keep_existing) {
  std::string values;
  auto head = args->begin();
  for (const auto arg : *args) {
    bool match = base::StartsWith(arg, prefix, base::CompareCase::SENSITIVE);
    if (match) {
      if (!values.empty())
        values += separator;
      values += arg.substr(prefix.size());
    }
    if (!match || keep_existing) {
      *head++ = arg;
    }
  }
  if (head != args->end())
    args->erase(head, args->end());
  if (!values.empty())
    args->push_back(prefix + values);
}

}  // namespace

BrowserJob::BrowserJob(const std::vector<std::string>& arguments,
                       const std::vector<std::string>& environment_variables,
                       FileChecker* checker,
                       LoginMetrics* metrics,
                       SystemUtils* utils,
                       std::unique_ptr<SubprocessInterface> subprocess)
    : arguments_(arguments),
      environment_variables_(environment_variables),
      file_checker_(checker),
      login_metrics_(metrics),
      system_(utils),
      start_times_(std::deque<time_t>(kRestartTries, 0)),
      removed_login_manager_flag_(false),
      session_already_started_(false),
      subprocess_(std::move(subprocess)) {
  // Take over managing kLoginManagerFlag.
  if (RemoveArgs(&arguments_, kLoginManagerFlag)) {
    removed_login_manager_flag_ = true;
    login_arguments_.push_back(kLoginManagerFlag);
  }
}

BrowserJob::~BrowserJob() {}

pid_t BrowserJob::CurrentPid() const {
  return subprocess_->GetPid();
}

bool BrowserJob::ShouldRunBrowser() {
  return !file_checker_ || !file_checker_->exists();
}

bool BrowserJob::ShouldStop() const {
  return (system_->time(nullptr) - start_times_.front() <
          kRestartWindowSeconds);
}

void BrowserJob::RecordTime() {
  start_times_.push_back(system_->time(nullptr));
  start_times_.pop_front();
  DCHECK_EQ(kRestartTries, start_times_.size());
}

bool BrowserJob::RunInBackground() {
  CHECK(login_metrics_);
  bool first_boot = !login_metrics_->HasRecordedChromeExec();
  login_metrics_->RecordStats("chrome-exec");

  extra_one_time_arguments_.clear();
  if (first_boot)
    extra_one_time_arguments_.push_back(kFirstExecAfterBootFlag);

  const std::vector<std::string> argv(ExportArgv());
  const std::vector<std::string> env_vars(ExportEnvironmentVariables());
  LOG(INFO) << "Running browser " << base::JoinString(argv, " ");
  RecordTime();
  return subprocess_->ForkAndExec(argv, env_vars);
}

void BrowserJob::KillEverything(int signal, const std::string& message) {
  if (subprocess_->GetPid() < 0)
    return;

  LOG(INFO) << "Terminating process group for browser " << subprocess_->GetPid()
            << " with signal " << signal << ": " << message;
  subprocess_->KillEverything(signal);
}

void BrowserJob::Kill(int signal, const std::string& message) {
  const pid_t pid = subprocess_->GetPid();
  if (pid < 0)
    return;

  LOG(INFO) << "Terminating browser process " << pid << " with signal "
            << signal << ": " << message;
  subprocess_->Kill(signal);
}

void BrowserJob::WaitAndAbort(base::TimeDelta timeout) {
  const pid_t pid = subprocess_->GetPid();
  if (pid < 0)
    return;

  if (!system_->ProcessGroupIsGone(pid, timeout)) {
    LOG(WARNING) << "Aborting browser process " << pid << "'s process group "
                 << timeout.InSeconds() << " seconds after sending signal";
    std::string message = base::StringPrintf("Browser took more than %" PRId64
                                             " seconds to exit after signal.",
                                             timeout.InSeconds());
    KillEverything(SIGABRT, message);
  } else {
    DLOG(INFO) << "Cleaned up browser process " << pid;
  }
}

// When user logs in we want to restart chrome in browsing mode with
// user signed in. Hence we remove --login-manager flag and add
// --login-user=|account_id| and --login-profile=|userhash| flags.
void BrowserJob::StartSession(const std::string& account_id,
                              const std::string& userhash) {
  if (!session_already_started_) {
    login_arguments_.clear();
    login_arguments_.push_back(kLoginUserFlag + account_id);
    login_arguments_.push_back(kLoginProfileFlag + userhash);
  }
  session_already_started_ = true;
}

void BrowserJob::StopSession() {
  login_arguments_.clear();
  if (removed_login_manager_flag_) {
    login_arguments_.push_back(kLoginManagerFlag);
    removed_login_manager_flag_ = false;
  }
}

const std::string BrowserJob::GetName() const {
  base::FilePath exec_file(arguments_[0]);
  return exec_file.BaseName().value();
}

void BrowserJob::SetArguments(const std::vector<std::string>& arguments) {
  // Ensure we preserve the program name to be executed, if we have one.
  std::string argv0;
  if (!arguments_.empty())
    argv0 = arguments_[0];

  arguments_ = arguments;

  if (!argv0.empty()) {
    if (arguments_.size())
      arguments_[0] = argv0;
    else
      arguments_.push_back(argv0);
  }
}

void BrowserJob::SetExtraArguments(const std::vector<std::string>& arguments) {
  extra_arguments_ = arguments;
}

void BrowserJob::SetExtraEnvironmentVariables(
    const std::vector<std::string>& env_vars) {
  extra_environment_variables_ = env_vars;
}

void BrowserJob::ClearPid() {
  subprocess_->ClearPid();
}

std::vector<std::string> BrowserJob::ExportArgv() const {
  std::vector<std::string> to_return(arguments_.begin(), arguments_.end());
  to_return.insert(to_return.end(), login_arguments_.begin(),
                   login_arguments_.end());

  if (ShouldDropExtraArgumentsAndEnvironmentVariables()) {
    LOG(WARNING) << "Dropping extra arguments and setting safe-mode switch due "
                    "to crashy browser.";
    to_return.emplace_back(kSafeModeFlag);
  } else {
    to_return.insert(to_return.end(), extra_arguments_.begin(),
                     extra_arguments_.end());
  }

  if (!extra_one_time_arguments_.empty()) {
    to_return.insert(to_return.end(), extra_one_time_arguments_.begin(),
                     extra_one_time_arguments_.end());
  }

  // Chrome doesn't support repeated switches. Merge switches containing
  // comma-separated values that may be supplied via multiple sources (e.g.
  // chrome_setup.cc, chrome://flags, Telemetry).
  //
  // --enable-features and --disable-features may be placed within sentinel
  // values (--flag-switches-begin/end, --policy-switches-begin/end). To
  // preserve those positions, keep the existing flags while also appending
  // merged versions at the end of the command line. Chrome will use the final,
  // merged flags: https://crbug.com/767266
  MergeSwitches(&to_return, kVmoduleFlag, ",", false /* keep_existing */);
  MergeSwitches(&to_return, kEnableFeaturesFlag, ",", true /* keep_existing */);
  MergeSwitches(&to_return, kDisableFeaturesFlag, ",",
                true /* keep_existing */);
  MergeSwitches(&to_return, kEnableBlinkFeaturesFlag, ",",
                true /* keep_existing */);

  // Add --show-webui-login if we are not in a session and Chrome has crashed.
  //
  // TODO(jdufault): Remove this logic after views-based login is stable. See
  // https://crbug.com/822434.
  constexpr char kShowWebUiLogin[] = "--show-webui-login";
  if (!session_already_started_ &&
      base::STLCount(to_return, kShowWebUiLogin) == 0) {
    // The start_times_ recording happens after this call, which means all
    // entries will be 0 on the first run.
    int crash_count = std::count_if(start_times_.begin(), start_times_.end(),
                                    [](int value) { return value != 0; });
    if (crash_count > 0) {
      to_return.push_back(kShowWebUiLogin);
      // Only the first crash would have been using views login.
      if (crash_count == 1) {
        login_metrics_->SendViewsLoginCrash();
      }
    }
  }

  return to_return;
}

std::vector<std::string> BrowserJob::ExportEnvironmentVariables() const {
  std::vector<std::string> vars = environment_variables_;
  if (!ShouldDropExtraArgumentsAndEnvironmentVariables()) {
    vars.insert(vars.end(), extra_environment_variables_.begin(),
                extra_environment_variables_.end());
  }
  return vars;
}

bool BrowserJob::ShouldDropExtraArgumentsAndEnvironmentVariables() const {
  // Check start_time_with_extra_args != 0 so that test cases such as
  // SetExtraArguments and ExportArgv pass without mocking time().
  const time_t start_time_with_extra_args =
      start_times_[kRestartTries - kUseExtraArgsRuns];
  return (start_time_with_extra_args != 0 &&
          system_->time(nullptr) - start_time_with_extra_args <
              kRestartWindowSeconds);
}

}  // namespace login_manager
