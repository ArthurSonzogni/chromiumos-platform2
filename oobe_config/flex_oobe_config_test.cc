// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/flex_oobe_config.h"

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <dbus/dbus-protocol.h>
#include <gtest/gtest.h>
#include <oobe_config/proto_bindings/oobe_config.pb.h>

#include "oobe_config/filesystem/file_handler_for_testing.h"

namespace oobe_config {

class FlexOobeConfigTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::unique_ptr<FileHandlerForTesting> tmp_file_handler_ptr =
        std::make_unique<FileHandlerForTesting>();
    file_handler_ = tmp_file_handler_ptr.get();
    flex_oobe_config_ =
        std::make_unique<FlexOobeConfig>(std::move(tmp_file_handler_ptr));
  }

  FileHandlerForTesting* file_handler_;
  std::unique_ptr<FlexOobeConfig> flex_oobe_config_;
};

#if USE_REVEN_OOBE_CONFIG

const char kFlexConfig[] = "{ \"enrollmentToken\": \"test_enrollment_token\" }";

TEST_F(FlexOobeConfigTest, NoFlexOobeConfig) {
  std::string config;
  ASSERT_FALSE(flex_oobe_config_->GetOobeConfigJson(&config));
  ASSERT_EQ(config, "");
}

TEST_F(FlexOobeConfigTest, FlexOobeConfigPresent) {
  file_handler_->CreateEncryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToEncryptedStateful(kFlexConfig);
  std::string config;

  ASSERT_TRUE(flex_oobe_config_->GetOobeConfigJson(&config));
  ASSERT_EQ(config, kFlexConfig);
}

TEST_F(FlexOobeConfigTest, DeleteFlexOobeConfigNotFound) {
  brillo::ErrorPtr error;

  ASSERT_FALSE(flex_oobe_config_->DeleteFlexOobeConfig(&error));
  ASSERT_EQ(error->GetCode(), DBUS_ERROR_FILE_NOT_FOUND);
}

TEST_F(FlexOobeConfigTest, DeleteFlexOobeConfigDeleteFailure) {
  file_handler_->CreateEncryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToEncryptedStateful(kFlexConfig);
  file_handler_->SimulateRemoveFlexOobeConfigFailure();
  brillo::ErrorPtr error;

  ASSERT_FALSE(flex_oobe_config_->DeleteFlexOobeConfig(&error));
  ASSERT_EQ(error->GetCode(), DBUS_ERROR_IO_ERROR);
}

TEST_F(FlexOobeConfigTest, DeleteFlexOobeConfigSuccess) {
  file_handler_->CreateEncryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToEncryptedStateful(kFlexConfig);
  brillo::ErrorPtr error;

  ASSERT_TRUE(flex_oobe_config_->DeleteFlexOobeConfig(&error));

  ASSERT_EQ(error, nullptr);
  ASSERT_FALSE(file_handler_->HasEncryptedFlexOobeConfigFile());
}

TEST_F(FlexOobeConfigTest, DeleteFlexOobeConfigAlsoDeletesUnencryptedStateful) {
  file_handler_->CreateUnencryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToUnencryptedStateful(kFlexConfig);
  file_handler_->CreateEncryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToEncryptedStateful(kFlexConfig);
  brillo::ErrorPtr error;

  ASSERT_TRUE(flex_oobe_config_->DeleteFlexOobeConfig(&error));

  ASSERT_EQ(error, nullptr);
  ASSERT_FALSE(file_handler_->HasUnencryptedFlexOobeConfigFile());
  ASSERT_FALSE(file_handler_->HasEncryptedFlexOobeConfigFile());
}

TEST_F(FlexOobeConfigTest, MoveFlexOobeConfigToEncryptedStatefulSuccess) {
  file_handler_->CreateUnencryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToUnencryptedStateful(kFlexConfig);
  file_handler_->CreateRestorePath();

  ASSERT_TRUE(flex_oobe_config_->MoveFlexOobeConfigToEncryptedStateful());

  ASSERT_FALSE(file_handler_->HasUnencryptedFlexOobeConfigFile());
  ASSERT_TRUE(file_handler_->HasEncryptedFlexOobeConfigFile());
  std::string config;
  ASSERT_TRUE(flex_oobe_config_->GetOobeConfigJson(&config));
  ASSERT_EQ(config, kFlexConfig);
  int mode;
  ASSERT_TRUE(file_handler_->GetEncryptedFlexOobeConfigFilePermissions(&mode));
  ASSERT_EQ(mode, 0640);
}

TEST_F(FlexOobeConfigTest,
       MoveFlexOobeConfigToEncryptedStatefulNoUnencryptedConfigIsNoOp) {
  file_handler_->CreateEncryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToEncryptedStateful(kFlexConfig);

  ASSERT_TRUE(flex_oobe_config_->MoveFlexOobeConfigToEncryptedStateful());

  ASSERT_FALSE(file_handler_->HasUnencryptedFlexOobeConfigFile());
  ASSERT_TRUE(file_handler_->HasEncryptedFlexOobeConfigFile());
}

TEST_F(FlexOobeConfigTest,
       MoveFlexOobeConfigToEncryptedStatefulEncryptedDirAlreadyExists) {
  file_handler_->CreateUnencryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToUnencryptedStateful(kFlexConfig);
  file_handler_->CreateEncryptedFlexConfigDirectory();

  ASSERT_TRUE(flex_oobe_config_->MoveFlexOobeConfigToEncryptedStateful());

  ASSERT_FALSE(file_handler_->HasUnencryptedFlexOobeConfigFile());
  ASSERT_TRUE(file_handler_->HasEncryptedFlexOobeConfigFile());
}

TEST_F(FlexOobeConfigTest,
       MoveFlexOobeConfigToEncryptedStatefulBothFilesPresent) {
  file_handler_->CreateUnencryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToUnencryptedStateful(kFlexConfig);
  file_handler_->CreateEncryptedFlexConfigDirectory();
  file_handler_->WriteFlexOobeConfigDataToEncryptedStateful(kFlexConfig);

  ASSERT_TRUE(flex_oobe_config_->MoveFlexOobeConfigToEncryptedStateful());

  ASSERT_FALSE(file_handler_->HasUnencryptedFlexOobeConfigFile());
  ASSERT_TRUE(file_handler_->HasEncryptedFlexOobeConfigFile());
}

#endif  // USE_REVEN_OOBE_CONFIG

#if !USE_REVEN_OOBE_CONFIG

TEST_F(FlexOobeConfigTest, DeleteFlexOobeConfigUnsupported) {
  brillo::ErrorPtr error;

  ASSERT_FALSE(flex_oobe_config_->DeleteFlexOobeConfig(&error));
  ASSERT_EQ(error->GetCode(), DBUS_ERROR_NOT_SUPPORTED);
}

#endif  // !USE_REVEN_OOBE_CONFIG

}  // namespace oobe_config
