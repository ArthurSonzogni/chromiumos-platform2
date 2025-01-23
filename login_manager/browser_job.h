// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_BROWSER_JOB_H_
#define LOGIN_MANAGER_BROWSER_JOB_H_

#include <time.h>
#include <unistd.h>

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <brillo/namespaces/mount_namespace.h>
#include <gtest/gtest_prod.h>

#include "login_manager/chrome_setup.h"

namespace login_manager {

class LoginMetrics;
class SubprocessInterface;
class SystemUtils;

class BrowserJobInterface {
 public:
  virtual ~BrowserJobInterface() = default;

  // Creates a background process and starts the job running in it. Does any
  // necessary bookkeeping.
  // Returns true if the process was created, false otherwise.
  virtual bool RunInBackground() = 0;

  // Attempt to kill the current instance of this job by sending
  // signal to the _entire process group_, sending message (if set) to
  // the instance to tell it why it must die.
  virtual void KillEverything(int signal, const std::string& message) = 0;

  // Attempt to kill the current instance of this job by sending
  // signal, sending message (if set) to the instance to tell it
  // why it must die.
  virtual void Kill(int signal, const std::string& message) = 0;

  // Returns the name of the job.
  virtual const std::string GetName() const = 0;

  // Returns the pid of the current instance of this job. May be -1.
  virtual pid_t CurrentPid() const = 0;

  virtual bool IsGuestSession() = 0;

  // If ShouldStop() returns true, this means that the parent should tear
  // everything down.
  virtual bool ShouldStop() const = 0;

  // Called when a session is started for a user, to update internal
  // bookkeeping wrt command-line flags. |account_id| should be a valid account
  // ID.
  virtual void StartSession(const std::string& account_id,
                            const std::string& userhash) = 0;

  // Called when the session is ended.
  virtual void StopSession() = 0;

  // Wait up to |timeout| for the browser process group to exit. Returns true if
  // all processes are gone, otherwise false.
  virtual bool WaitForExit(base::TimeDelta timeout) = 0;

  // Aborts the process, waits |crash_timeout| for a crash report to be written,
  // and then SIGKILLs the job and its children if it's not gone by then.
  virtual void AbortAndKillAll(base::TimeDelta timeout) = 0;

  // Sets command line arguments for the job from string vector. This overwrites
  // the arguments passed to BrowserJob's constructor.
  virtual void SetArguments(const std::vector<std::string>& arguments) = 0;

  // Sets extra command line arguments for the job from a string vector. These
  // are in addition to the arguments from BrowserJob's constructor (or
  // SetArguments()).
  virtual void SetExtraArguments(const std::vector<std::string>& arguments) = 0;

  // Set feature flags to pass to the browser on startup.
  virtual void SetFeatureFlags(
      const std::vector<std::string>& feature_flags,
      const std::map<std::string, std::string>& origin_list_flags) = 0;

  // Sets command line arguments for integration tests. These are in addition to
  // the arguments from BrowserJob's constructor / SetArguments() and the
  // arguments from SetExtraArguments,
  virtual void SetTestArguments(const std::vector<std::string>& arguments) = 0;

  // Sets additional environment variables for the job. These are in addition to
  // the environmental variables set in BrowserJob's constructor.
  virtual void SetAdditionalEnvironmentVariables(
      const std::vector<std::string>& env_vars) = 0;

  // Throw away the pid of the currently-tracked browser job.
  virtual void ClearPid() = 0;

  // The flag to pass to Chrome to tell it to behave as the login manager.
  static const char kLoginManagerFlag[];

  // The flag to pass to Chrome to tell it which user has signed in.
  static const char kLoginUserFlag[];

  // The flag to pass to Chrome to tell it the hash of the user who's signed in.
  static const char kLoginProfileFlag[];

  // The flag to pass to Chrome to tell it to run in Guest mode.
  static const char kGuestSessionFlag[];

  // The flag to pass to Chrome to tell it that, if it crashes, it should tell
  // crash_reporter to run in crash-loop mode.
  static const char kCrashLoopBeforeFlag[];
};

class BrowserJob : public BrowserJobInterface {
 public:
  // This describes a configuration for running the browser.
  // Since the browser comprises several processes and runs in different modes,
  // a BrowserJob::Config object similarly covers various process types and
  // modes.
  struct Config {
    bool isolate_guest_session;
    bool isolate_regular_session;
    // Put the browser process tree in the specified non-root mount namespace.
    std::optional<base::FilePath> chrome_mount_ns_path;
  };

  BrowserJob(const std::vector<std::string>& arguments,
             const std::vector<std::string>& environment_variables,
             LoginMetrics* metrics,
             SystemUtils* system_utils,
             const BrowserJob::Config& cfg,
             std::unique_ptr<SubprocessInterface> subprocess);
  BrowserJob(const BrowserJob&) = delete;
  BrowserJob& operator=(const BrowserJob&) = delete;

  ~BrowserJob() override;

  // Overridden from BrowserJobInterface
  bool RunInBackground() override;
  void KillEverything(int signal, const std::string& message) override;
  void Kill(int signal, const std::string& message) override;
  bool WaitForExit(base::TimeDelta timeout) override;
  void AbortAndKillAll(base::TimeDelta timeout) override;
  pid_t CurrentPid() const override;
  bool IsGuestSession() override;
  bool ShouldStop() const override;
  void StartSession(const std::string& account_id,
                    const std::string& userhash) override;
  void StopSession() override;
  const std::string GetName() const override;
  void SetArguments(const std::vector<std::string>& arguments) override;
  void SetExtraArguments(const std::vector<std::string>& arguments) override;
  void SetFeatureFlags(
      const std::vector<std::string>& feature_flags,
      const std::map<std::string, std::string>& origin_list_flags) override;
  void SetTestArguments(const std::vector<std::string>& arguments) override;
  void SetAdditionalEnvironmentVariables(
      const std::vector<std::string>& env_vars) override;
  void ClearPid() override;

  // Stores the current time as the time when the job was started.
  void RecordTime();

  // Exports a copy of the current argv or environment variables.
  std::vector<std::string> ExportArgv() const;
  std::vector<std::string> ExportEnvironmentVariables() const;

  // Whether to drop the "extra" arguments when starting the job.
  bool ShouldDropExtraArguments() const;

  // Flag passed to Chrome the first time Chrome is started after the
  // system boots. Not passed when Chrome is restarted after signout.
  static const char kFirstExecAfterBootFlag[];

  // DeviceStartUpFlags policy and user flags are set as |extra_arguments_|.
  // After kUseExtraArgsRuns in kRestartWindowSeconds, drop |extra_arguments_|
  // in the restarted job in the hope that the startup crash stops.
  static const int kUseExtraArgsRuns;

  // After kRestartTries in kRestartWindowSeconds, the BrowserJob will indicate
  // that it should be stopped.
  static const int kRestartTries;
  static const time_t kRestartWindowSeconds;

 private:
  // Arguments to pass to exec.
  std::vector<std::string> arguments_;

  // Environment variables exported for Chrome.
  std::vector<std::string> environment_variables_;

  // Login-related arguments to pass to exec.  Managed wholly by this class.
  std::vector<std::string> login_arguments_;

  // Feature flags to pass to the browser.
  std::vector<std::string> feature_flags_;
  std::map<std::string, std::string> origin_list_flags_;

  // Extra arguments to pass to exec.
  std::vector<std::string> extra_arguments_;

  // Extra one time arguments.
  std::vector<std::string> extra_one_time_arguments_;

  // Integration test arguments to pass to exec.
  std::vector<std::string> test_arguments_;

  // Additional environment variables to set when running the browser.
  // Values are of the form "NAME=VALUE".
  std::vector<std::string> additional_environment_variables_;

  // Wrapper for reading/writing metrics. Externally owned.
  LoginMetrics* login_metrics_;

  // Wrapper for system library calls. Externally owned.
  SystemUtils* system_utils_;

  // FIFO of job-start timestamps. Used to determine if we've restarted too many
  // times too quickly. The most recent job-start timestamp is at the end.
  std::deque<time_t> start_times_;

  // Indicates if we removed login manager flag when session started so we
  // add it back when session stops.
  bool removed_login_manager_flag_ = false;

  // Indicates that we already started a session.  Needed because the
  // browser requires us to track the _first_ user to start a session.
  // There is no issue filed to address this.
  bool session_already_started_ = false;

  Config config_;

  // The subprocess tracked by this job.
  std::unique_ptr<SubprocessInterface> subprocess_;

  FRIEND_TEST(BrowserJobTest, InitializationTest);
  FRIEND_TEST(BrowserJobTest, ShouldStopTest);
  FRIEND_TEST(BrowserJobTest, ShouldNotStopTest);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_BROWSER_JOB_H_
