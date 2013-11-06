// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mist/config_loader.h"

#include <string>

#include <base/file_path.h>
#include <base/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "mist/proto_bindings/config.pb.h"
#include "mist/proto_bindings/usb_modem_info.pb.h"

using base::FilePath;
using std::string;

namespace mist {

namespace {

const char kTestConfigFileContent[] =
    "# Test config\n"
    "\n"
    "# USB modem 1\n"
    "usb_modem_info {\n"
    "  initial_usb_id {\n"
    "    vendor_id: 0x2345\n"
    "    product_id: 0x7890\n"
    "  }\n"
    "}\n"
    "# USB modem 2\n"
    "usb_modem_info {\n"
    "  initial_usb_id { vendor_id: 0x1234 product_id: 0xabcd }\n"
    "  final_usb_id { vendor_id: 0x5678 product_id: 0xfedc }\n"
    "  final_usb_id { vendor_id: 0x3210 product_id: 0x9876 }\n"
    "  usb_message: \"0123456789abcdef\"\n"
    "  usb_message: \"fedcba9877654210\"\n"
    "  usb_message: \"1234\"\n"
    "  expect_response: true\n"
    "}\n";

}  // namespace

class ConfigLoaderTest : public testing::Test {
 protected:
  bool CreateConfigFileInDir(const string &content, const FilePath &dir,
                             FilePath *config_file) {
    if (!file_util::CreateTemporaryFileInDir(dir, config_file))
      return false;

    if (file_util::WriteFile(*config_file, content.data(), content.size()) !=
        static_cast<int>(content.size())) {
      return false;
    }

    return true;
  }

  ConfigLoader config_loader_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(ConfigLoaderTest, GetUsbModemInfo) {
  // No config is loaded.
  EXPECT_TRUE(config_loader_.GetUsbModemInfo(0x1111, 0x2222) == NULL);

  base::FilePath config_file;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  ASSERT_TRUE(CreateConfigFileInDir(kTestConfigFileContent, temp_dir_.path(),
                                    &config_file));

  EXPECT_TRUE(config_loader_.LoadConfig(config_file));

  EXPECT_TRUE(config_loader_.GetUsbModemInfo(0x1111, 0x2222) == NULL);

  const UsbModemInfo *usb_modem_info1 =
      config_loader_.GetUsbModemInfo(0x2345, 0x7890);
  EXPECT_TRUE(usb_modem_info1 != NULL);
  EXPECT_EQ(0x2345, usb_modem_info1->initial_usb_id().vendor_id());
  EXPECT_EQ(0x7890, usb_modem_info1->initial_usb_id().product_id());
  EXPECT_EQ(0, usb_modem_info1->final_usb_id_size());
  EXPECT_EQ(0, usb_modem_info1->usb_message_size());
  EXPECT_FALSE(usb_modem_info1->expect_response());

  const UsbModemInfo* usb_modem_info2 =
      config_loader_.GetUsbModemInfo(0x1234, 0xabcd);
  EXPECT_TRUE(usb_modem_info2 != NULL);
  EXPECT_EQ(0x1234, usb_modem_info2->initial_usb_id().vendor_id());
  EXPECT_EQ(0xabcd, usb_modem_info2->initial_usb_id().product_id());
  EXPECT_EQ(2, usb_modem_info2->final_usb_id_size());
  EXPECT_EQ(0x5678, usb_modem_info2->final_usb_id(0).vendor_id());
  EXPECT_EQ(0xfedc, usb_modem_info2->final_usb_id(0).product_id());
  EXPECT_EQ(0x3210, usb_modem_info2->final_usb_id(1).vendor_id());
  EXPECT_EQ(0x9876, usb_modem_info2->final_usb_id(1).product_id());
  EXPECT_EQ(3, usb_modem_info2->usb_message_size());
  EXPECT_EQ("0123456789abcdef", usb_modem_info2->usb_message(0));
  EXPECT_EQ("fedcba9877654210", usb_modem_info2->usb_message(1));
  EXPECT_EQ("1234", usb_modem_info2->usb_message(2));
  EXPECT_TRUE(usb_modem_info2->expect_response());
}

TEST_F(ConfigLoaderTest, LoadEmptyConfigFile) {
  base::FilePath config_file;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  ASSERT_TRUE(CreateConfigFileInDir("", temp_dir_.path(), &config_file));

  EXPECT_TRUE(config_loader_.LoadConfig(config_file));
  Config *config = config_loader_.config_.get();
  EXPECT_TRUE(config != NULL);
  EXPECT_EQ(0, config->usb_modem_info_size());
}

TEST_F(ConfigLoaderTest, LoadInvalidConfigFile) {
  base::FilePath config_file;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  ASSERT_TRUE(CreateConfigFileInDir("<invalid config>", temp_dir_.path(),
                                    &config_file));

  EXPECT_FALSE(config_loader_.LoadConfig(config_file));
  EXPECT_TRUE(config_loader_.config_.get() == NULL);
}

TEST_F(ConfigLoaderTest, LoadNonExistentConfigFile) {
  EXPECT_FALSE(config_loader_.LoadConfig(FilePath("/non-existent-file")));
  EXPECT_TRUE(config_loader_.config_.get() == NULL);
}

TEST_F(ConfigLoaderTest, LoadValidConfigFile) {
  base::FilePath config_file;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  ASSERT_TRUE(CreateConfigFileInDir(kTestConfigFileContent, temp_dir_.path(),
                                    &config_file));

  EXPECT_TRUE(config_loader_.LoadConfig(config_file));
  Config *config = config_loader_.config_.get();
  EXPECT_TRUE(config != NULL);
  EXPECT_EQ(2, config->usb_modem_info_size());

  const UsbModemInfo& usb_modem_info1 = config->usb_modem_info(0);
  EXPECT_EQ(0x2345, usb_modem_info1.initial_usb_id().vendor_id());
  EXPECT_EQ(0x7890, usb_modem_info1.initial_usb_id().product_id());

  const UsbModemInfo& usb_modem_info2 = config->usb_modem_info(1);
  EXPECT_EQ(0x1234, usb_modem_info2.initial_usb_id().vendor_id());
  EXPECT_EQ(0xabcd, usb_modem_info2.initial_usb_id().product_id());
}

}  // namespace mist
