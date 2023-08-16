// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/rpc_utils.h"

#include <list>
#include <string>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <gtest/gtest.h>

namespace rmad {

class RpcUtilsTest : public testing::Test {
 public:
  RpcUtilsTest() : message_(""), rpc_output_(0) {}
  ~RpcUtilsTest() = default;

  void SetMessage(const std::string& s) { message_ = s; }

  // A simple function that runs the callback function with the input.
  void Rpc(int x, base::OnceCallback<void(int)> rpc_callback) const {
    std::move(rpc_callback).Run(x);
  }

  bool RpcOutputChecker(int x) const { return x > 3; }

  void SuccessHandler(base::OnceCallback<void(const std::string&)> callback,
                      int x) {
    rpc_output_ = x;
    std::move(callback).Run("success");
  }

  void FailHandler(base::OnceCallback<void(const std::string&)> callback) {
    std::move(callback).Run("fail");
  }

 protected:
  std::string message_;
  int rpc_output_;
};

TEST_F(RpcUtilsTest, SuccessHandlerCalled) {
  RunRpcWithInputs(
      base::BindOnce(&RpcUtilsTest::SetMessage, base::Unretained(this)),
      base::BindRepeating(&RpcUtilsTest::Rpc, base::Unretained(this)),
      std::list<int>{1, 2, 3, 4, 5},
      base::BindRepeating(&RpcUtilsTest::RpcOutputChecker,
                          base::Unretained(this)),
      base::BindOnce(&RpcUtilsTest::SuccessHandler, base::Unretained(this)),
      base::BindOnce(&RpcUtilsTest::FailHandler, base::Unretained(this)));
  EXPECT_EQ(message_, "success");
  EXPECT_EQ(rpc_output_, 4);
}

TEST_F(RpcUtilsTest, FailHandlerCalled) {
  RunRpcWithInputs(
      base::BindOnce(&RpcUtilsTest::SetMessage, base::Unretained(this)),
      base::BindRepeating(&RpcUtilsTest::Rpc, base::Unretained(this)),
      std::list<int>{1, 2, 3},
      base::BindRepeating(&RpcUtilsTest::RpcOutputChecker,
                          base::Unretained(this)),
      base::BindOnce(&RpcUtilsTest::SuccessHandler, base::Unretained(this)),
      base::BindOnce(&RpcUtilsTest::FailHandler, base::Unretained(this)));
  EXPECT_EQ(message_, "fail");
  EXPECT_EQ(rpc_output_, 0);
}

}  // namespace rmad
