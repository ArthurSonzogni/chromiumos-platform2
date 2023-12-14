// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/action_runner.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "heartd/daemon/test_utils/mock_dbus_connector.h"

namespace heartd {

namespace {

namespace mojom = ::ash::heartd::mojom;

using ::testing::_;
using ::testing::Exactly;

class ActionRunnerTest : public testing::Test {
 public:
  ActionRunnerTest() {}
  ~ActionRunnerTest() override = default;

 protected:
  MockDbusConnector mock_dbus_connector_;
  ActionRunner action_runner_{&mock_dbus_connector_};
};

TEST_F(ActionRunnerTest, DefaultSetting) {
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(0));

  action_runner_.Run(mojom::ServiceName::kKiosk,
                     mojom::ActionType::kNormalReboot);
}

TEST_F(ActionRunnerTest, EnableNormalRebootActionForNormalReboot) {
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(1));

  action_runner_.EnableNormalRebootAction();
  action_runner_.Run(mojom::ServiceName::kKiosk,
                     mojom::ActionType::kNormalReboot);
}

TEST_F(ActionRunnerTest, EnableNormalRebootActionForOtherAction) {
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(0));

  action_runner_.EnableNormalRebootAction();
  auto start_action = mojom::ActionType::kNoOperation;
  switch (start_action) {
    case mojom::ActionType::kNoOperation:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNoOperation);
      [[fallthrough]];
    case mojom::ActionType::kNormalReboot:
      // It should reboot, it's tested in
      // EnableNormalRebootActionForNormalReboot.
      [[fallthrough]];
    case mojom::ActionType::kForceReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kForceReboot);
      [[fallthrough]];
    case mojom::ActionType::kUnmappedEnumField:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kUnmappedEnumField);
      break;
  }
}

TEST_F(ActionRunnerTest, DisableNormalRebootAction) {
  EXPECT_CALL(*mock_dbus_connector_.power_manager_proxy(),
              RequestRestartAsync(_, _, _, _, _))
      .Times(Exactly(0));

  action_runner_.EnableNormalRebootAction();
  action_runner_.DisableNormalRebootAction();
  auto start_action = mojom::ActionType::kNoOperation;
  switch (start_action) {
    case mojom::ActionType::kNoOperation:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNoOperation);
      [[fallthrough]];
    case mojom::ActionType::kNormalReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kNormalReboot);
      [[fallthrough]];
    case mojom::ActionType::kForceReboot:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kForceReboot);
      [[fallthrough]];
    case mojom::ActionType::kUnmappedEnumField:
      action_runner_.Run(mojom::ServiceName::kKiosk,
                         mojom::ActionType::kUnmappedEnumField);
      break;
  }
}

}  // namespace

}  // namespace heartd
