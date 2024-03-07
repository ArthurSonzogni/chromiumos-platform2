// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_FAKE_MANAGER_H_
#define FBPREPROCESSOR_FAKE_MANAGER_H_

#include <base/test/task_environment.h>
#include <dbus/bus.h>

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
  FakeManager();
  ~FakeManager() = default;

  void Start(dbus::Bus* bus) override;

  bool FirmwareDumpsAllowed() const override { return fw_dumps_allowed_; };

  SessionStateManagerInterface* session_state_manager() const override {
    return nullptr;
  }

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

 private:
  base::test::TaskEnvironment task_env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  bool fw_dumps_allowed_;

  int default_file_expiration_in_secs_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_FAKE_MANAGER_H_