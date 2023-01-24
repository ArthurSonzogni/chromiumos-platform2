// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/oobe_config_test.h"

#include <memory>
#include <string>
#include <utility>

#include <unistd.h>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gtest/gtest.h>

#include "oobe_config/filesystem/file_handler.h"
#include "oobe_config/oobe_config.h"
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

void OobeConfigTest::SetUp() {
  ASSERT_TRUE(file_handler_.CreateDefaultExistingPaths());

  oobe_config_ = std::make_unique<OobeConfig>(file_handler_);
  oobe_config_->set_network_config_for_testing(kNetworkConfig);
}

TEST_F(OobeConfigTest, EncryptedSaveAndRestoreTest) {
  file_handler_.CreateOobeCompletedFlag();

  ASSERT_TRUE(oobe_config_->EncryptedRollbackSave());

  ASSERT_TRUE(file_handler_.HasDataSavedFlag());

  std::string rollback_data_str;
  ASSERT_TRUE(
      file_handler_.ReadOpensslEncryptedRollbackData(&rollback_data_str));
  ASSERT_FALSE(rollback_data_str.empty());

  std::string pstore_data;
  ASSERT_TRUE(file_handler_.ReadPstoreData(&pstore_data));

  // Simulate powerwash by using new `OobeConfig` object and directory.
  file_handler_ = FileHandlerForTesting();
  ASSERT_TRUE(file_handler_.CreateDefaultExistingPaths());

  // Rewrite the rollback data to simulate the preservation that happens
  // during a rollback powerwash.
  ASSERT_TRUE(
      file_handler_.WriteOpensslEncryptedRollbackData(rollback_data_str));
  ASSERT_TRUE(file_handler_.WriteRamoopsData(pstore_data));

  oobe_config_ = std::make_unique<OobeConfig>(file_handler_);

  ASSERT_TRUE(oobe_config_->EncryptedRollbackRestore());
}

}  // namespace oobe_config
