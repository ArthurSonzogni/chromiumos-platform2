// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/smart_dim_requestor.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>
#include <base/run_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "power_manager/powerd/policy/state_controller.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"

namespace power_manager {
namespace policy {

class MockStateController : public StateController {
 public:
  MOCK_METHOD(void, HandleDeferFromSmartDim, ());
};

class SmartDimRequestorTest : public ::testing::Test {
 public:
  SmartDimRequestorTest() {}
  void SetUp() override {
    ml_decision_dbus_proxy_ = dbus_wrapper_.GetObjectProxy(
        chromeos::kMlDecisionServiceName, chromeos::kMlDecisionServicePath);
    dbus_wrapper_.SetMethodCallback(base::BindRepeating(
        &SmartDimRequestorTest::HandleMethodCall, base::Unretained(this)));
  }
  // Initialize smart_dim_requestor_.
  void InitWithMlServiceAvailability(const bool available) {
    smart_dim_requestor_.Init(&dbus_wrapper_, &mock_state_controller_);
    dbus_wrapper_.NotifyServiceAvailable(ml_decision_dbus_proxy_, available);
  }

 protected:
  // DBusWrapperStub::MethodCallback implementation used to handle D-Bus calls
  // from |ml_decision_dbus_proxy_|.
  std::unique_ptr<dbus::Response> HandleMethodCall(
      dbus::ObjectProxy* proxy, dbus::MethodCall* method_call) {
    std::unique_ptr<dbus::Response> response =
        dbus::Response::FromMethodCall(method_call);
    ++num_of_method_calls_;
    if (proxy != ml_decision_dbus_proxy_) {
      ADD_FAILURE() << "Unhandled method call to proxy " << proxy;
      response.reset();
    } else if (method_call->GetInterface() !=
               chromeos::kMlDecisionServiceInterface) {
      ADD_FAILURE() << "Unhandled method call to interface "
                    << method_call->GetInterface();
      response.reset();
    } else if (method_call->GetMember() ==
               chromeos::kMlDecisionServiceShouldDeferScreenDimMethod) {
      dbus::MessageWriter(response.get()).AppendBool(should_defer_);
    } else {
      ADD_FAILURE() << "Unhandled method call to member "
                    << method_call->GetMember();
      response.reset();
    }
    return response;
  }

  system::DBusWrapperStub dbus_wrapper_;
  SmartDimRequestor smart_dim_requestor_;
  MockStateController mock_state_controller_;
  int num_of_method_calls_ = 0;
  bool should_defer_ = false;
  dbus::ObjectProxy* ml_decision_dbus_proxy_ = nullptr;
};

TEST_F(SmartDimRequestorTest, NotEnabledIfMlServiceUnavailable) {
  InitWithMlServiceAvailability(false);
  EXPECT_FALSE(smart_dim_requestor_.IsEnabled());
}

TEST_F(SmartDimRequestorTest, EnabledIfMlServiceAvailable) {
  InitWithMlServiceAvailability(true);
  EXPECT_TRUE(smart_dim_requestor_.IsEnabled());
}

TEST_F(SmartDimRequestorTest, NotReadyIfLessThanDimImminent) {
  InitWithMlServiceAvailability(true);

  base::TimeDelta screen_dim_imminent = base::TimeDelta::FromSeconds(2);
  // last_smart_dim_decision_request_time_ is initialized as base::TimeTicks().
  // now is set to be half of the duration of screen_dim_imminent.
  base::TimeTicks now = base::TimeTicks() + screen_dim_imminent / 2;

  EXPECT_FALSE(smart_dim_requestor_.ReadyForRequest(now, screen_dim_imminent));
}

TEST_F(SmartDimRequestorTest, HandleSmartDimShouldDefer) {
  InitWithMlServiceAvailability(true);

  base::TimeDelta screen_dim_imminent = base::TimeDelta::FromSeconds(2);
  base::TimeTicks now = base::TimeTicks() + screen_dim_imminent;

  // The HandleDeferFromSmartDim should be called once.
  EXPECT_CALL(mock_state_controller_, HandleDeferFromSmartDim).Times(1);
  should_defer_ = true;
  smart_dim_requestor_.RequestSmartDimDecision(now);
  base::RunLoop().RunUntilIdle();
  // Exactly one dbus call should be sent.
  EXPECT_EQ(num_of_method_calls_, 1);
}

TEST_F(SmartDimRequestorTest, HandleSmartDimShouldNotDefer) {
  InitWithMlServiceAvailability(true);

  base::TimeDelta screen_dim_imminent = base::TimeDelta::FromSeconds(2);
  base::TimeTicks now = base::TimeTicks() + screen_dim_imminent;

  // Exactly one dbus call should be sent.
  should_defer_ = false;
  EXPECT_CALL(mock_state_controller_, HandleDeferFromSmartDim).Times(0);
  smart_dim_requestor_.RequestSmartDimDecision(now);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(num_of_method_calls_, 1);
}

}  // namespace policy
}  // namespace power_manager
