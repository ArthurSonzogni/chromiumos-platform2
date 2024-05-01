// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/test/bind.h>
#include <base/time/time.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <gtest/gtest.h>

#include "fbpreprocessor/fake_manager.h"
#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/output_manager.h"

namespace fbpreprocessor {
namespace {

constexpr std::string_view kTestFirmwareContent{"TEST CONTENT TEST CONTENT"};

class OutputManagerTest : public testing::Test {
 protected:
  void SetUp() override { Init(); }

  // This lets the test simulate a user login through the |FakeManager| object.
  // In particular, that means that we verify that the objects that have
  // registered with the |SessionManagerInterface| are notified through that
  // interface.
  void SimulateUserLogin() { manager_->SimulateUserLogin(); }

  // See SimulateUserLogin().
  void SimulateUserLogout() { manager_->SimulateUserLogout(); }

  void SimulateFinchEnabling(bool allowed) {
    manager_->output_manager()->OnFeatureChanged(allowed);
    manager_->RunTasksUntilIdle();
  }

  base::FilePath GetOutputFirmwareDumpName(std::string_view name) {
    return manager_->GetRootDir()
        .Append(FakeManager::kTestUserHash)
        .Append(kProcessedDirectory)
        .Append(name);
  }

  // Call OutputManager::GetDebugDumps() and extract the list of filenames of
  // firmware dumps of type |type| that have been reported.
  void GetDBusDebugDumpsList(DebugDump::Type type,
                             std::set<std::string>* found) {
    auto response = std::make_unique<
        brillo::dbus_utils::MockDBusMethodResponse<DebugDumps>>();

    response->set_return_callback(base::BindLambdaForTesting(
        [&found, &type](const DebugDumps& debug_dumps) {
          for (int i = 0; i < debug_dumps.dump_size(); i++) {
            auto dump = debug_dumps.dump(i);
            if (dump.type() == type && dump.has_wifi_dump()) {
              found->insert(dump.wifi_dump().dmpfile());
            }
          }
        }));
    manager_->output_manager()->GetDebugDumps(std::move(response));
    manager_->RunTasksUntilIdle();
  }

  void AddFirmwareDumpToOutputManager(const FirmwareDump& dump) {
    manager_->output_manager()->AddFirmwareDump(dump);
    manager_->RunTasksUntilIdle();
  }

  FakeManager* manager() const { return manager_.get(); }

 private:
  void InitManager() {
    manager_ = std::make_unique<FakeManager>();
    manager_->Start(/*bus=*/nullptr);
  }

  void Init() { InitManager(); }

  // Declare the Manager first so it's destroyed last.
  std::unique_ptr<FakeManager> manager_;
};

TEST_F(OutputManagerTest, ExistingDumpsDeletedOnLogin) {
  FirmwareDump fw_dump(GetOutputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_TRUE(base::PathExists(fw_dump.DumpFile()));
  // |SessionManager| notifies registered observers that a user has logged in.
  SimulateUserLogin();
  EXPECT_FALSE(base::PathExists(fw_dump.DumpFile()));
}

TEST_F(OutputManagerTest, OnUserLoggedInDeletesExistingDumps) {
  FirmwareDump fw_dump(GetOutputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_TRUE(base::PathExists(fw_dump.DumpFile()));
  // |OutputManager| is notified that a user has logged in.
  manager()->output_manager()->OnUserLoggedIn(
      FakeManager::kTestUserHash.data());
  EXPECT_FALSE(base::PathExists(fw_dump.DumpFile()));
}

TEST_F(OutputManagerTest, EmptyWiFiFirmwareListOnLogin) {
  SimulateUserLogin();
  std::set<std::string> found;
  GetDBusDebugDumpsList(DebugDump::WIFI, &found);
  EXPECT_EQ(found, std::set<std::string>());
}

TEST_F(OutputManagerTest, FilesOnDiskNotAutomaticallyAdded) {
  SimulateUserLogin();
  FirmwareDump fw_dump(GetOutputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);

  // The firmware dumps exist on disk but have not been registered with
  // OutputManager, expect that OutputManager does not report any firmware
  // dumps.
  std::set<std::string> found;
  GetDBusDebugDumpsList(DebugDump::WIFI, &found);
  EXPECT_EQ(found, std::set<std::string>());
}

// Test that when we add firmware dumps to OutputManager,
// OutputManager::GetDebugDumps() returns the correct list in the protobuf.
TEST_F(OutputManagerTest, AddFirmwareDumpSucceeds) {
  SimulateUserLogin();

  std::set<std::string> expected_dumps;
  for (int i = 0; i < 3; i++) {
    FirmwareDump fw_dump(
        GetOutputFirmwareDumpName("test_" + std::to_string(i) + ".dmp"),
        FirmwareDump::Type::kWiFi);
    base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
    AddFirmwareDumpToOutputManager(fw_dump);
    expected_dumps.insert(fw_dump.DumpFile().value());

    // OutputManager has been notified that a new firmware dump has been
    // created. GetDebugDumps() now also lists the new dump.
    std::set<std::string> found;
    GetDBusDebugDumpsList(DebugDump::WIFI, &found);
    EXPECT_EQ(found, expected_dumps) << "Could not find " << fw_dump;

    // Only the first firmware dump is registered if they all have the same
    // addition time, at least with the fake time keeping. It does not happen in
    // real world conditions. Work around that by advancing the clock by 1s.
    manager()->FastForwardBy(base::Seconds(1));
  }
}

TEST_F(OutputManagerTest, FirmwareDumpsExpire) {
  SimulateUserLogin();

  // Add the firmware dump to OutputManager.
  FirmwareDump fw_dump(GetOutputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_TRUE(base::PathExists(fw_dump.DumpFile()));
  AddFirmwareDumpToOutputManager(fw_dump);

  // Wait until the firmware dump has expired.
  manager()->FastForwardBy(
      base::Seconds(manager()->default_file_expiration_in_secs() + 30));

  // After the firmware dump has expired, we expect that:
  // - the file has been deleted
  // - OutputManager::GetDebugDumps() returns an empty list of firmware dumps.
  EXPECT_FALSE(base::PathExists(fw_dump.DumpFile()));
  std::set<std::string> found;
  GetDBusDebugDumpsList(DebugDump::WIFI, &found);
  EXPECT_EQ(found, std::set<std::string>());
}

TEST_F(OutputManagerTest, DisallowingFeatureWithFinchDeletesFirmwareDumps) {
  SimulateUserLogin();

  FirmwareDump fw_dump(GetOutputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_TRUE(base::PathExists(fw_dump.DumpFile()));
  AddFirmwareDumpToOutputManager(fw_dump);

  // Finch disables the feature.
  SimulateFinchEnabling(/*allowed=*/false);

  // Collection of firmware dumps has been disabled. Expect that:
  // - the file has been deleted
  // - OutputManager::GetDebugDumps() returns an empty list of firmware dumps.
  EXPECT_FALSE(base::PathExists(fw_dump.DumpFile()));
  std::set<std::string> found;
  GetDBusDebugDumpsList(DebugDump::WIFI, &found);
  EXPECT_EQ(found, std::set<std::string>());
}

TEST_F(OutputManagerTest, DisallowingFeatureReturnsEmptyFirmwareList) {
  SimulateUserLogin();

  FirmwareDump fw_dump(GetOutputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  AddFirmwareDumpToOutputManager(fw_dump);

  // Force disabling the feature, as if it was disabled by policy or Finch.
  manager()->set_firmware_dumps_allowed(/*allowed=*/false);

  // The Manager will now report that the feature is disallowed. Expect that
  // OutputManager::GetDebugDumps() returns an empty list of firmware dumps.
  std::set<std::string> found;
  GetDBusDebugDumpsList(DebugDump::WIFI, &found);
  EXPECT_EQ(found, std::set<std::string>());
}

TEST_F(OutputManagerTest, UserLogoutReturnsEmptyFirmwareList) {
  SimulateUserLogin();

  FirmwareDump fw_dump(GetOutputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  AddFirmwareDumpToOutputManager(fw_dump);

  SimulateUserLogout();

  // The Manager has notified OutputManager that the user logged out. Expect
  // that OutputManager::GetDebugDumps() returns an empty list of firmware
  // dumps.
  std::set<std::string> found;
  GetDBusDebugDumpsList(DebugDump::WIFI, &found);
  EXPECT_EQ(found, std::set<std::string>());
}

TEST_F(OutputManagerTest, UserLogoutDoesNotDeleteFiles) {
  SimulateUserLogin();

  FirmwareDump fw_dump(GetOutputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  AddFirmwareDumpToOutputManager(fw_dump);

  SimulateUserLogout();

  // The Manager has notified OutputManager that the user logged out. Even
  // though OutputManager::GetDebugDumps() won't report those files anymore, the
  // files have not been deleted from disk.
  EXPECT_TRUE(base::PathExists(fw_dump.DumpFile()));
}

}  // namespace
}  // namespace fbpreprocessor
