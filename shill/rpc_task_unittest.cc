// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/rpc_task.h"

#include <gtest/gtest.h>

#include "shill/mock_adaptors.h"
#include "shill/nice_mock_control.h"

using std::map;
using std::string;
using std::vector;

namespace shill {

class RPCTaskTest : public testing::Test,
                    public RPCTaskDelegate {
 public:
  RPCTaskTest()
      : get_login_calls_(0),
        notify_calls_(0),
        task_(&control_, this) {}

  // Inherited from RPCTaskDelegate.
  virtual void GetLogin(string *user, string *password);
  virtual void Notify(const string &reason, const map<string, string> &dict);

 protected:
  int get_login_calls_;
  int notify_calls_;
  string *last_user_;
  string *last_password_;
  string last_notify_reason_;
  map<string, string> last_notify_dict_;
  NiceMockControl control_;
  RPCTask task_;
};

void RPCTaskTest::GetLogin(string *user, string *password) {
  get_login_calls_++;
  last_user_ = user;
  last_password_ = password;
}

void RPCTaskTest::Notify(const string &reason,
                         const map<string, string> &dict) {
  notify_calls_++;
  last_notify_reason_ = reason;
  last_notify_dict_ = dict;
}

TEST_F(RPCTaskTest, GetEnvironment) {
  vector<string> env = task_.GetEnvironment();
  ASSERT_EQ(2, env.size());
  EXPECT_EQ(
      string(kRPCTaskServiceVariable) + "=" + RPCTaskMockAdaptor::kRpcConnId,
      env[0]);
  EXPECT_EQ(string(kRPCTaskPathVariable) + "=" + RPCTaskMockAdaptor::kRpcId,
            env[1]);
}

TEST_F(RPCTaskTest, GetRpcIdentifiers) {
  EXPECT_EQ(RPCTaskMockAdaptor::kRpcId, task_.GetRpcIdentifier());
  EXPECT_EQ(RPCTaskMockAdaptor::kRpcConnId, task_.GetRpcConnectionIdentifier());
}

TEST_F(RPCTaskTest, GetLogin) {
  string user, password;
  task_.GetLogin(&user, &password);
  EXPECT_EQ(1, get_login_calls_);
  EXPECT_EQ(&user, last_user_);
  EXPECT_EQ(&password, last_password_);
}

TEST_F(RPCTaskTest, Notify) {
  static const char kReason[] = "up";
  map<string, string> dict;
  dict["foo"] = "bar";
  task_.Notify(kReason, dict);
  EXPECT_EQ(1, notify_calls_);
  EXPECT_EQ(kReason, last_notify_reason_);
  EXPECT_EQ("bar", last_notify_dict_["foo"]);
}

}  // namespace shill
