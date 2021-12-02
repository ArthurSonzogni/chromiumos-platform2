// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/dim_advisor.h"

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

#include "hps/proto_bindings/hps_service.pb.h"
#include "power_manager/powerd/policy/state_controller.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"

namespace power_manager {
namespace policy {

using HpsResult = hps::HpsResult;

class MockStateController : public StateController {
 public:
  MOCK_METHOD(void, HandleDeferFromSmartDim, ());
  MOCK_METHOD(void, HandleHpsResultChange, (HpsResult hps_result));
};

class DimAdvisorTest : public ::testing::Test {
 public:
  DimAdvisorTest() {}
  void SetUp() override {
    ml_decision_dbus_proxy_ = dbus_wrapper_.GetObjectProxy(
        chromeos::kMlDecisionServiceName, chromeos::kMlDecisionServicePath);
    dbus_wrapper_.SetMethodCallback(base::BindRepeating(
        &DimAdvisorTest::HandleMethodCall, base::Unretained(this)));
    hps_dbus_proxy_ = dbus_wrapper_.GetObjectProxy(hps::kHpsServiceName,
                                                   hps::kHpsServicePath);
  }
  // Initialize dim_advisor_.
  void InitWithMlServiceAvailability(const bool available) {
    dim_advisor_.Init(&dbus_wrapper_, &mock_state_controller_);
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

  void EmitHpsSignal(HpsResult result) {
    hps::HpsResultProto result_proto;
    result_proto.set_value(result);

    dbus::Signal signal(hps::kHpsServiceInterface, hps::kHpsSenseChanged);
    dbus::MessageWriter writer(&signal);
    writer.AppendProtoAsArrayOfBytes(result_proto);
    dbus_wrapper_.EmitRegisteredSignal(hps_dbus_proxy_, &signal);
  }

  system::DBusWrapperStub dbus_wrapper_;
  DimAdvisor dim_advisor_;
  MockStateController mock_state_controller_;
  int num_of_method_calls_ = 0;
  bool should_defer_ = false;
  dbus::ObjectProxy* ml_decision_dbus_proxy_ = nullptr;
  dbus::ObjectProxy* hps_dbus_proxy_ = nullptr;
};

TEST_F(DimAdvisorTest, NotEnabledIfMlServiceUnavailable) {
  InitWithMlServiceAvailability(false);
  EXPECT_FALSE(dim_advisor_.IsSmartDimEnabled());
}

TEST_F(DimAdvisorTest, EnabledIfMlServiceAvailable) {
  InitWithMlServiceAvailability(true);
  EXPECT_TRUE(dim_advisor_.IsSmartDimEnabled());
}

TEST_F(DimAdvisorTest, NotReadyIfLessThanDimImminent) {
  InitWithMlServiceAvailability(true);

  base::TimeDelta screen_dim_imminent = base::TimeDelta::FromSeconds(2);
  // last_smart_dim_decision_request_time_ is initialized as base::TimeTicks().
  // now is set to be half of the duration of screen_dim_imminent.
  base::TimeTicks now = base::TimeTicks() + screen_dim_imminent / 2;

  EXPECT_FALSE(dim_advisor_.ReadyForSmartDimRequest(now, screen_dim_imminent));
}

TEST_F(DimAdvisorTest, HandleSmartDimShouldDefer) {
  InitWithMlServiceAvailability(true);

  base::TimeDelta screen_dim_imminent = base::TimeDelta::FromSeconds(2);
  base::TimeTicks now = base::TimeTicks() + screen_dim_imminent;

  // The HandleDeferFromSmartDim should be called once.
  EXPECT_CALL(mock_state_controller_, HandleDeferFromSmartDim).Times(1);
  should_defer_ = true;
  dim_advisor_.RequestSmartDimDecision(now);
  base::RunLoop().RunUntilIdle();
  // Exactly one dbus call should be sent.
  EXPECT_EQ(num_of_method_calls_, 1);
}

TEST_F(DimAdvisorTest, HandleSmartDimShouldNotDefer) {
  InitWithMlServiceAvailability(true);

  base::TimeDelta screen_dim_imminent = base::TimeDelta::FromSeconds(2);
  base::TimeTicks now = base::TimeTicks() + screen_dim_imminent;

  // Exactly one dbus call should be sent.
  should_defer_ = false;
  EXPECT_CALL(mock_state_controller_, HandleDeferFromSmartDim).Times(0);
  dim_advisor_.RequestSmartDimDecision(now);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(num_of_method_calls_, 1);
}

TEST_F(DimAdvisorTest, HpsIsEnabledAfterGettingFirstSignal) {
  InitWithMlServiceAvailability(false);

  EXPECT_FALSE(dim_advisor_.IsHpsSenseEnabled());
  EXPECT_CALL(mock_state_controller_, HandleHpsResultChange).Times(1);
  EmitHpsSignal(HpsResult::POSITIVE);
  EXPECT_TRUE(dim_advisor_.IsHpsSenseEnabled());
}

TEST_F(DimAdvisorTest, HandleHpsResultChange) {
  InitWithMlServiceAvailability(false);

  EXPECT_CALL(mock_state_controller_,
              HandleHpsResultChange(HpsResult::NEGATIVE))
      .Times(1);
  EmitHpsSignal(HpsResult::NEGATIVE);

  EXPECT_CALL(mock_state_controller_,
              HandleHpsResultChange(HpsResult::POSITIVE))
      .Times(1);
  EmitHpsSignal(HpsResult::POSITIVE);

  EXPECT_CALL(mock_state_controller_, HandleHpsResultChange(HpsResult::UNKNOWN))
      .Times(1);
  EmitHpsSignal(HpsResult::UNKNOWN);
}

}  // namespace policy
}  // namespace power_manager
