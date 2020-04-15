// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_PROCESS_MANAGER_SERVICE_INTERFACE_H_
#define LOGIN_MANAGER_PROCESS_MANAGER_SERVICE_INTERFACE_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>

namespace login_manager {

class GeneratorJobInterface;

class ProcessManagerServiceInterface {
 public:
  ProcessManagerServiceInterface() {}
  virtual ~ProcessManagerServiceInterface() {}

  // Enqueue a QuitClosure.
  virtual void ScheduleShutdown() = 0;

  // Fork, then run the browser in the child process.
  virtual void RunBrowser() = 0;

  // Abort the browser process with |signal|, passing |message| to its
  // crash dumping machinery.
  virtual void AbortBrowser(int signal, const std::string& message) = 0;

  // Whenever the browser is restarted, add |args| to its command line in
  // addition to the normal arguments. Effects last until this function is
  // called again.
  virtual void SetBrowserTestArgs(const std::vector<std::string>& args) = 0;

  // Whenever the browser is restarted, use |args| as its command line. This
  // overwrites the normal arguments (such as the ones from PerformChromeSetup).
  // Effects last until this function is called again.
  virtual void SetBrowserArgs(const std::vector<std::string>& args) = 0;

  // Whenever the browser is restarted, add |env_vars| to its environmental
  // variables in addition to the normal environmental variables. Each string
  // in |env_vars| should be the form "NAME=VALUE". Effects last until this
  // function is called again.
  virtual void SetBrowserAdditionalEnvironmentalVariables(
      const std::vector<std::string>& env_vars) = 0;

  // Kill and restart the browser.
  virtual void RestartBrowser() = 0;

  // Set bookkeeping for the browser process to indicate that a session
  // has been started for the given user.
  virtual void SetBrowserSessionForUser(const std::string& username,
                                        const std::string& userhash) = 0;

  // Stores in memory the flags that session manager should apply the next time
  // it restarts Chrome inside an existing session.
  virtual void SetFlagsForUser(const std::string& username,
                               const std::vector<std::string>& flags) = 0;

  // Check if |pid| is the currently-managed browser process.
  virtual bool IsBrowser(pid_t pid) = 0;

  // Returns the last time that the browser was restarted after exiting
  // (typically due to a crash).
  virtual base::TimeTicks GetLastBrowserRestartTime() = 0;
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_PROCESS_MANAGER_SERVICE_INTERFACE_H_
