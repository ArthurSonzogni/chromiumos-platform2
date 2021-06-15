// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

#include "rmad/state_handler/components_repair_state_handler.h"
#include "rmad/state_handler/state_handler_test_common.h"
#include "rmad/utils/mock_dbus_utils.h"

using ComponentRepairStatus =
    rmad::ComponentsRepairState::ComponentRepairStatus;
using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;
using testing::WithArg;

namespace rmad {

class ComponentsRepairStateHandlerTest : public StateHandlerTest {
 public:
  scoped_refptr<ComponentsRepairStateHandler> CreateStateHandler(
      const runtime_probe::ProbeResult& runtime_probe_reply,
      bool runtime_probe_retval) {
    // Mock the D-Bus call.
    auto mock_dbus_utils = std::make_unique<StrictMock<MockDBusUtils>>();
    EXPECT_CALL(*mock_dbus_utils, CallDBusMethod(_, _, _, _, _, _, _))
        .WillOnce(
            DoAll(WithArg<5>(Invoke([=](google::protobuf::MessageLite* reply) {
                    *static_cast<runtime_probe::ProbeResult*>(reply) =
                        runtime_probe_reply;
                  })),
                  Return(runtime_probe_retval)));

    return base::MakeRefCounted<ComponentsRepairStateHandler>(
        json_store_, std::move(mock_dbus_utils));
  }

  std::unique_ptr<ComponentsRepairState> CreateDefaultComponentsRepairState() {
    static const std::vector<ComponentRepairStatus::Component>
        default_original_components = {
            ComponentRepairStatus::RMAD_COMPONENT_MAINBOARD_REWORK,
            ComponentRepairStatus::RMAD_COMPONENT_KEYBOARD,
            ComponentRepairStatus::RMAD_COMPONENT_POWER_BUTTON};
    auto components_repair = std::make_unique<ComponentsRepairState>();
    for (auto component : default_original_components) {
      ComponentRepairStatus* component_repair =
          components_repair->add_components();
      component_repair->set_component(component);
      component_repair->set_repair_status(
          ComponentRepairStatus::RMAD_REPAIR_ORIGINAL);
    }
    return components_repair;
  }
};

TEST_F(ComponentsRepairStateHandlerTest, InitializeState_Success) {
  auto handler = CreateStateHandler(runtime_probe::ProbeResult(), true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);
}

TEST_F(ComponentsRepairStateHandlerTest, InitializeState_DBusFail) {
  auto handler = CreateStateHandler(runtime_probe::ProbeResult(), false);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(ComponentsRepairStateHandlerTest, InitializeState_RuntimeProbeFail) {
  runtime_probe::ProbeResult reply;
  reply.set_error(runtime_probe::RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
  auto handler = CreateStateHandler(reply, true);
  EXPECT_EQ(handler->InitializeState(),
            RMAD_ERROR_STATE_HANDLER_INITIALIZATION_FAILED);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_Success) {
  runtime_probe::ProbeResult reply;
  reply.add_battery();
  auto handler = CreateStateHandler(reply, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<ComponentsRepairState> components_repair =
      CreateDefaultComponentsRepairState();
  ComponentRepairStatus* component_repair = components_repair->add_components();
  component_repair->set_component(
      ComponentRepairStatus::RMAD_COMPONENT_BATTERY);
  component_repair->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_ORIGINAL);
  RmadState state;
  state.set_allocated_components_repair(components_repair.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_OK);
  EXPECT_EQ(state_case, RmadState::StateCase::kDeviceDestination);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_MissingState) {
  runtime_probe::ProbeResult reply;
  reply.add_battery();
  auto handler = CreateStateHandler(reply, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // No ComponentsRepairState.
  RmadState state;

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_UnknownComponent) {
  runtime_probe::ProbeResult reply;
  reply.add_battery();
  auto handler = CreateStateHandler(reply, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<ComponentsRepairState> components_repair =
      CreateDefaultComponentsRepairState();
  ComponentRepairStatus* component_repair = components_repair->add_components();
  component_repair->set_component(
      ComponentRepairStatus::RMAD_COMPONENT_BATTERY);
  component_repair->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_ORIGINAL);
  // RMAD_COMPONENT_NETWORK is deprecated.
  component_repair = components_repair->add_components();
  component_repair->set_component(
      ComponentRepairStatus::RMAD_COMPONENT_NETWORK);
  component_repair->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_ORIGINAL);

  RmadState state;
  state.set_allocated_components_repair(components_repair.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_UnprobedComponent) {
  runtime_probe::ProbeResult reply;
  reply.add_battery();
  auto handler = CreateStateHandler(reply, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<ComponentsRepairState> components_repair =
      CreateDefaultComponentsRepairState();
  ComponentRepairStatus* component_repair = components_repair->add_components();
  component_repair->set_component(
      ComponentRepairStatus::RMAD_COMPONENT_BATTERY);
  component_repair->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_ORIGINAL);
  // RMAD_COMPONENT_STORAGE is not probed.
  component_repair = components_repair->add_components();
  component_repair->set_component(
      ComponentRepairStatus::RMAD_COMPONENT_STORAGE);
  component_repair->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_ORIGINAL);

  RmadState state;
  state.set_allocated_components_repair(components_repair.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(ComponentsRepairStateHandlerTest,
       GetNextStateCase_MissingProbedComponent) {
  runtime_probe::ProbeResult reply;
  reply.add_battery();
  auto handler = CreateStateHandler(reply, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  std::unique_ptr<ComponentsRepairState> components_repair =
      CreateDefaultComponentsRepairState();
  // RMAD_COMPONENT_BATTERY is probed but set to MISSING.
  ComponentRepairStatus* component_repair = components_repair->add_components();
  component_repair->set_component(
      ComponentRepairStatus::RMAD_COMPONENT_BATTERY);
  component_repair->set_repair_status(
      ComponentRepairStatus::RMAD_REPAIR_MISSING);

  RmadState state;
  state.set_allocated_components_repair(components_repair.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

TEST_F(ComponentsRepairStateHandlerTest, GetNextStateCase_UnknownRepairState) {
  runtime_probe::ProbeResult reply;
  reply.add_battery();
  auto handler = CreateStateHandler(reply, true);
  EXPECT_EQ(handler->InitializeState(), RMAD_ERROR_OK);

  // RMAD_COMPONENT_BATTERY is still UNKNOWN.
  std::unique_ptr<ComponentsRepairState> components_repair =
      CreateDefaultComponentsRepairState();

  RmadState state;
  state.set_allocated_components_repair(components_repair.release());

  auto [error, state_case] = handler->GetNextStateCase(state);
  EXPECT_EQ(error, RMAD_ERROR_REQUEST_INVALID);
  EXPECT_EQ(state_case, RmadState::StateCase::kComponentsRepair);
}

}  // namespace rmad
