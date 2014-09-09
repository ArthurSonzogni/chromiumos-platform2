// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <chromeos/syslog_logging.h>
#include <chromeos/test_helpers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "vpn-manager/service_manager.h"

using ::base::FilePath;
using ::chromeos::ClearLog;
using ::chromeos::FindLog;
using ::chromeos::GetLog;
using ::testing::InSequence;
using ::testing::Return;

namespace vpn_manager {

class MockService : public ServiceManager {
 public:
  MockService() : ServiceManager("mock") {}
  MOCK_METHOD0(Start, bool());
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD0(Poll, int());
  MOCK_METHOD0(ProcessOutput, void());
  MOCK_METHOD1(IsChild, bool(pid_t pid));
};

class ServiceManagerTest : public ::testing::Test {
 public:
  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());
    test_path_ = temp_dir_.path().Append("service_manager_testdir");
    base::DeleteFile(test_path_, true);
    base::CreateDirectory(test_path_);
    temp_path_ = test_path_.Append("service");
    ServiceManager::temp_base_path_ = temp_path_.value().c_str();
    ServiceManager::temp_path_ = &temp_path_;
    ServiceManager::SetLayerOrder(&outer_service_, &inner_service_);
    chromeos::ClearLog();
  }

  void TearDown() override {
    ServiceManager::temp_base_path_ = nullptr;
    ServiceManager::temp_path_ = nullptr;
  }

 protected:
  FilePath temp_path_;
  FilePath test_path_;
  MockService outer_service_;
  MockService inner_service_;
  MockService single_service_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(ServiceManagerTest, InitializeDirectories) {
  FilePath picked_temp;
  {
    base::ScopedTempDir my_temp;
    EXPECT_FALSE(my_temp.IsValid());
    ServiceManager::InitializeDirectories(&my_temp);
    EXPECT_TRUE(my_temp.IsValid());
    picked_temp = my_temp.path();
    EXPECT_TRUE(base::DirectoryExists(picked_temp));
  }
  EXPECT_FALSE(base::DirectoryExists(picked_temp));
}

TEST_F(ServiceManagerTest, OnStartedInnerSucceeds) {
  EXPECT_CALL(inner_service_, Start()).WillOnce(Return(true));
  EXPECT_FALSE(outer_service_.is_running());
  EXPECT_FALSE(outer_service_.was_stopped());
  outer_service_.OnStarted();
  EXPECT_TRUE(outer_service_.is_running());
  EXPECT_FALSE(outer_service_.was_stopped());
}

TEST_F(ServiceManagerTest, OnStartedInnerFails) {
  InSequence unused;
  EXPECT_CALL(inner_service_, Start()).WillOnce(Return(false));
  EXPECT_CALL(outer_service_, Stop());
  EXPECT_FALSE(outer_service_.is_running());
  outer_service_.OnStarted();
  // The outer service keeps saying it's running until its OnStop is called.
  EXPECT_TRUE(outer_service_.is_running());
  EXPECT_TRUE(FindLog("Inner service mock failed"));
}

TEST_F(ServiceManagerTest, OnStartedNoInner) {
  EXPECT_FALSE(single_service_.is_running());
  EXPECT_FALSE(single_service_.was_stopped());
  single_service_.OnStarted();
  EXPECT_TRUE(single_service_.is_running());
  EXPECT_FALSE(single_service_.was_stopped());
}

TEST_F(ServiceManagerTest, OnStoppedFromSuccess) {
  EXPECT_CALL(outer_service_, Stop());
  inner_service_.is_running_ = true;
  EXPECT_TRUE(inner_service_.is_running());
  EXPECT_FALSE(inner_service_.was_stopped());
  inner_service_.OnStopped(true);
  EXPECT_FALSE(inner_service_.is_running());
  EXPECT_TRUE(inner_service_.was_stopped());
}

TEST_F(ServiceManagerTest, OnStoppedFromFailure) {
  EXPECT_CALL(outer_service_, Stop());
  inner_service_.is_running_ = true;
  EXPECT_TRUE(inner_service_.is_running());
  EXPECT_FALSE(inner_service_.was_stopped());
  inner_service_.OnStopped(false);
  EXPECT_FALSE(inner_service_.is_running());
  EXPECT_TRUE(inner_service_.was_stopped());
}

TEST_F(ServiceManagerTest, RegisterError) {
  // No error initially
  EXPECT_EQ(kServiceErrorNoError, single_service_.GetError());
  single_service_.RegisterError(kServiceErrorInternal);
  EXPECT_EQ(kServiceErrorInternal, single_service_.GetError());
  // Registering a more specific error overrides the current error
  single_service_.RegisterError(kServiceErrorPppAuthenticationFailed);
  EXPECT_EQ(kServiceErrorPppAuthenticationFailed, single_service_.GetError());
  // Registering a less specific error does not override the current error
  single_service_.RegisterError(kServiceErrorPppConnectionFailed);
  EXPECT_EQ(kServiceErrorPppAuthenticationFailed, single_service_.GetError());

  // No error initially
  EXPECT_EQ(kServiceErrorNoError, outer_service_.GetError());
  EXPECT_EQ(kServiceErrorNoError, inner_service_.GetError());
  // The outer service reports its error if the inner service reports no error
  outer_service_.RegisterError(kServiceErrorIpsecConnectionFailed);
  EXPECT_EQ(kServiceErrorIpsecConnectionFailed, outer_service_.GetError());
  EXPECT_EQ(kServiceErrorNoError, inner_service_.GetError());
  // The outer service reports the error reported by the inner service
  inner_service_.RegisterError(kServiceErrorL2tpConnectionFailed);
  EXPECT_EQ(kServiceErrorL2tpConnectionFailed, outer_service_.GetError());
  EXPECT_EQ(kServiceErrorL2tpConnectionFailed, inner_service_.GetError());
}

TEST_F(ServiceManagerTest, WriteFdToSyslog) {
  int mypipe[2];
  ASSERT_EQ(0, pipe(mypipe));
  std::string partial;

  const char kMessage1[] = "good morning\npipe\n";
  EXPECT_EQ(strlen(kMessage1), write(mypipe[1], kMessage1, strlen(kMessage1)));
  single_service_.WriteFdToSyslog(mypipe[0], "prefix: ", &partial);
  EXPECT_EQ("prefix: good morning\nprefix: pipe\n", GetLog());
  EXPECT_EQ("", partial);

  ClearLog();

  const char kMessage2[] = "partial line";
  EXPECT_EQ(strlen(kMessage2), write(mypipe[1], kMessage2, strlen(kMessage2)));
  single_service_.WriteFdToSyslog(mypipe[0], "prefix: ", &partial);
  EXPECT_EQ(kMessage2, partial);
  EXPECT_EQ("", GetLog());

  const char kMessage3[] = " end\nbegin\nlast";
  EXPECT_EQ(strlen(kMessage3), write(mypipe[1], kMessage3, strlen(kMessage3)));
  single_service_.WriteFdToSyslog(mypipe[0], "prefix: ", &partial);
  EXPECT_EQ("last", partial);
  EXPECT_EQ("prefix: partial line end\nprefix: begin\n", GetLog());

  close(mypipe[0]);
  close(mypipe[1]);
}

TEST_F(ServiceManagerTest, GetLocalAddressFromRemote) {
  struct sockaddr remote_address;
  struct sockaddr local_address;
  std::string local_address_text;
  EXPECT_TRUE(ServiceManager::ConvertIPStringToSockAddr("127.0.0.1",
                                                        &remote_address));
  EXPECT_TRUE(ServiceManager::GetLocalAddressFromRemote(remote_address,
                                                        &local_address));
  EXPECT_TRUE(ServiceManager::ConvertSockAddrToIPString(local_address,
                                                        &local_address_text));
  EXPECT_EQ("127.0.0.1", local_address_text);
}


TEST_F(ServiceManagerTest, GetRootPersistentPath) {
  // Restore the non-testing default base path.
  ServiceManager::temp_base_path_ = ServiceManager::kDefaultTempBasePath;
  EXPECT_EQ("/var/run/l2tpipsec_vpn/current",
            ServiceManager::GetRootPersistentPath().value());
}

}  // namespace vpn_manager

int main(int argc, char** argv) {
  SetUpTests(&argc, argv, true);
  return RUN_ALL_TESTS();
}
