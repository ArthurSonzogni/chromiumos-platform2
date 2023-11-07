// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <base/check.h>
#include <base/functional/callback.h>
#include <base/memory/scoped_refptr.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "federated/memory_pressure_training_condition.h"

namespace federated {
namespace {

using ::resource_manager::kMemoryPressureArcvm;
using ::resource_manager::kMemoryPressureChrome;
using ::resource_manager::kResourceManagerInterface;
using ::resource_manager::kResourceManagerServiceName;
using ::resource_manager::kResourceManagerServicePath;

using ::testing::_;
using ::testing::Invoke;
using ::testing::SaveArg;
using ::testing::StrictMock;

}  // namespace

class MemoryPressureTrainingConditionTest : public ::testing::Test {
 public:
  MemoryPressureTrainingConditionTest()
      : mock_dbus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        dbus_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            mock_dbus_.get(),
            kResourceManagerServiceName,
            dbus::ObjectPath(kResourceManagerServicePath))) {}
  MemoryPressureTrainingConditionTest(
      const MemoryPressureTrainingConditionTest&) = delete;
  MemoryPressureTrainingConditionTest& operator=(
      const MemoryPressureTrainingConditionTest&) = delete;

  void SetUp() override {
    EXPECT_CALL(*mock_dbus_,
                GetObjectProxy(kResourceManagerServiceName,
                               dbus::ObjectPath(kResourceManagerServicePath)))
        .WillOnce(Return(dbus_object_proxy_.get()));

    EXPECT_CALL(*dbus_object_proxy_,
                DoConnectToSignal(kResourceManagerInterface,
                                  kMemoryPressureChrome, _, _))
        .WillOnce(SaveArg<2>(&on_signal_callbacks_[kMemoryPressureChrome]));

    EXPECT_CALL(*dbus_object_proxy_,
                DoConnectToSignal(kResourceManagerInterface,
                                  kMemoryPressureArcvm, _, _))
        .WillOnce(SaveArg<2>(&on_signal_callbacks_[kMemoryPressureArcvm]));

    memory_pressure_training_condition_ =
        std::make_unique<MemoryPressureTrainingCondition>(mock_dbus_.get());
  }

  // Creates a dbus signal with the given `signal_name`, writes the given level
  // to it and invokes.
  void CreateSignalAndInvoke(const uint8_t pressure_level,
                             const std::string& signal_name) {
    dbus::Signal signal(kResourceManagerInterface, signal_name);
    dbus::MessageWriter writer(&signal);
    writer.AppendByte(pressure_level);

    auto callback_iter = on_signal_callbacks_.find(signal_name);
    ASSERT_NE(callback_iter, on_signal_callbacks_.end());
    callback_iter->second.Run(&signal);
  }

  MemoryPressureTrainingCondition* memory_pressure_training_condition() const {
    DCHECK(memory_pressure_training_condition_);
    return memory_pressure_training_condition_.get();
  }

 private:
  scoped_refptr<StrictMock<dbus::MockBus>> mock_dbus_;

  scoped_refptr<StrictMock<dbus::MockObjectProxy>> dbus_object_proxy_;

  std::unique_ptr<MemoryPressureTrainingCondition>
      memory_pressure_training_condition_;

  std::unordered_map<std::string,
                     base::RepeatingCallback<void(dbus::Signal* signal)>>
      on_signal_callbacks_;
};

// When no signals are received, we treat the memory level is moderated.
TEST_F(MemoryPressureTrainingConditionTest, InitializedTrue) {
  EXPECT_TRUE(
      memory_pressure_training_condition()->IsTrainingConditionSatisfied());
}

// Tests that memory pressure level signals are properly handled.
TEST_F(MemoryPressureTrainingConditionTest, MemoryPressureSignals) {
  EXPECT_TRUE(
      memory_pressure_training_condition()->IsTrainingConditionSatisfied());

  // Chrome memory level doesn't meet.
  CreateSignalAndInvoke(2, kMemoryPressureChrome);
  EXPECT_FALSE(
      memory_pressure_training_condition()->IsTrainingConditionSatisfied());

  // Arc vm memory level doesn't meet.
  CreateSignalAndInvoke(2, kMemoryPressureArcvm);
  EXPECT_FALSE(
      memory_pressure_training_condition()->IsTrainingConditionSatisfied());

  // Chrome memory level meets again, but arc vm service level still doesn't
  // meet.
  CreateSignalAndInvoke(1, kMemoryPressureChrome);
  EXPECT_FALSE(
      memory_pressure_training_condition()->IsTrainingConditionSatisfied());

  // Arc vm memory level meets again.
  CreateSignalAndInvoke(1, kMemoryPressureArcvm);
  EXPECT_TRUE(
      memory_pressure_training_condition()->IsTrainingConditionSatisfied());
}

}  // namespace federated
