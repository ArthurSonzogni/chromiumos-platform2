// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/image_properties.h"

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/test_utils.h"
#include "update_engine/cros/fake_system_state.h"

using chromeos_update_engine::test_utils::WriteFileString;
using std::string;

namespace chromeos_update_engine {

class ImagePropertiesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a uniquely named test directory.
    ASSERT_TRUE(tempdir_.CreateUniqueTempDir());
    EXPECT_TRUE(base::CreateDirectory(tempdir_.GetPath().Append("etc")));
    EXPECT_TRUE(base::CreateDirectory(base::FilePath(
        tempdir_.GetPath().value() + kStatefulPartition + "/etc")));
    test::SetImagePropertiesRootPrefix(tempdir_.GetPath().value().c_str());
    FakeSystemState::CreateInstance();
    SetLockDown(false);
  }

  void SetLockDown(bool locked_down) {
    FakeSystemState::Get()->fake_hardware()->SetIsOfficialBuild(locked_down);
    FakeSystemState::Get()->fake_hardware()->SetIsNormalBootMode(locked_down);
  }

  base::ScopedTempDir tempdir_;
};

TEST_F(ImagePropertiesTest, SimpleTest) {
  ASSERT_TRUE(
      WriteFileString(tempdir_.GetPath().Append("etc/lsb-release").value(),
                      "CHROMEOS_RELEASE_BOARD=arm-generic\n"
                      "CHROMEOS_RELEASE_FOO=bar\n"
                      "CHROMEOS_RELEASE_VERSION=0.2.2.3\n"
                      "CHROMEOS_RELEASE_TRACK=dev-channel\n"
                      "CHROMEOS_AUSERVER=http://www.google.com"));
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ("arm-generic", props.board);
  EXPECT_EQ("{87efface-864d-49a5-9bb3-4b050a7c227a}", props.product_id);
  EXPECT_EQ("0.2.2.3", props.version);
  EXPECT_EQ(kDevChannel, props.current_channel);
  EXPECT_EQ("http://www.google.com", props.omaha_url);
}

TEST_F(ImagePropertiesTest, AppIDTest) {
  ASSERT_TRUE(WriteFileString(
      tempdir_.GetPath().Append("etc/lsb-release").value(),
      "CHROMEOS_RELEASE_APPID={58c35cef-9d30-476e-9098-ce20377d535d}"));
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ("{58c35cef-9d30-476e-9098-ce20377d535d}", props.product_id);
}

TEST_F(ImagePropertiesTest, ConfusingReleaseTest) {
  ASSERT_TRUE(
      WriteFileString(tempdir_.GetPath().Append("etc/lsb-release").value(),
                      "CHROMEOS_RELEASE_FOO=CHROMEOS_RELEASE_VERSION=1.2.3.4\n"
                      "CHROMEOS_RELEASE_VERSION=0.2.2.3"));
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ("0.2.2.3", props.version);
}

TEST_F(ImagePropertiesTest, MissingVersionTest) {
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ("", props.version);
}

TEST_F(ImagePropertiesTest, OverrideTest) {
  ASSERT_TRUE(
      WriteFileString(tempdir_.GetPath().Append("etc/lsb-release").value(),
                      "CHROMEOS_RELEASE_BOARD=arm-generic\n"
                      "CHROMEOS_RELEASE_FOO=bar\n"
                      "CHROMEOS_RELEASE_TRACK=dev-channel\n"
                      "CHROMEOS_AUSERVER=http://www.google.com"));
  ASSERT_TRUE(WriteFileString(
      tempdir_.GetPath().value() + kStatefulPartition + "/etc/lsb-release",
      "CHROMEOS_RELEASE_BOARD=x86-generic\n"
      "CHROMEOS_RELEASE_TRACK=beta-channel\n"
      "CHROMEOS_AUSERVER=https://www.google.com"));
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ("x86-generic", props.board);
  EXPECT_EQ(kDevChannel, props.current_channel);
  EXPECT_EQ("https://www.google.com", props.omaha_url);
  MutableImageProperties mutable_props = LoadMutableImageProperties();
  EXPECT_EQ(kBetaChannel, mutable_props.target_channel);
}

TEST_F(ImagePropertiesTest, OverrideLockDownTest) {
  ASSERT_TRUE(
      WriteFileString(tempdir_.GetPath().Append("etc/lsb-release").value(),
                      "CHROMEOS_RELEASE_BOARD=arm-generic\n"
                      "CHROMEOS_RELEASE_FOO=bar\n"
                      "CHROMEOS_RELEASE_TRACK=dev-channel\n"
                      "CHROMEOS_AUSERVER=https://www.google.com"));
  ASSERT_TRUE(WriteFileString(
      tempdir_.GetPath().value() + kStatefulPartition + "/etc/lsb-release",
      "CHROMEOS_RELEASE_BOARD=x86-generic\n"
      "CHROMEOS_RELEASE_TRACK=stable-channel\n"
      "CHROMEOS_AUSERVER=http://www.google.com"));
  SetLockDown(true);
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ("arm-generic", props.board);
  EXPECT_EQ(kDevChannel, props.current_channel);
  EXPECT_EQ("https://www.google.com", props.omaha_url);
  MutableImageProperties mutable_props = LoadMutableImageProperties();
  EXPECT_EQ(kStableChannel, mutable_props.target_channel);
}

TEST_F(ImagePropertiesTest, BoardAppIdUsedForNonCanaryChannelTest) {
  ASSERT_TRUE(
      WriteFileString(tempdir_.GetPath().Append("etc/lsb-release").value(),
                      "CHROMEOS_RELEASE_APPID=r\n"
                      "CHROMEOS_BOARD_APPID=b\n"
                      "CHROMEOS_CANARY_APPID=c\n"
                      "CHROMEOS_RELEASE_TRACK=stable-channel\n"));
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ(kStableChannel, props.current_channel);
  EXPECT_EQ("b", props.product_id);
}

TEST_F(ImagePropertiesTest, CanaryAppIdUsedForCanaryChannelTest) {
  ASSERT_TRUE(
      WriteFileString(tempdir_.GetPath().Append("etc/lsb-release").value(),
                      "CHROMEOS_RELEASE_APPID=r\n"
                      "CHROMEOS_BOARD_APPID=b\n"
                      "CHROMEOS_CANARY_APPID=c\n"
                      "CHROMEOS_RELEASE_TRACK=canary-channel\n"));
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ(kCanaryChannel, props.current_channel);
  EXPECT_EQ("c", props.canary_product_id);
}

TEST_F(ImagePropertiesTest, ReleaseAppIdUsedAsDefaultTest) {
  ASSERT_TRUE(
      WriteFileString(tempdir_.GetPath().Append("etc/lsb-release").value(),
                      "CHROMEOS_RELEASE_APPID=r\n"
                      "CHROMEOS_CANARY_APPID=c\n"
                      "CHROMEOS_RELEASE_TRACK=stable-channel\n"));
  ImageProperties props = LoadImageProperties();
  EXPECT_EQ(kStableChannel, props.current_channel);
  EXPECT_EQ("r", props.product_id);
}

}  // namespace chromeos_update_engine
