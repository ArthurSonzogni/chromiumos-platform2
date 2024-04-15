// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>
#include <gtest/gtest.h>

#include "fbpreprocessor/fake_manager.h"
#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/pseudonymization_manager.h"

namespace fbpreprocessor {
namespace {

constexpr std::string_view kTestFirmwareContent{"TEST CONTENT TEST CONTENT"};

class PseudonymizationManagerTest : public testing::Test {
 protected:
  void SetUp() override { Init(); }

  // This lets the test simulate a user login through the |FakeManager| object.
  // In particular, that means that we verify that the objects that have
  // registered with the |SessionManagerInterface| are notified through that
  // interface.
  void SimulateUserLogin() { manager_->SimulateUserLogin(); }

  // See SimulateUserLogin().
  void SimulateUserLogout() { manager_->SimulateUserLogout(); }

  FakeManager* manager() const { return manager_.get(); }

  PseudonymizationManager* pseudonymization_manager() {
    return pseudonymization_manager_.get();
  }

  base::FilePath GetInputFirmwareDumpName(std::string_view name) {
    return manager_->GetRootDir()
        .Append(FakeManager::kTestUserHash)
        .Append(kInputDirectory)
        .Append(name);
  }

 private:
  void InitManager() {
    manager_ = std::make_unique<FakeManager>();
    manager_->Start(/*bus=*/nullptr);
  }

  void Init() {
    InitManager();
    pseudonymization_manager_ =
        std::make_unique<PseudonymizationManager>(manager_.get());
    pseudonymization_manager_->set_base_dir_for_test(manager_->GetRootDir());
  }

  // Declare the Manager first so it's destroyed last.
  std::unique_ptr<FakeManager> manager_;
  std::unique_ptr<PseudonymizationManager> pseudonymization_manager_;
};

TEST_F(PseudonymizationManagerTest, StartPseudonymizationNoUserLoggedIn) {
  // Expect the pseudonymization request to be rejected if there's no user
  // logged in.
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_FALSE(pseudonymization_manager()->StartPseudonymization(fw_dump));
}

TEST_F(PseudonymizationManagerTest, StartPseudonymizationUserLoggedOut) {
  // Expect the pseudonymization request to be rejected if users have logged
  // out.
  SimulateUserLogin();
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  SimulateUserLogout();
  EXPECT_FALSE(pseudonymization_manager()->StartPseudonymization(fw_dump));
}

TEST_F(PseudonymizationManagerTest, StartPseudonymizationSuccessAfterLogin) {
  // |SessionManager| notifies registered observers that a user has logged in.
  SimulateUserLogin();

  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  // If a user is logged in and a firmware dump exists in the input directory,
  // then the pseudonymization request will be accepted.
  EXPECT_TRUE(pseudonymization_manager()->StartPseudonymization(fw_dump));
}

TEST_F(PseudonymizationManagerTest,
       StartPseudonymizationSuccessAfterOnUserLoggedIn) {
  // |PseudonymizationManager| is notified that a user has logged in.
  pseudonymization_manager()->OnUserLoggedIn(FakeManager::kTestUserHash.data());

  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  // If a user is logged in and a firmware dump exists in the input directory,
  // then the pseudonymization request will be accepted.
  EXPECT_TRUE(pseudonymization_manager()->StartPseudonymization(fw_dump));
}

TEST_F(PseudonymizationManagerTest, StartPseudonymizationNoOp) {
  SimulateUserLogin();
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  // If a user is logged in and a firmware dump exists in the input directory,
  // then the pseudonymization request will be accepted.
  pseudonymization_manager()->StartPseudonymization(fw_dump);
  manager()->RunTasksUntilIdle();

  // For a no-op pseudonymization request, the name and content of the processed
  // firmware dump should be the same as the input firmware dump.
  base::FilePath processed_path = manager()
                                      ->GetRootDir()
                                      .Append(FakeManager::kTestUserHash)
                                      .Append(kProcessedDirectory)
                                      .Append("test.dmp");
  std::string processed_content;
  EXPECT_TRUE(base::ReadFileToString(processed_path, &processed_content));
  EXPECT_EQ(processed_content, kTestFirmwareContent);
}

TEST_F(PseudonymizationManagerTest, RateLimitAccepts5Requests) {
  SimulateUserLogin();
  // Start 5 pseudonymizations, one per minute. They should all be accepted
  // since we accept up to 5 pseudonymizations in 30 minutes.
  for (int i = 0; i < 5; ++i) {
    FirmwareDump fw_dump(
        GetInputFirmwareDumpName("test_" + std::to_string(i) + ".dmp"),
        FirmwareDump::Type::kWiFi);
    base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
    EXPECT_TRUE(pseudonymization_manager()->StartPseudonymization(fw_dump))
        << "for file " << fw_dump.DumpFile();
    manager()->FastForwardBy(base::Minutes(1));
  }
}

TEST_F(PseudonymizationManagerTest, RateLimitAcceptsOnly5Requests) {
  SimulateUserLogin();
  // Start 5 pseudonymizations, one per minute. They should all be accepted
  // since we accept up to 5 pseudonymizations in 30 minutes.
  for (int i = 0; i < 5; ++i) {
    FirmwareDump fw_dump(
        GetInputFirmwareDumpName("test_" + std::to_string(i) + ".dmp"),
        FirmwareDump::Type::kWiFi);
    base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
    pseudonymization_manager()->StartPseudonymization(fw_dump);
    manager()->FastForwardBy(base::Minutes(1));
  }

  // We're now at 5 pseudonymizations in the last 5 minutes, which is more than
  // the maximume rate (5 per 30 minutes). Pseudonymizations should be rejected.
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_FALSE(pseudonymization_manager()->StartPseudonymization(fw_dump));
}

TEST_F(PseudonymizationManagerTest, RateLimitAcceptsAfter30Minutes) {
  SimulateUserLogin();
  // Start 5 pseudonymizations, one per minute. They should all be accepted
  // since we accept up to 5 pseudonymizations in 30 minutes.
  for (int i = 0; i < 5; ++i) {
    FirmwareDump fw_dump(
        GetInputFirmwareDumpName("test_" + std::to_string(i) + ".dmp"),
        FirmwareDump::Type::kWiFi);
    base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
    pseudonymization_manager()->StartPseudonymization(fw_dump);
    manager()->FastForwardBy(base::Minutes(1));
  }

  // After 40 minutes without a pseudonymization, we no longer hit the rate
  // limit and pseudonymization requests should be accepted again.
  manager()->FastForwardBy(base::Minutes(40));

  // Start 5 pseudonymizations, one per minute. They should all be accepted.
  for (int i = 0; i < 5; ++i) {
    FirmwareDump fw_dump(
        GetInputFirmwareDumpName("retest_" + std::to_string(i) + ".dmp"),
        FirmwareDump::Type::kWiFi);
    base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
    EXPECT_TRUE(pseudonymization_manager()->StartPseudonymization(fw_dump))
        << "for file " << fw_dump.DumpFile();
    manager()->FastForwardBy(base::Minutes(1));
  }
}

TEST_F(PseudonymizationManagerTest, RateLimitClearedOnLogout) {
  SimulateUserLogin();
  // Start 5 pseudonymizations, one per minute.
  for (int i = 0; i < 5; ++i) {
    FirmwareDump fw_dump(
        GetInputFirmwareDumpName("test_" + std::to_string(i) + ".dmp"),
        FirmwareDump::Type::kWiFi);
    base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
    pseudonymization_manager()->StartPseudonymization(fw_dump);
    manager()->FastForwardBy(base::Minutes(1));
  }

  // We're now at 5 pseudonymizations in the last 5 minutes, which is more than
  // the maximume rate (5 per 30 minutes). Pseudonymizations should be rejected.
  SimulateUserLogout();
  SimulateUserLogin();

  // The user logged out and logged back in. The rate limiter has been reset,
  // expect pseudonymization requests to be accepted again.
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_TRUE(pseudonymization_manager()->StartPseudonymization(fw_dump));
}

TEST_F(PseudonymizationManagerTest, RejectedRequestDeletesDump) {
  SimulateUserLogin();
  // Start 5 pseudonymizations, one per minute.
  for (int i = 0; i < 5; ++i) {
    FirmwareDump fw_dump(
        GetInputFirmwareDumpName("test_" + std::to_string(i) + ".dmp"),
        FirmwareDump::Type::kWiFi);
    base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
    pseudonymization_manager()->StartPseudonymization(fw_dump);
    manager()->FastForwardBy(base::Minutes(1));
  }

  // We're now at 5 pseudonymizations in the last 5 minutes, which is more than
  // the maximume rate (5 per 30 minutes). Pseudonymizations should be rejected.
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  EXPECT_TRUE(base::PathExists(fw_dump.DumpFile()));
  EXPECT_FALSE(pseudonymization_manager()->StartPseudonymization(fw_dump));
  // The firmware dump that can't be pseudonymized must have been deleted.
  EXPECT_FALSE(base::PathExists(fw_dump.DumpFile()));
}

TEST_F(PseudonymizationManagerTest, PseudonymizationEmitsStartTypeUMA) {
  // We're pseudonymizing 2 firmware dumps, both of type WiFi. Expect that the
  // value "1" for the type is sent to UMA both times.
  std::vector<int> expected_uma_calls{1, 1};
  SimulateUserLogin();
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  pseudonymization_manager()->StartPseudonymization(fw_dump);
  pseudonymization_manager()->StartPseudonymization(fw_dump);

  EXPECT_EQ(manager()->GetMetricCalls(
                "Platform.FbPreprocessor.Pseudonymization.DumpType"),
            expected_uma_calls);
}

TEST_F(PseudonymizationManagerTest, PseudonymizationEmitsResultUMA) {
  // Test that we emit Metrics::PseudonymizationResult::kSuccess after a
  // successful pseudonymization.
  std::vector<int> expected_uma_calls{1};
  SimulateUserLogin();
  FirmwareDump fw_dump(GetInputFirmwareDumpName("test.dmp"),
                       FirmwareDump::Type::kWiFi);
  base::WriteFile(fw_dump.DumpFile(), kTestFirmwareContent);
  pseudonymization_manager()->StartPseudonymization(fw_dump);
  manager()->RunTasksUntilIdle();

  EXPECT_EQ(manager()->GetMetricCalls(
                "Platform.FbPreprocessor.WiFi.Pseudonymization.Result"),
            expected_uma_calls);
}

}  // namespace
}  // namespace fbpreprocessor
