// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_FAKE_MANAGER_H_
#define FBPREPROCESSOR_FAKE_MANAGER_H_

#include <memory>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <dbus/bus.h>

#include "fbpreprocessor/fake_session_state_manager.h"
#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

// |FakeManager| is an implementation of fbpreprocessord's main |Manager| object
// that avoids some of the dependencies with the rest of the system (e.g. D-Bus)
// to make it simpler to write unit tests.
// Typical usage:
//
//   class MyTest : public testing::Test {
//    protected:
//     void SetUp() override {
//       manager_ = std::make_unique<FakeManager>();
//       manager_->Start();
//     }
//    private:
//     std::unique_ptr<FakeManager> manager_;
//   };
class FakeManager : public Manager {
 public:
  static constexpr std::string_view kTestUserHash{"user_hash"};

  FakeManager();
  ~FakeManager() = default;

  void Start(dbus::Bus* bus) override;

  bool FirmwareDumpsAllowed() const override { return fw_dumps_allowed_; };

  SessionStateManagerInterface* session_state_manager() const override;

  PseudonymizationManager* pseudonymization_manager() const override {
    return nullptr;
  }

  OutputManager* output_manager() const override { return nullptr; }

  InputManager* input_manager() const override { return nullptr; }

  PlatformFeaturesClient* platform_features() const override { return nullptr; }

  scoped_refptr<base::SequencedTaskRunner> task_runner() override {
    return task_env_.GetMainThreadTaskRunner();
  }

  int default_file_expiration_in_secs() const override {
    return default_file_expiration_in_secs_;
  };

  // Tests sometimes need to ensure that all tasks that have been posted have
  // been run. See warnings at base::test::TaskEnvironment::RunUntilIdle().
  void RunTasksUntilIdle() { task_env_.RunUntilIdle(); }

  // Make the simulated clock advance by |delta|. See
  // base::test::TaskEnvironment::FastForwardBy() for more information.
  void FastForwardBy(const base::TimeDelta& delta) {
    task_env_.FastForwardBy(delta);
  }

  // Let a test simulate what happens when a user logs in (for example
  // SessionManager will notify the observers).
  void SimulateUserLogin();

  // Let a test simulate what happens when a user logs out (for example
  // SessionManager will notify the observers).
  void SimulateUserLogout();

  // Returns the path to the directory where firmware dumps are stored. It's the
  // equivalent of /run/daemon-store/fbpreprocessord/${USER_HASH} for the "real"
  // daemon.
  base::FilePath GetRootDir() const { return root_dir_.GetPath(); }

  // Let tests simulate cases where firmware dump collection is disallowed, for
  // example by policy.
  void set_firmware_dumps_allowed(bool allowed) { fw_dumps_allowed_ = allowed; }

 private:
  // Create a temporary directory with the same structure as the real-world
  // daemon-store. Tests can create firmware dumps in the input directory and
  // read firmware dumps from the output directory, as if it were the
  // daemon-store.
  void SetupFakeDaemonStore();

  base::test::TaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  bool fw_dumps_allowed_;

  int default_file_expiration_in_secs_;

  // Temporary directory used to recreate the equivalent of the daemon-store
  // /run/daemon-store/fbpreprocessord/${USER_HASH} used by the "real" daemon.
  base::ScopedTempDir root_dir_;

  std::unique_ptr<FakeSessionStateManager> session_state_manager_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_FAKE_MANAGER_H_
