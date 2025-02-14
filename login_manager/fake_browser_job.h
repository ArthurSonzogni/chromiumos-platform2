// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_FAKE_BROWSER_JOB_H_
#define LOGIN_MANAGER_FAKE_BROWSER_JOB_H_

#include <sys/types.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <gmock/gmock.h>

#include "login_manager/browser_job.h"

namespace login_manager {
class FakeChildProcess;

class FakeBrowserJob : public BrowserJobInterface {
 public:
  explicit FakeBrowserJob(const std::string& name);
  FakeBrowserJob(const FakeBrowserJob&) = delete;
  FakeBrowserJob& operator=(const FakeBrowserJob&) = delete;

  ~FakeBrowserJob() override;

  void set_fake_child_process(std::unique_ptr<FakeChildProcess> fake) {
    fake_process_ = std::move(fake);
  }
  void set_schedule_exit(bool value) { schedule_exit_ = value; }

  // Overridden from BrowserJobInterface
  bool IsGuestSession() override;
  MOCK_METHOD(bool, ShouldStop, (), (const, override));
  MOCK_METHOD(void, KillEverything, (int, const std::string&), (override));
  MOCK_METHOD(void, Kill, (int, const std::string&), (override));
  MOCK_METHOD(bool, WaitForExit, (base::TimeDelta), (override));
  MOCK_METHOD(void, AbortAndKillAll, (base::TimeDelta), (override));
  MOCK_METHOD(void,
              StartSession,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(void, StopSession, (), (override));
  MOCK_METHOD(void,
              SetArguments,
              (const std::vector<std::string>&),
              (override));
  MOCK_METHOD(void,
              SetExtraArguments,
              (const std::vector<std::string>&),
              (override));
  MOCK_METHOD(void,
              SetFeatureFlags,
              (const std::vector<std::string>&,
               (const std::map<std::string, std::string>&)),
              (override));
  MOCK_METHOD(void,
              SetTestArguments,
              (const std::vector<std::string>&),
              (override));
  MOCK_METHOD(void,
              SetAdditionalEnvironmentVariables,
              (const std::vector<std::string>&),
              (override));

  bool RunInBackground(
      base::OnceCallback<void(const siginfo_t&)> callback) override;
  const std::string GetName() const override;
  pid_t CurrentPid() const override;
  void ClearPid() override;

 private:
  std::unique_ptr<FakeChildProcess> fake_process_;
  const std::string name_;
  bool running_ = false;
  bool schedule_exit_ = true;
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_FAKE_BROWSER_JOB_H_
