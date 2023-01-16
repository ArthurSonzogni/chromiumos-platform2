// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/constants.h"
#include "rmad/logs/logs_constants.h"
#include "rmad/logs/logs_utils.h"
#include "rmad/metrics/metrics_utils.h"
#include "rmad/state_handler/components_repair_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/system/mock_cryptohome_client.h"
#include "rmad/system/mock_runtime_probe_client.h"
#include "rmad/utils/mock_crossystem_utils.h"

using ComponentRepairStatus =
    rmad::ComponentsRepairState::ComponentRepairStatus;
using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

class ComponentsRepairStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<ComponentsRepairStateHandler> CreateStateHandler(
      bool runtime_probe_client_retval,
      const ComponentsWithIdentifier& probed_components,
      bool ccd_blocked,
      int hwwp_enabled) {
    // Mock |CryptohomeClient|.
    auto mock_cryptohome_client =
        std::make_unique<NiceMock<MockCryptohomeClient>>();
    ON_CALL(*mock_cryptohome_client, IsCcdBlocked())
        .WillByDefault(Return(ccd_blocked));
    // Mock |RuntimeProbeClient|.
    auto mock_runtime_probe_client =
        std::make_unique<NiceMock<MockRuntimeProbeClient>>();
    ON_CALL(*mock_runtime_probe_client, ProbeCategories(_, _, _))
        .WillByDefault(DoAll(SetArgPointee<2>(probed_components),
                             Return(runtime_probe_client_retval)));
    // Mock |CrosSystemUtils|.
    auto mock_crossystem_utils =
        std::make_unique<NiceMock<MockCrosSystemUtils>>();
    ON_CALL(*mock_crossystem_utils,
            GetInt(Eq(CrosSystemUtils::kHwwpStatusProperty), _))
        .WillByDefault(DoAll(SetArgPointee<1>(hwwp_enabled), Return(true)));

    return base::MakeRefCounted<ComponentsRepairStateHandler>(
        json_store_, daemon_callback_, std::move(mock_cryptohome_client),
        std::move(mock_runtime_probe_client), std::move(mock_crossystem_utils));
  }

  RmadState CreateDefaultComponentsRepairState() {
    static const std::vector<RmadComponent> default_original_components = {
        RMAD_COMPONENT_KEYBOARD,           RMAD_COMPONENT_POWER_BUTTON,
        RMAD_COMPONENT_BASE_ACCELEROMETER, RMAD_COMPONENT_LID_ACCELEROMETER,
        RMAD_COMPONENT_BASE_GYROSCOPE,     RMAD_COMPONENT_LID_GYROSCOPE,
        RMAD_COMPONENT_AUDIO_CODEC};
    RmadState state;
    auto components_repair = std::make_unique<ComponentsRepairState>();
    for (auto component : default_original_components) {
      ComponentRepairStatus* component_repair_status =
          state.mutable_components_repair()->add_components();
      component_repair_status->set_component(component);
      component_repair_status->set_repair_status(
          ComponentRepairStatus::RMAD_REPAIR_STATUS_ORIGINAL);
      component_repair_status->set_identifier("");
    }
    return state;
  }
};

TEST_F(ComponentsRepairStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler(true, {}, false, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(ComponentsRepairStateHandlerTest, InitializeState_Fail) {
  auto handler = CreateStateHandler(false, {}, false, 1);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(ComponentsRepairStateHandlerTest,
       GetNextStateCase_Success_NonMlbRework) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, false, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = CreateDefaultComponentsRepairState();
  ComponentRepairStatus* component_repair_status =
      state.mutable_components_repair()->add_components();
  component_repair_status->set_component(RMAD_COMPONENT_BATTERY);
  component_repair_status->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_STATUS_REPLACED);
  component_repair_status->set_identifier("battery_abcd");

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kDeviceDestination);

  std::vector<std::string> replaced_components;
  EXPECT_TRUE(
      json_store_->GetValue(kReplacedComponentNames, &replaced_components));
  EXPECT_EQ(
      replaced_components,
      std::vector<std::string>{RmadComponent_Name(RMAD_COMPONENT_BATTERY)});

  // Verify the replaced component was recorded to logs.
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(1, events->size());
  const base::Value::Dict& event = (*events)[0].GetDict();
  EXPECT_EQ(static_cast<int>(LogEventType::kData), event.FindInt(kType));

  const base::Value::List* components =
      event.FindDict(kDetails)->FindList(kLogReplacedComponents);
  EXPECT_EQ(1, components->size());
  EXPECT_EQ(RmadComponent_Name(RMAD_COMPONENT_BATTERY),
            (*components)[0].GetString());
  EXPECT_FALSE(event.FindDict(kDetails)->FindBool(kLogReworkSelected).value());
}

TEST_F(ComponentsRepairStateHandlerTest,
       GetNextStateCase_Success_MlbRework_Case1) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, true, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_components_repair()->set_mainboard_rework(true);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableRsu);

  std::vector<std::string> replaced_components;
  EXPECT_TRUE(
      json_store_->GetValue(kReplacedComponentNames, &replaced_components));

  std::set<std::string> replaced_components_set(replaced_components.begin(),
                                                replaced_components.end());
  std::set<std::string> expected_replaced_components_set = {
      RmadComponent_Name(RMAD_COMPONENT_BATTERY),
      RmadComponent_Name(RMAD_COMPONENT_KEYBOARD),
      RmadComponent_Name(RMAD_COMPONENT_POWER_BUTTON),
      RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
      RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
      RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
      RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE),
      RmadComponent_Name(RMAD_COMPONENT_AUDIO_CODEC),
  };
  EXPECT_EQ(replaced_components_set, expected_replaced_components_set);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_FALSE(same_owner);

  bool wp_disable_required;
  EXPECT_TRUE(json_store_->GetValue(kWpDisableRequired, &wp_disable_required));
  EXPECT_TRUE(wp_disable_required);

  bool ccd_blocked;
  EXPECT_TRUE(json_store_->GetValue(kCcdBlocked, &ccd_blocked));
  EXPECT_TRUE(ccd_blocked);

  bool wipe_device;
  EXPECT_TRUE(json_store_->GetValue(kWipeDevice, &wipe_device));
  EXPECT_TRUE(wipe_device);

  // Verify the mainboard rework selection was recorded to logs.
  base::Value logs(base::Value::Type::DICT);
  json_store_->GetValue(kLogs, &logs);

  const base::Value::List* events = logs.GetDict().FindList(kEvents);
  EXPECT_EQ(1, events->size());
  const base::Value::Dict& event = (*events)[0].GetDict();
  EXPECT_TRUE(event.FindDict(kDetails)->FindBool(kLogReworkSelected).value());
}

TEST_F(ComponentsRepairStateHandlerTest,
       GetNextStateCase_Success_MlbRework_Case2_HwwpDisabled) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, false, 0);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_components_repair()->set_mainboard_rework(true);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisablePhysical);

  std::vector<std::string> replaced_components;
  EXPECT_TRUE(
      json_store_->GetValue(kReplacedComponentNames, &replaced_components));

  std::set<std::string> replaced_components_set(replaced_components.begin(),
                                                replaced_components.end());
  std::set<std::string> expected_replaced_components_set = {
      RmadComponent_Name(RMAD_COMPONENT_BATTERY),
      RmadComponent_Name(RMAD_COMPONENT_KEYBOARD),
      RmadComponent_Name(RMAD_COMPONENT_POWER_BUTTON),
      RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
      RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
      RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
      RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE),
      RmadComponent_Name(RMAD_COMPONENT_AUDIO_CODEC),
  };
  EXPECT_EQ(replaced_components_set, expected_replaced_components_set);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_FALSE(same_owner);

  bool wp_disable_required;
  EXPECT_TRUE(json_store_->GetValue(kWpDisableRequired, &wp_disable_required));
  EXPECT_TRUE(wp_disable_required);

  bool ccd_blocked;
  EXPECT_TRUE(json_store_->GetValue(kCcdBlocked, &ccd_blocked));
  EXPECT_FALSE(ccd_blocked);

  bool wipe_device;
  EXPECT_TRUE(json_store_->GetValue(kWipeDevice, &wipe_device));
  EXPECT_TRUE(wipe_device);
}

TEST_F(ComponentsRepairStateHandlerTest,
       GetNextStateCase_Success_MlbRework_Case2_HwwpEnabled) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, false, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state;
  state.mutable_components_repair()->set_mainboard_rework(true);

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kWpDisableMethod);

  std::vector<std::string> replaced_components;
  EXPECT_TRUE(
      json_store_->GetValue(kReplacedComponentNames, &replaced_components));

  std::set<std::string> replaced_components_set(replaced_components.begin(),
                                                replaced_components.end());
  std::set<std::string> expected_replaced_components_set = {
      RmadComponent_Name(RMAD_COMPONENT_BATTERY),
      RmadComponent_Name(RMAD_COMPONENT_KEYBOARD),
      RmadComponent_Name(RMAD_COMPONENT_POWER_BUTTON),
      RmadComponent_Name(RMAD_COMPONENT_BASE_ACCELEROMETER),
      RmadComponent_Name(RMAD_COMPONENT_LID_ACCELEROMETER),
      RmadComponent_Name(RMAD_COMPONENT_BASE_GYROSCOPE),
      RmadComponent_Name(RMAD_COMPONENT_LID_GYROSCOPE),
      RmadComponent_Name(RMAD_COMPONENT_AUDIO_CODEC),
  };
  EXPECT_EQ(replaced_components_set, expected_replaced_components_set);

  bool same_owner;
  EXPECT_TRUE(json_store_->GetValue(kSameOwner, &same_owner));
  EXPECT_FALSE(same_owner);

  bool wp_disable_required;
  EXPECT_TRUE(json_store_->GetValue(kWpDisableRequired, &wp_disable_required));
  EXPECT_TRUE(wp_disable_required);

  bool ccd_blocked;
  EXPECT_TRUE(json_store_->GetValue(kCcdBlocked, &ccd_blocked));
  EXPECT_FALSE(ccd_blocked);

  bool wipe_device;
  EXPECT_TRUE(json_store_->GetValue(kWipeDevice, &wipe_device));
  EXPECT_TRUE(wipe_device);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_MissingState) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, false, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No ComponentsRepairState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_UnknownComponent) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, false, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = CreateDefaultComponentsRepairState();
  ComponentRepairStatus* component_repair_status =
      state.mutable_components_repair()->add_components();
  component_repair_status->set_component(RMAD_COMPONENT_BATTERY);
  component_repair_status->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_STATUS_ORIGINAL);
  component_repair_status->set_identifier("battery_abcd");
  // RMAD_COMPONENT_NETWORK is deprecated.
  component_repair_status = state.mutable_components_repair()->add_components();
  component_repair_status->set_component(RMAD_COMPONENT_NETWORK);
  component_repair_status->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_STATUS_ORIGINAL);
  component_repair_status->set_identifier("network_abcd");

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_UnprobedComponent) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, false, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = CreateDefaultComponentsRepairState();
  ComponentRepairStatus* component_repair_status =
      state.mutable_components_repair()->add_components();
  component_repair_status->set_component(RMAD_COMPONENT_BATTERY);
  component_repair_status->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_STATUS_ORIGINAL);
  component_repair_status->set_identifier("battery_abcd");
  // RMAD_COMPONENT_STORAGE is not probed.
  component_repair_status = state.mutable_components_repair()->add_components();
  component_repair_status->set_component(RMAD_COMPONENT_STORAGE);
  component_repair_status->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_STATUS_ORIGINAL);
  component_repair_status->set_identifier("storage_abcd");

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(ComponentsRepairStateHandlerTest,
       GetNextStateCase_MissingProbedComponent) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, false, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  RmadState state = CreateDefaultComponentsRepairState();
  // RMAD_COMPONENT_BATTERY is probed but set to MISSING.
  ComponentRepairStatus* component_repair_status =
      state.mutable_components_repair()->add_components();
  component_repair_status->set_component(RMAD_COMPONENT_BATTERY);
  component_repair_status->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_STATUS_MISSING);
  component_repair_status->set_identifier("storage_abcd");

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_UnknownRepairState) {
  auto handler = CreateStateHandler(
      true, {{RMAD_COMPONENT_BATTERY, "battery_abcd"}}, false, 1);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // State doesn't contain RMAD_COMPONENT_BATTERY.
  RmadState state = CreateDefaultComponentsRepairState();

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

}  // namespace rmad
