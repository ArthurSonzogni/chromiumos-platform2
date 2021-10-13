// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/rmad_interface_impl.h"
#include "rmad/state_handler/mock_state_handler.h"
#include "rmad/state_handler/state_handler_manager.h"
#include "rmad/system/mock_runtime_probe_client.h"
#include "rmad/system/mock_shill_client.h"
#include "rmad/system/mock_tpm_manager_client.h"
#include "rmad/utils/json_store.h"

using testing::_;
using testing::Assign;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SetArgPointee;

namespace rmad {

constexpr char kJsonStoreFileName[] = "json_store_file";
constexpr char kCurrentStateSetJson[] = R"({"state_history": [ 1 ]})";
constexpr char kCurrentStateNotSetJson[] = "{}";
constexpr char kCurrentStateInvalidStateJson[] = R"("state_history": [0])";
constexpr char kCurrentStateWithRepeatableHistoryJson[] =
    R"({"state_history": [ 1, 2 ]})";
constexpr char kCurrentStateWithUnrepeatableHistoryJson[] =
    R"({"state_history": [ 1, 2, 3 ]})";
constexpr char kCurrentStateWithUnsupportedStateJson[] =
    R"({"state_history": [ 1, 2, 4 ]})";
constexpr char kInitializeCurrentStateFailJson[] =
    R"({"state_history": [ 1 ]})";
constexpr char kInitializeNextStateFailJson[] =
    R"({"state_history": [ 1, 2 ]})";
constexpr char kInitializePreviousStateFailJson[] =
    R"({"state_history": [ 1, 2 ]})";
constexpr char kInvalidJson[] = R"(alfkjklsfsgdkjnbknd^^)";

class RmadInterfaceImplTest : public testing::Test {
 public:
  RmadInterfaceImplTest() {
    welcome_proto_.set_allocated_welcome(new WelcomeState());
    components_repair_proto_.set_allocated_components_repair(
        new ComponentsRepairState());
    device_destination_proto_.set_allocated_device_destination(
        new DeviceDestinationState());
  }

  base::FilePath CreateInputFile(std::string filename,
                                 const char* str,
                                 int size) {
    base::FilePath file_path = temp_dir_.GetPath().AppendASCII(filename);
    base::WriteFile(file_path, str, size);
    return file_path;
  }

  scoped_refptr<BaseStateHandler> CreateMockHandler(
      scoped_refptr<JsonStore> json_store,
      const RmadState& state,
      bool is_repeatable,
      RmadErrorCode initialize_error,
      RmadState::StateCase next_state) {
    auto mock_handler =
        base::MakeRefCounted<NiceMock<MockStateHandler>>(json_store);
    RmadState::StateCase state_case = state.state_case();
    ON_CALL(*mock_handler, GetStateCase()).WillByDefault(Return(state_case));
    ON_CALL(*mock_handler, GetState()).WillByDefault(ReturnRef(state));
    ON_CALL(*mock_handler, IsRepeatable()).WillByDefault(Return(is_repeatable));
    ON_CALL(*mock_handler, InitializeState())
        .WillByDefault(Return(initialize_error));
    ON_CALL(*mock_handler, GetNextStateCase(_))
        .WillByDefault(Return(BaseStateHandler::GetNextStateCaseReply{
            .error = RMAD_ERROR_OK, .state_case = next_state}));
    return mock_handler;
  }

  std::unique_ptr<StateHandlerManager> CreateStateHandlerManagerWithHandlers(
      scoped_refptr<JsonStore> json_store,
      std::vector<scoped_refptr<BaseStateHandler>> mock_handlers) {
    auto state_handler_manager =
        std::make_unique<StateHandlerManager>(json_store);
    // TODO(gavindodd): Work out how to create RmadState objects and have them
    // scoped to the test.
    for (auto mock_handler : mock_handlers) {
      state_handler_manager->RegisterStateHandler(mock_handler);
    }
    return state_handler_manager;
  }

  std::unique_ptr<StateHandlerManager> CreateStateHandlerManager(
      scoped_refptr<JsonStore> json_store) {
    // TODO(gavindodd): Work out how to create RmadState objects and have them
    // scoped to the test.
    std::vector<scoped_refptr<BaseStateHandler>> mock_handlers;
    mock_handlers.push_back(CreateMockHandler(json_store, welcome_proto_, true,
                                              RMAD_ERROR_OK,
                                              RmadState::kComponentsRepair));
    mock_handlers.push_back(
        CreateMockHandler(json_store, components_repair_proto_, true,
                          RMAD_ERROR_OK, RmadState::kDeviceDestination));
    mock_handlers.push_back(
        CreateMockHandler(json_store, device_destination_proto_, false,
                          RMAD_ERROR_OK, RmadState::kWpDisableMethod));
    return CreateStateHandlerManagerWithHandlers(json_store, mock_handlers);
  }

  std::unique_ptr<StateHandlerManager> CreateStateHandlerManagerMissingHandler(
      scoped_refptr<JsonStore> json_store) {
    // TODO(gavindodd): Work out how to create RmadState objects and have them
    // scoped to the test.
    std::vector<scoped_refptr<BaseStateHandler>> mock_handlers;
    mock_handlers.push_back(CreateMockHandler(json_store, welcome_proto_, true,
                                              RMAD_ERROR_OK,
                                              RmadState::kComponentsRepair));
    return CreateStateHandlerManagerWithHandlers(json_store, mock_handlers);
  }

  std::unique_ptr<StateHandlerManager>
  CreateStateHandlerManagerInitializeStateFail(
      scoped_refptr<JsonStore> json_store) {
    std::vector<scoped_refptr<BaseStateHandler>> mock_handlers;
    mock_handlers.push_back(CreateMockHandler(json_store, welcome_proto_, true,
                                              RMAD_ERROR_MISSING_COMPONENT,
                                              RmadState::kComponentsRepair));
    mock_handlers.push_back(
        CreateMockHandler(json_store, components_repair_proto_, true,
                          RMAD_ERROR_OK, RmadState::kDeviceDestination));
    mock_handlers.push_back(CreateMockHandler(
        json_store, device_destination_proto_, false,
        RMAD_ERROR_DEVICE_INFO_INVALID, RmadState::kWpDisableMethod));
    return CreateStateHandlerManagerWithHandlers(json_store, mock_handlers);
  }

  std::unique_ptr<RuntimeProbeClient> CreateRuntimeProbeClient(
      bool has_cellular) {
    auto mock_runtime_probe_client =
        std::make_unique<NiceMock<MockRuntimeProbeClient>>();
    std::set<RmadComponent> components;
    if (has_cellular) {
      components.insert(RMAD_COMPONENT_CELLULAR);
    }
    ON_CALL(*mock_runtime_probe_client, ProbeCategories(_, _))
        .WillByDefault(DoAll(SetArgPointee<1>(components), Return(true)));
    return mock_runtime_probe_client;
  }

  std::unique_ptr<ShillClient> CreateShillClient(bool* cellular_disabled) {
    auto mock_shill_client = std::make_unique<NiceMock<MockShillClient>>();
    if (cellular_disabled) {
      ON_CALL(*mock_shill_client, DisableCellular())
          .WillByDefault(DoAll(Assign(cellular_disabled, true), Return(true)));
    } else {
      ON_CALL(*mock_shill_client, DisableCellular())
          .WillByDefault(Return(true));
    }
    return mock_shill_client;
  }

  std::unique_ptr<TpmManagerClient> CreateTpmManagerClient(
      RoVerificationStatus ro_verification_status) {
    auto mock_tpm_manager_client =
        std::make_unique<NiceMock<MockTpmManagerClient>>();
    ON_CALL(*mock_tpm_manager_client, GetRoVerificationStatus(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(ro_verification_status), Return(true)));
    return mock_tpm_manager_client;
  }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  RmadState welcome_proto_;
  RmadState components_repair_proto_;
  RmadState device_destination_proto_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(RmadInterfaceImplTest, GetCurrentState_Set_HasCellular) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  bool cellular_disabled = false;
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(true), CreateShillClient(&cellular_disabled),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  EXPECT_TRUE(cellular_disabled);

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_Set_NoCellular) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  bool cellular_disabled = false;
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(&cellular_disabled),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  EXPECT_FALSE(cellular_disabled);

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest,
       GetCurrentState_NotInRma_RoVerificationNotTriggered) {
  base::FilePath json_store_file_path =
      temp_dir_.GetPath().AppendASCII("missing.json");
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  bool cellular_disabled = false;
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(&cellular_disabled),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  EXPECT_FALSE(cellular_disabled);

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_RMA_NOT_REQUIRED, reply.error());
    EXPECT_EQ(RmadState::STATE_NOT_SET, reply.state().state_case());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest,
       GetCurrentState_NotInRma_RoVerificationTriggered) {
  base::FilePath json_store_file_path =
      temp_dir_.GetPath().AppendASCII("missing.json");
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::PASS));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_CorruptedFile) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, "", 0);
  // Make the file read-only.
  base::SetPosixFilePermissions(json_store_file_path, 0444);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_FALSE(rmad_interface.Initialize());
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_EmptyFile) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, "", 0);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_NotSet) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateNotSetJson,
                      std::size(kCurrentStateNotSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_WithHistory) {
  base::FilePath json_store_file_path = CreateInputFile(
      kJsonStoreFileName, kCurrentStateWithRepeatableHistoryJson,
      std::size(kCurrentStateWithRepeatableHistoryJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kComponentsRepair, reply.state().state_case());
    EXPECT_EQ(true, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_WithUnsupportedState) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateWithUnsupportedStateJson,
                      std::size(kCurrentStateWithUnsupportedStateJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kComponentsRepair, reply.state().state_case());
    EXPECT_EQ(true, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  // TODO(gavindodd): Use mock log to check for expected error.
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_InvalidState) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateInvalidStateJson,
                      std::size(kCurrentStateInvalidStateJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_InvalidJson) {
  base::FilePath json_store_file_path = CreateInputFile(
      kJsonStoreFileName, kInvalidJson, std::size(kInvalidJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_InitializeStateFail) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kInitializeCurrentStateFailJson,
                      std::size(kInitializeCurrentStateFailJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManagerInitializeStateFail(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_MISSING_COMPONENT, reply.error());
    EXPECT_EQ(RmadState::STATE_NOT_SET, reply.state().state_case());
  };
  rmad_interface.GetCurrentState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, TransitionNextState) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());
  EXPECT_EQ(true, rmad_interface.CanAbort());

  TransitionNextStateRequest request;
  auto callback1 = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kComponentsRepair, reply.state().state_case());
    EXPECT_EQ(true, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.TransitionNextState(request, base::BindRepeating(callback1));
  EXPECT_EQ(true, rmad_interface.CanAbort());

  auto callback2 = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kDeviceDestination, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(false, reply.can_abort());
  };
  rmad_interface.TransitionNextState(request, base::BindRepeating(callback2));
  EXPECT_EQ(false, rmad_interface.CanAbort());
}

TEST_F(RmadInterfaceImplTest, TransitionNextState_MissingHandler) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManagerMissingHandler(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  TransitionNextStateRequest request;
  auto callback = [](const GetStateReply& reply) {
    FAIL() << "Unexpected call to callback";
  };
  EXPECT_DEATH(rmad_interface.TransitionNextState(
                   request, base::BindRepeating(callback)),
               "No registered state handler");
}

TEST_F(RmadInterfaceImplTest, TransitionNextState_InitializeNextStateFail) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kInitializeNextStateFailJson,
                      std::size(kInitializeNextStateFailJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManagerInitializeStateFail(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  TransitionNextStateRequest request;
  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_DEVICE_INFO_INVALID, reply.error());
    EXPECT_EQ(RmadState::kComponentsRepair, reply.state().state_case());
    EXPECT_EQ(true, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.TransitionNextState(request, base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, TransitionPreviousState) {
  base::FilePath json_store_file_path = CreateInputFile(
      kJsonStoreFileName, kCurrentStateWithRepeatableHistoryJson,
      std::size(kCurrentStateWithRepeatableHistoryJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_OK, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.TransitionPreviousState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, TransitionPreviousState_NoHistory) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_TRANSITION_FAILED, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.TransitionPreviousState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, TransitionPreviousState_MissingHandler) {
  base::FilePath json_store_file_path = CreateInputFile(
      kJsonStoreFileName, kCurrentStateWithRepeatableHistoryJson,
      std::size(kCurrentStateWithRepeatableHistoryJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManagerMissingHandler(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_TRANSITION_FAILED, reply.error());
    EXPECT_EQ(RmadState::kWelcome, reply.state().state_case());
    EXPECT_EQ(false, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.TransitionPreviousState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest,
       TransitionPreviousState_InitializePreviousStateFail) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kInitializePreviousStateFailJson,
                      std::size(kInitializePreviousStateFailJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManagerInitializeStateFail(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  auto callback = [](const GetStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_MISSING_COMPONENT, reply.error());
    EXPECT_EQ(RmadState::kComponentsRepair, reply.state().state_case());
    EXPECT_EQ(true, reply.can_go_back());
    EXPECT_EQ(true, reply.can_abort());
  };
  rmad_interface.TransitionPreviousState(base::BindRepeating(callback));
}

TEST_F(RmadInterfaceImplTest, AbortRma) {
  base::FilePath json_store_file_path = CreateInputFile(
      kJsonStoreFileName, kCurrentStateWithRepeatableHistoryJson,
      std::size(kCurrentStateWithRepeatableHistoryJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(json_store_file_path));

  auto callback = [](const AbortRmaReply& reply) {
    EXPECT_EQ(RMAD_ERROR_RMA_NOT_REQUIRED, reply.error());
  };
  rmad_interface.AbortRma(base::BindRepeating(callback));

  // Check the the state file is cleared.
  EXPECT_FALSE(base::PathExists(json_store_file_path));
}

TEST_F(RmadInterfaceImplTest, AbortRma_NoHistory) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(json_store_file_path));

  auto callback = [](const AbortRmaReply& reply) {
    EXPECT_EQ(RMAD_ERROR_RMA_NOT_REQUIRED, reply.error());
  };
  rmad_interface.AbortRma(base::BindRepeating(callback));

  // Check the the state file is cleared.
  EXPECT_FALSE(base::PathExists(json_store_file_path));
}

TEST_F(RmadInterfaceImplTest, AbortRma_Failed) {
  base::FilePath json_store_file_path = CreateInputFile(
      kJsonStoreFileName, kCurrentStateWithUnrepeatableHistoryJson,
      std::size(kCurrentStateWithUnrepeatableHistoryJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(
      json_store, CreateStateHandlerManager(json_store),
      CreateRuntimeProbeClient(false), CreateShillClient(nullptr),
      CreateTpmManagerClient(RoVerificationStatus::NOT_TRIGGERED));
  EXPECT_TRUE(rmad_interface.Initialize());

  // Check that the state file exists now.
  EXPECT_TRUE(base::PathExists(json_store_file_path));

  auto callback = [](const AbortRmaReply& reply) {
    EXPECT_EQ(RMAD_ERROR_ABORT_FAILED, reply.error());
  };
  rmad_interface.AbortRma(base::BindRepeating(callback));

  // Check the the state file still exists.
  EXPECT_TRUE(base::PathExists(json_store_file_path));
}

}  // namespace rmad
