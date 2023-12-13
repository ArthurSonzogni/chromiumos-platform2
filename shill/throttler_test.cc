// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/throttler.h"

#include <gmock/gmock.h>
#include <net-base/mock_process_manager.h>
#include <net-base/mock_socket.h>

#include "shill/mock_control.h"
#include "shill/mock_file_io.h"
#include "shill/mock_manager.h"
#include "shill/test_event_dispatcher.h"

using testing::_;
using testing::AllOf;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;
using testing::Test;
using testing::WithArg;

namespace shill {

class ThrottlerTest : public Test {
 public:
  ThrottlerTest()
      : mock_manager_(&control_interface_, &dispatcher_, nullptr),
        throttler_(&dispatcher_, &mock_manager_) {
    throttler_.process_manager_ = &mock_process_manager_;
    throttler_.file_io_ = &mock_file_io_;
  }

 protected:
  static const char kIfaceName0[];
  static const char kIfaceName1[];
  static const char kIfaceName2[];
  static const pid_t kPID1;
  static const pid_t kPID2;
  static const pid_t kPID3;
  static const uint32_t kThrottleRate;

  MockControl control_interface_;
  EventDispatcherForTest dispatcher_;
  StrictMock<MockManager> mock_manager_;
  NiceMock<net_base::MockProcessManager> mock_process_manager_;
  NiceMock<MockFileIO> mock_file_io_;
  Throttler throttler_;
  net_base::MockSocket socket_;
};

const char ThrottlerTest::kIfaceName0[] = "eth0";
const char ThrottlerTest::kIfaceName1[] = "wlan0";
const char ThrottlerTest::kIfaceName2[] = "ppp0";
const pid_t ThrottlerTest::kPID1 = 9900;
const pid_t ThrottlerTest::kPID2 = 9901;
const pid_t ThrottlerTest::kPID3 = 9902;
const uint32_t ThrottlerTest::kThrottleRate = 100;

TEST_F(ThrottlerTest, ThrottleCallsTCExpectedTimesAndSetsState) {
  std::vector<std::string> interfaces = {kIfaceName0, kIfaceName1};
  EXPECT_CALL(mock_manager_, GetDeviceInterfaceNames())
      .WillOnce(Return(interfaces));
  constexpr uint64_t kExpectedCapMask = CAP_TO_MASK(CAP_NET_ADMIN);
  EXPECT_CALL(
      mock_process_manager_,
      StartProcessInMinijailWithPipes(
          _, base::FilePath(Throttler::kTCPath), _, _,
          AllOf(net_base::MinijailOptionsMatchUserGroup(Throttler::kTCUser,
                                                        Throttler::kTCGroup),
                net_base::MinijailOptionsMatchCapMask(kExpectedCapMask)),
          _, _))
      .Times(interfaces.size())
      .WillOnce(WithArg<6>([&](struct net_base::std_file_descriptors std_fds) {
        *std_fds.stdin_fd = socket_.Get();
        return kPID1;
      }))
      .WillOnce(WithArg<6>([&](struct net_base::std_file_descriptors std_fds) {
        *std_fds.stdin_fd = socket_.Get();
        return kPID2;
      }));
  EXPECT_CALL(mock_file_io_, SetFdNonBlocking(_))
      .Times(interfaces.size())
      .WillRepeatedly(Return(false));
  throttler_.ThrottleInterfaces(base::DoNothing(), kThrottleRate,
                                kThrottleRate);
  throttler_.OnProcessExited(0);
  throttler_.OnProcessExited(0);
  EXPECT_TRUE(throttler_.desired_throttling_enabled_);
  EXPECT_EQ(throttler_.desired_upload_rate_kbits_, kThrottleRate);
  EXPECT_EQ(throttler_.desired_download_rate_kbits_, kThrottleRate);
}

TEST_F(ThrottlerTest, NewlyAddedInterfaceIsThrottled) {
  throttler_.desired_throttling_enabled_ = true;
  throttler_.desired_upload_rate_kbits_ = kThrottleRate;
  throttler_.desired_download_rate_kbits_ = kThrottleRate;
  constexpr uint64_t kExpectedCapMask = CAP_TO_MASK(CAP_NET_ADMIN);
  EXPECT_CALL(
      mock_process_manager_,
      StartProcessInMinijailWithPipes(
          _, base::FilePath(Throttler::kTCPath), _, _,
          AllOf(net_base::MinijailOptionsMatchUserGroup(Throttler::kTCUser,
                                                        Throttler::kTCGroup),
                net_base::MinijailOptionsMatchCapMask(kExpectedCapMask)),
          _, _))
      .Times(1)
      .WillOnce(WithArg<6>([&](struct net_base::std_file_descriptors std_fds) {
        *std_fds.stdin_fd = socket_.Get();
        return kPID3;
      }));
  EXPECT_CALL(mock_file_io_, SetFdNonBlocking(_)).WillOnce(Return(false));
  throttler_.ApplyThrottleToNewInterface(kIfaceName2);
}

TEST_F(ThrottlerTest, DisablingThrottleClearsState) {
  throttler_.desired_throttling_enabled_ = true;
  throttler_.desired_upload_rate_kbits_ = kThrottleRate;
  throttler_.desired_download_rate_kbits_ = kThrottleRate;
  std::vector<std::string> interfaces = {kIfaceName0};
  EXPECT_CALL(mock_manager_, GetDeviceInterfaceNames())
      .WillOnce(Return(interfaces));
  constexpr uint64_t kExpectedCapMask = CAP_TO_MASK(CAP_NET_ADMIN);
  EXPECT_CALL(
      mock_process_manager_,
      StartProcessInMinijailWithPipes(
          _, base::FilePath(Throttler::kTCPath), _, _,
          AllOf(net_base::MinijailOptionsMatchUserGroup(Throttler::kTCUser,
                                                        Throttler::kTCGroup),
                net_base::MinijailOptionsMatchCapMask(kExpectedCapMask)),
          _, _))
      .Times(1)
      .WillOnce(WithArg<6>([&](struct net_base::std_file_descriptors std_fds) {
        *std_fds.stdin_fd = socket_.Get();
        return kPID1;
      }));
  EXPECT_CALL(mock_file_io_, SetFdNonBlocking(_))
      .Times(interfaces.size())
      .WillRepeatedly(Return(false));
  throttler_.DisableThrottlingOnAllInterfaces(base::DoNothing());
  throttler_.OnProcessExited(0);
  EXPECT_FALSE(throttler_.desired_throttling_enabled_);
  EXPECT_EQ(throttler_.desired_upload_rate_kbits_, 0);
  EXPECT_EQ(throttler_.desired_download_rate_kbits_, 0);
}

TEST_F(ThrottlerTest, DisablingThrottleWhenNoThrottleExists) {
  throttler_.desired_throttling_enabled_ = false;
  throttler_.desired_upload_rate_kbits_ = 0;
  throttler_.desired_download_rate_kbits_ = 0;
  std::vector<std::string> interfaces = {kIfaceName0};
  EXPECT_CALL(mock_manager_, GetDeviceInterfaceNames())
      .WillOnce(Return(interfaces));
  constexpr uint64_t kExpectedCapMask = CAP_TO_MASK(CAP_NET_ADMIN);
  EXPECT_CALL(
      mock_process_manager_,
      StartProcessInMinijailWithPipes(
          _, base::FilePath(Throttler::kTCPath), _, _,
          AllOf(net_base::MinijailOptionsMatchUserGroup(Throttler::kTCUser,
                                                        Throttler::kTCGroup),
                net_base::MinijailOptionsMatchCapMask(kExpectedCapMask)),
          _, _))
      .Times(1)
      .WillOnce(WithArg<6>([&](struct net_base::std_file_descriptors std_fds) {
        *std_fds.stdin_fd = socket_.Get();
        return kPID1;
      }));
  EXPECT_CALL(mock_file_io_, SetFdNonBlocking(_))
      .Times(interfaces.size())
      .WillRepeatedly(Return(false));
  throttler_.DisableThrottlingOnAllInterfaces(base::DoNothing());
  throttler_.OnProcessExited(1);
  EXPECT_FALSE(throttler_.desired_throttling_enabled_);
  EXPECT_EQ(throttler_.desired_upload_rate_kbits_, 0);
  EXPECT_EQ(throttler_.desired_download_rate_kbits_, 0);
}

}  // namespace shill
