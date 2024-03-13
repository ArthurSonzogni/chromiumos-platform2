// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string_view>

#include <base/files/file_util.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>
#include <gtest/gtest.h>

#include "fbpreprocessor/fake_manager.h"
#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/input_manager.h"

namespace {
constexpr std::string_view kTestFirmwareContent{"TEST CONTENT TEST CONTENT"};
}  // namespace

namespace fbpreprocessor {

class InputManagerTest : public testing::Test {
 protected:
  void SetUp() override { Init(); }

  // This lets the test simulate a user login through the |FakeManager| object.
  // In particular, that means that we verify that the objects that have
  // registered with the |SessionManagerInterface| are notified through that
  // interface.
  void SimulateUserLogin() { manager_->SimulateUserLogin(); }

  // See SimulateUserLogin().
  void SimulateUserLogout() { manager_->SimulateUserLogout(); }

  base::FilePath GetInputFirmwareDumpName(std::string_view name) {
    return manager_->GetRootDir()
        .Append(FakeManager::kTestUserHash)
        .Append(kInputDirectory)
        .Append(name);
  }

  InputManager* input_manager() const { return input_manager_.get(); }

  FakeManager* manager() const { return manager_.get(); }

 private:
  void InitManager() {
    manager_ = std::make_unique<FakeManager>();
    manager_->Start(/*bus=*/nullptr);
  }

  void Init() {
    InitManager();
    input_manager_ = std::make_unique<InputManager>(manager_.get());
    input_manager_->set_base_dir_for_test(manager_->GetRootDir());
  }

  // Declare the Manager first so it's destroyed last.
  std::unique_ptr<FakeManager> manager_;
  std::unique_ptr<InputManager> input_manager_;
};

TEST_F(InputManagerTest, UserLoginDeletesExistingFirmwareDumps) {
  // Create a firmware dump in the daemon-store before the user has logged in.
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"));
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_TRUE(base::PathExists(fw_dump.DumpFile()));

  // The |Manager| notifies that a user has logged in.
  SimulateUserLogin();

  // On user login, pre-existing firmware dumps have been deleted.
  EXPECT_FALSE(base::PathExists(fw_dump.DumpFile()));
}

TEST_F(InputManagerTest, OnUserLoggedInDeletesExistingFirmwareDumps) {
  // Create a firmware dump in the daemon-store before the user has logged in.
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"));
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_TRUE(base::PathExists(fw_dump.DumpFile()));

  // InputManager is notified that a user has logged in.
  input_manager()->OnUserLoggedIn(FakeManager::kTestUserHash.data());

  // On user login, pre-existing firmware dumps have been deleted.
  EXPECT_FALSE(base::PathExists(fw_dump.DumpFile()));
}

TEST_F(InputManagerTest, OnNewFirmwareDumpSucceeds) {
  SimulateUserLogin();
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"));
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);

  EXPECT_TRUE(input_manager()->OnNewFirmwareDump(fw_dump));
}

TEST_F(InputManagerTest, OnNewFirmwareDumpRejectsNonExistingFiles) {
  SimulateUserLogin();
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"));

  // The FirmwareDump object is not backed by an on-disk file, expect the
  // request to be rejected.
  EXPECT_FALSE(input_manager()->OnNewFirmwareDump(fw_dump));
}

}  // namespace fbpreprocessor
