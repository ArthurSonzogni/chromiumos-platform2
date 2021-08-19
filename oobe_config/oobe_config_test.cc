// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/oobe_config.h"

#include <string>
#include <utility>

#include <unistd.h>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gtest/gtest.h>

#include "oobe_config/rollback_constants.h"
#include "oobe_config/rollback_data.pb.h"

namespace {
const char kNetworkConfig[] = R"({"NetworkConfigurations":[{
    "GUID":"wpa-psk-network-guid",
    "Type": "WiFi",
    "Name": "WiFi",
    "WiFi": {
      "Security": "WPA-PSK",
      "Passphrase": "wpa-psk-network-passphrase"
  }}]})";
}  // namespace

namespace oobe_config {

class OobeConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    oobe_config_ = std::make_unique<OobeConfig>();
    ASSERT_TRUE(fake_root_dir_.CreateUniqueTempDir());
    oobe_config_->set_prefix_path_for_testing(fake_root_dir_.GetPath());
    oobe_config_->set_network_config_for_testing(kNetworkConfig);
  }

  void CheckSaveAndRestore(bool encrypted) {
    oobe_config_->WriteFile(kSaveTempPath.Append(kOobeCompletedFileName), "");

    // Saving rollback data.
    LOG(INFO) << "Saving rollback data...";
    if (encrypted) {
      ASSERT_TRUE(oobe_config_->EncryptedRollbackSave());
    } else {
      ASSERT_TRUE(oobe_config_->UnencryptedRollbackSave());
    }
    ASSERT_TRUE(oobe_config_->FileExists(kDataSavedFile));

    std::string rollback_data_str;
    ASSERT_TRUE(oobe_config_->ReadFile(kUnencryptedStatefulRollbackDataPath,
                                       &rollback_data_str));

    std::string pstore_data;
    EXPECT_FALSE(rollback_data_str.empty());
    if (!encrypted) {
      oobe_config::RollbackData rollback_data;
      ASSERT_TRUE(rollback_data.ParseFromString(rollback_data_str));
      ASSERT_TRUE(rollback_data.eula_auto_accept());
      ASSERT_FALSE(rollback_data.eula_send_statistics());
    } else {
      ASSERT_TRUE(
          oobe_config_->ReadFile(kRollbackDataForPmsgFile, &pstore_data));
    }

    // Simulate powerwash and only preserve rollback_data by creating new temp
    // dir.
    base::ScopedTempDir tempdir_after;
    ASSERT_TRUE(tempdir_after.CreateUniqueTempDir());
    oobe_config_->set_prefix_path_for_testing(tempdir_after.GetPath());

    // Verify that we don't have any remaining files.
    std::string tmp_data = "x";
    ASSERT_FALSE(oobe_config_->ReadFile(kUnencryptedStatefulRollbackDataPath,
                                        &tmp_data));
    EXPECT_TRUE(tmp_data.empty());

    // Rewrite the rollback data to simulate the preservation that happens
    // during a rollback powerwash.
    ASSERT_TRUE(oobe_config_->WriteFile(kUnencryptedStatefulRollbackDataPath,
                                        rollback_data_str));
    if (encrypted) {
      oobe_config_->WriteFile(kPstorePath.Append("pmsg-ramoops-0"),
                              pstore_data);
    }

    // Restore data.
    LOG(INFO) << "Restoring rollback data...";
    if (encrypted) {
      EXPECT_TRUE(oobe_config_->EncryptedRollbackRestore());
    } else {
      EXPECT_TRUE(oobe_config_->UnencryptedRollbackRestore());
    }
  }

  base::ScopedTempDir fake_root_dir_;
  std::unique_ptr<OobeConfig> oobe_config_;
};

TEST_F(OobeConfigTest, UnencryptedSaveAndRestoreTest) {
  CheckSaveAndRestore(false /* encrypted */);
}

TEST_F(OobeConfigTest, EncryptedSaveAndRestoreTest) {
  CheckSaveAndRestore(true /* encrypted */);
}

TEST_F(OobeConfigTest, ReadNonexistentFile) {
  base::FilePath bogus_path("/DoesNotExist");
  std::string result = "result";
  EXPECT_FALSE(oobe_config_->ReadFile(bogus_path, &result));
  EXPECT_TRUE(result.empty());
}

TEST_F(OobeConfigTest, WriteFileDisallowed) {
  base::FilePath file_path("/test_file");
  std::string content = "content";
  EXPECT_TRUE(oobe_config_->WriteFile(file_path, content));
  // Make the file unwriteable.
  EXPECT_EQ(chmod(fake_root_dir_.GetPath()
                      .Append(file_path.value().substr(1))
                      .value()
                      .c_str(),
                  0400),
            0);
  EXPECT_FALSE(oobe_config_->WriteFile(file_path, content));
}

TEST_F(OobeConfigTest, ReadFileDisallowed) {
  base::FilePath file_path("/test_file");
  std::string content = "content";
  EXPECT_TRUE(oobe_config_->WriteFile(file_path, content));
  // Make the file unreadable.
  EXPECT_EQ(chmod(fake_root_dir_.GetPath()
                      .Append(file_path.value().substr(1))
                      .value()
                      .c_str(),
                  0000),
            0);
  EXPECT_FALSE(oobe_config_->ReadFile(file_path, &content));
  EXPECT_TRUE(content.empty());
}

TEST_F(OobeConfigTest, WriteAndReadFile) {
  base::FilePath file_path("/test_file");
  std::string content = "content";
  std::string result;
  EXPECT_TRUE(oobe_config_->WriteFile(file_path, content));
  EXPECT_TRUE(oobe_config_->ReadFile(file_path, &result));
  EXPECT_EQ(result, content);
}

TEST_F(OobeConfigTest, FileExistsYes) {
  base::FilePath file_path("/test_file");
  std::string content = "content";
  EXPECT_TRUE(oobe_config_->WriteFile(file_path, content));
  EXPECT_TRUE(oobe_config_->FileExists(file_path));
}

TEST_F(OobeConfigTest, FileExistsNo) {
  base::FilePath file_path("/test_file");
  EXPECT_FALSE(oobe_config_->FileExists(file_path));
}

TEST_F(OobeConfigTest, NoRestorePending) {
  EXPECT_FALSE(oobe_config_->ShouldRestoreRollbackData());
}

TEST_F(OobeConfigTest, ShouldRestoreRollbackData) {
  std::string content;
  EXPECT_TRUE(
      oobe_config_->WriteFile(kUnencryptedStatefulRollbackDataPath, content));
  EXPECT_TRUE(oobe_config_->ShouldRestoreRollbackData());
}

TEST_F(OobeConfigTest, ShouldSaveRollbackData) {
  std::string content;
  EXPECT_TRUE(oobe_config_->WriteFile(kRollbackSaveMarkerFile, content));
  EXPECT_TRUE(oobe_config_->ShouldSaveRollbackData());
}

TEST_F(OobeConfigTest, ShouldNotSaveRollbackData) {
  EXPECT_FALSE(oobe_config_->ShouldSaveRollbackData());
}

TEST_F(OobeConfigTest, DeleteRollbackSaveFlagFile) {
  std::string content;
  EXPECT_TRUE(oobe_config_->WriteFile(kRollbackSaveMarkerFile, content));
  EXPECT_TRUE(oobe_config_->DeleteRollbackSaveFlagFile());
  EXPECT_FALSE(oobe_config_->FileExists(kRollbackSaveMarkerFile));
}

TEST_F(OobeConfigTest, DeleteNonexistentRollbackSaveFlagFile) {
  // It is considered successful to delete a file that does not exist.
  EXPECT_TRUE(oobe_config_->DeleteRollbackSaveFlagFile());
}

}  // namespace oobe_config
