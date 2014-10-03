// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/connection_tester.h"

#include <string>

#include <base/bind.h>
#include <base/memory/scoped_ptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/connectivity_trial.h"
#include "shill/mock_connection.h"
#include "shill/mock_connectivity_trial.h"
#include "shill/mock_control.h"
#include "shill/mock_device_info.h"
#include "shill/mock_event_dispatcher.h"

using base::Bind;
using base::Callback;
using base::Unretained;
using std::string;
using std::vector;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;
using testing::Test;

namespace shill {

class ConnectionTesterTest : public Test {
 public:
  ConnectionTesterTest()
      : device_info_(
            new NiceMock<MockDeviceInfo>(&control_, nullptr, nullptr, nullptr)),
        connection_(new StrictMock<MockConnection>(device_info_.get())),
        connection_tester_(
            new ConnectionTester(connection_.get(), &dispatcher_,
                                 callback_target_.tester_callback())),
        connectivity_trial_(new StrictMock<MockConnectivityTrial>(
            connection_, ConnectionTester::kTrialTimeoutSeconds)) {}

  virtual void SetUp() {
    EXPECT_CALL(*connection_.get(), IsIPv6())
        .WillRepeatedly(Return(false));
    connection_tester_->connectivity_trial_
        .reset(connectivity_trial_);  // Passes ownership
    EXPECT_TRUE(connection_tester()->connectivity_trial_.get());
  }

  virtual void TearDown() {
    if (connection_tester()->connectivity_trial_.get()) {
      EXPECT_CALL(*connectivity_trial(), Stop());

      // Delete the connection tester while expectations still exist.
      connection_tester_.reset();
    }
  }

 protected:
  class CallbackTarget {
   public:
    CallbackTarget()
        : tester_callback_(Bind(&CallbackTarget::TesterCallback,
                                Unretained(this))) {
    }

    MOCK_METHOD0(TesterCallback, void());
    Callback<void()> &tester_callback() {
      return tester_callback_;
    }

   private:
    Callback<void()> tester_callback_;
  };

  void StartConnectivityTest() {
    connection_tester_->Start();
  }

  ConnectionTester *connection_tester() { return connection_tester_.get(); }
  MockConnectivityTrial *connectivity_trial() { return connectivity_trial_; }
  MockEventDispatcher &dispatcher() { return dispatcher_; }
  CallbackTarget &callback_target() { return callback_target_; }

  void ExpectReset() {
    EXPECT_TRUE(callback_target_.tester_callback().
                Equals(connection_tester_->tester_callback_));
  }

 private:
  StrictMock<MockEventDispatcher> dispatcher_;
  MockControl control_;
  scoped_ptr<MockDeviceInfo> device_info_;
  scoped_refptr<MockConnection> connection_;
  CallbackTarget callback_target_;
  scoped_ptr<ConnectionTester> connection_tester_;
  MockConnectivityTrial* connectivity_trial_;
};

TEST_F(ConnectionTesterTest, Constructor) {
  ExpectReset();
}

TEST_F(ConnectionTesterTest, StartTest) {
  EXPECT_CALL(*connectivity_trial(), Start(_, _)).Times(1);
  StartConnectivityTest();
}

TEST_F(ConnectionTesterTest, StartTestRepeated) {
  EXPECT_CALL(*connectivity_trial(), Start(_, _)).WillOnce(Return(true));
  StartConnectivityTest();

  EXPECT_CALL(*connectivity_trial(), Start(_, _)).WillOnce(Return(true));
  StartConnectivityTest();
}

TEST_F(ConnectionTesterTest, StopTest) {
  EXPECT_CALL(*connectivity_trial(), Stop()).Times(1);
  connection_tester()->Stop();
}

TEST_F(ConnectionTesterTest, CompleteTest) {
  ConnectivityTrial::Result result =
      ConnectivityTrial::Result(ConnectivityTrial::kPhaseContent,
                                ConnectivityTrial::kStatusSuccess);
  EXPECT_CALL(*connectivity_trial(), Stop()).Times(1);
  EXPECT_CALL(callback_target(), TesterCallback()).Times(1);
  connection_tester()->CompleteTest(result);
}

}  // namespace shill
