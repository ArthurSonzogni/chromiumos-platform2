// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/vpd_utils_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/utils/mock_cmd_utils.h"

using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class VpdUtilsTest : public testing::Test {
 public:
  VpdUtilsTest() = default;
  ~VpdUtilsTest() override = default;
};

TEST_F(VpdUtilsTest, GetSerialNumber_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>("abc"), Return(true)));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string serial_number;
  EXPECT_TRUE(vpd_utils->GetSerialNumber(&serial_number));
  EXPECT_EQ(serial_number, "abc");
}

TEST_F(VpdUtilsTest, GetSerialNumber_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string serial_number;
  EXPECT_FALSE(vpd_utils->GetSerialNumber(&serial_number));
}

TEST_F(VpdUtilsTest, GetSerialNumber_Nullptr) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_DEATH(vpd_utils->GetSerialNumber(nullptr), "");
}

TEST_F(VpdUtilsTest, GetWhitelabelTag_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>("abc"), Return(true)));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string wl_tag;
  EXPECT_TRUE(vpd_utils->GetWhitelabelTag(&wl_tag));
  EXPECT_EQ(wl_tag, "abc");
}

TEST_F(VpdUtilsTest, GetWhitelabelTag_Empty) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string wl_tag;
  EXPECT_TRUE(vpd_utils->GetWhitelabelTag(&wl_tag));
  EXPECT_EQ(wl_tag, "");
}

TEST_F(VpdUtilsTest, GetWhitelabelTag_Nullptr) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_DEATH(vpd_utils->GetWhitelabelTag(nullptr), "");
}

TEST_F(VpdUtilsTest, GetRegion_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce(DoAll(SetArgPointee<1>("abc"), Return(true)));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string region;
  EXPECT_TRUE(vpd_utils->GetRegion(&region));
  EXPECT_EQ(region, "abc");
}

TEST_F(VpdUtilsTest, GetRegion_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string region;
  EXPECT_FALSE(vpd_utils->GetRegion(&region));
}

TEST_F(VpdUtilsTest, GetRegion_Nullptr) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_DEATH(vpd_utils->GetRegion(nullptr), "");
}

TEST_F(VpdUtilsTest, GetCalibbias_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>("123"), Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>("456"), Return(true)));
  }
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::vector<int> calibbias;
  EXPECT_TRUE(vpd_utils->GetCalibbias({"x", "y"}, &calibbias));
  EXPECT_EQ(calibbias.size(), 2);
  EXPECT_EQ(calibbias[0], 123);
  EXPECT_EQ(calibbias[1], 456);
}

TEST_F(VpdUtilsTest, GetCalibbias_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::vector<int> calibbias;
  EXPECT_FALSE(vpd_utils->GetCalibbias({"x", "y"}, &calibbias));
}

TEST_F(VpdUtilsTest, GetCalibbias_Fail_ParseError) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>("123"), Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>("abc"), Return(true)));
  }
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::vector<int> calibbias;
  EXPECT_FALSE(vpd_utils->GetCalibbias({"x", "y"}, &calibbias));
}

TEST_F(VpdUtilsTest, GetCalibbias_Nullptr) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_DEATH(vpd_utils->GetCalibbias({"x", "y"}, nullptr), "");
}

TEST_F(VpdUtilsTest, GetRegistrationCode_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  {
    InSequence seq;
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>("abc"), Return(true)));
    EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
        .WillOnce(DoAll(SetArgPointee<1>("def"), Return(true)));
  }
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string ubind, gbind;
  EXPECT_TRUE(vpd_utils->GetRegistrationCode(&ubind, &gbind));
  EXPECT_EQ(ubind, "abc");
  EXPECT_EQ(gbind, "def");
}

TEST_F(VpdUtilsTest, GetRegistrationCode_Fail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillOnce(Return(false));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string ubind, gbind;
  EXPECT_FALSE(vpd_utils->GetRegistrationCode(&ubind, &gbind));
}

TEST_F(VpdUtilsTest, GetRegistrationCode_NullptrUbind) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string gbind;
  EXPECT_DEATH(vpd_utils->GetRegistrationCode(nullptr, &gbind), "");
}

TEST_F(VpdUtilsTest, GetRegistrationCode_NullptrGbind) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string ubind;
  EXPECT_DEATH(vpd_utils->GetRegistrationCode(&ubind, nullptr), "");
}

TEST_F(VpdUtilsTest, SetSerialNumber_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  // Expect this to be called when flushing the cached values in destructor.
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce([](const std::vector<std::string>& argv, std::string* output) {
        const std::vector<std::string> expect = {
            "/usr/sbin/vpd", "-i", "RO_VPD", "-s", "serial_number=abc"};
        EXPECT_EQ(argv, expect);
        return true;
      });
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string serial_number;
  EXPECT_TRUE(vpd_utils->SetSerialNumber("abc"));
  EXPECT_TRUE(vpd_utils->GetSerialNumber(&serial_number));
  EXPECT_EQ(serial_number, "abc");
}

TEST_F(VpdUtilsTest, SetWhitelabelTag_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  // Expect this to be called when flushing the cached values in destructor.
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce([](const std::vector<std::string>& argv, std::string* output) {
        const std::vector<std::string> expect = {
            "/usr/sbin/vpd", "-i", "RO_VPD", "-s", "whitelabel_tag=abc"};
        EXPECT_EQ(argv, expect);
        return true;
      });
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string wl_tag;
  EXPECT_TRUE(vpd_utils->SetWhitelabelTag("abc"));
  EXPECT_TRUE(vpd_utils->GetWhitelabelTag(&wl_tag));
  EXPECT_EQ(wl_tag, "abc");
}

TEST_F(VpdUtilsTest, SetRegion_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  // Expect this to be called when flushing the cached values in destructor.
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce([](const std::vector<std::string>& argv, std::string* output) {
        const std::vector<std::string> expect = {"/usr/sbin/vpd", "-i",
                                                 "RO_VPD", "-s", "region=abc"};
        EXPECT_EQ(argv, expect);
        return true;
      });
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string region;
  EXPECT_TRUE(vpd_utils->SetRegion("abc"));
  EXPECT_TRUE(vpd_utils->GetRegion(&region));
  EXPECT_EQ(region, "abc");
}

TEST_F(VpdUtilsTest, SetCalibbias_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  // Expect this to be called when flushing the cached values in destructor.
  // The command can be in either order.
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce([](const std::vector<std::string>& argv, std::string* output) {
        const std::vector<std::string> expect1 = {
            "/usr/sbin/vpd", "-i", "RO_VPD", "-s", "x=123", "-s", "y=456"};
        const std::vector<std::string> expect2 = {
            "/usr/sbin/vpd", "-i", "RO_VPD", "-s", "y=456", "-s", "x=123"};
        EXPECT_TRUE(argv == expect1 || argv == expect2);
        return true;
      });
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::vector<int> calibbias;
  EXPECT_TRUE(vpd_utils->SetCalibbias({{"x", 123}, {"y", 456}}));
  EXPECT_TRUE(vpd_utils->GetCalibbias({"x", "y"}, &calibbias));
  EXPECT_EQ(calibbias.size(), 2);
  EXPECT_EQ(calibbias[0], 123);
  EXPECT_EQ(calibbias[1], 456);
}

TEST_F(VpdUtilsTest, SetRegistrationCode_Success) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  // Expect this to be called when flushing the cached values in destructor.
  // The command can be in either order.
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _))
      .WillOnce([](const std::vector<std::string>& argv, std::string* output) {
        const std::vector<std::string> expect1 = {"/usr/sbin/vpd",
                                                  "-i",
                                                  "RW_VPD",
                                                  "-s",
                                                  "ubind_attribute=abc",
                                                  "-s",
                                                  "gbind_attribute=def"};
        const std::vector<std::string> expect2 = {"/usr/sbin/vpd",
                                                  "-i",
                                                  "RW_VPD",
                                                  "-s",
                                                  "gbind_attribute=def",
                                                  "-s",
                                                  "ubind_attribute=abc"};
        EXPECT_TRUE(argv == expect1 || argv == expect2);
        return true;
      });
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  std::string ubind, gbind;
  EXPECT_TRUE(vpd_utils->SetRegistrationCode("abc", "def"));
  EXPECT_TRUE(vpd_utils->GetRegistrationCode(&ubind, &gbind));
  EXPECT_EQ(ubind, "abc");
  EXPECT_EQ(gbind, "def");
}

TEST_F(VpdUtilsTest, FlushRoFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillRepeatedly(Return(false));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(vpd_utils->SetSerialNumber("abc"));
  EXPECT_FALSE(vpd_utils->FlushOutRoVpdCache());
}

TEST_F(VpdUtilsTest, FlushRwFail) {
  auto mock_cmd_utils = std::make_unique<StrictMock<MockCmdUtils>>();
  EXPECT_CALL(*mock_cmd_utils, GetOutput(_, _)).WillRepeatedly(Return(false));
  auto vpd_utils = std::make_unique<VpdUtilsImpl>(std::move(mock_cmd_utils));

  EXPECT_TRUE(vpd_utils->SetRegistrationCode("abc", "def"));
  EXPECT_FALSE(vpd_utils->FlushOutRwVpdCache());
}

}  // namespace rmad
