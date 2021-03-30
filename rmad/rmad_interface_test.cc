// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/rmad_interface_impl.h"
#include "rmad/state_handler/mock_state_handler.h"
#include "rmad/state_handler/state_handler_manager.h"
#include "rmad/utils/json_store.h"

using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SetArgPointee;

namespace rmad {

constexpr char kJsonStoreFileName[] = "json_store_file";
constexpr char kCurrentStateSetJson[] =
    R"({"current_state": "RMAD_STATE_WELCOME_SCREEN"})";
constexpr char kCurrentStateNotSetJson[] = "{}";
constexpr char kCurrentStateInvalidStateJson[] = R"("current_state": "abc")";

class RmadInterfaceImplTest : public testing::Test {
 public:
  RmadInterfaceImplTest() = default;

  base::FilePath CreateInputFile(std::string filename,
                                 const char* str,
                                 int size) {
    base::FilePath file_path = temp_dir_.GetPath().AppendASCII(filename);
    base::WriteFile(file_path, str, size);
    return file_path;
  }

  scoped_refptr<BaseStateHandler> CreateMockHandler(
      scoped_refptr<JsonStore> json_store,
      RmadState state,
      bool is_allow_abort,
      RmadState next_state,
      bool next_state_retval) {
    auto mock_handler =
        base::MakeRefCounted<NiceMock<MockStateHandler>>(json_store);
    ON_CALL(*mock_handler, GetState()).WillByDefault(Return(state));
    ON_CALL(*mock_handler, IsAllowAbort())
        .WillByDefault(Return(is_allow_abort));
    ON_CALL(*mock_handler, GetNextState(_))
        .WillByDefault(
            DoAll(SetArgPointee<0>(next_state), Return(next_state_retval)));
    return mock_handler;
  }

  std::unique_ptr<StateHandlerManager> CreateStateHandlerManager(
      scoped_refptr<JsonStore> json_store) {
    auto state_handler_manager =
        std::make_unique<StateHandlerManager>(json_store);
    state_handler_manager->RegisterStateHandler(
        CreateMockHandler(json_store, RMAD_STATE_WELCOME_SCREEN, false,
                          RMAD_STATE_COMPONENT_SELECTION, true));
    state_handler_manager->RegisterStateHandler(
        CreateMockHandler(json_store, RMAD_STATE_COMPONENT_SELECTION, true,
                          RMAD_STATE_DESTINATION_SELECTION, false));
    return state_handler_manager;
  }

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

TEST_F(RmadInterfaceImplTest, GetCurrentState_Set) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(json_store,
                                   CreateStateHandlerManager(json_store));

  GetCurrentStateRequest request;
  auto callback = [](const GetCurrentStateReply& reply) {
    EXPECT_EQ(RMAD_STATE_WELCOME_SCREEN, reply.state());
  };
  rmad_interface.GetCurrentState(request, base::Bind(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_NotSet) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateNotSetJson,
                      std::size(kCurrentStateNotSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(json_store,
                                   CreateStateHandlerManager(json_store));

  GetCurrentStateRequest request;
  auto callback = [](const GetCurrentStateReply& reply) {
    EXPECT_EQ(RMAD_STATE_RMA_NOT_REQUIRED, reply.state());
  };
  rmad_interface.GetCurrentState(request, base::Bind(callback));
}

TEST_F(RmadInterfaceImplTest, GetCurrentState_InvalidState) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateInvalidStateJson,
                      std::size(kCurrentStateInvalidStateJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(json_store,
                                   CreateStateHandlerManager(json_store));

  GetCurrentStateRequest request;
  auto callback = [](const GetCurrentStateReply& reply) {
    EXPECT_EQ(RMAD_STATE_RMA_NOT_REQUIRED, reply.state());
  };
  rmad_interface.GetCurrentState(request, base::Bind(callback));
}

TEST_F(RmadInterfaceImplTest, TransitionState) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(json_store,
                                   CreateStateHandlerManager(json_store));

  TransitionStateRequest request;
  // First transition:
  // RMAD_STATE_WELCOME_SCREEN -> RMAD_STATE_COMPONENT_SELECTION.
  auto callback1 = [](const TransitionStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_NOT_SET, reply.error());
    EXPECT_EQ(RMAD_STATE_COMPONENT_SELECTION, reply.state());
  };
  rmad_interface.TransitionState(request, base::Bind(callback1));
  // Second transition:
  // RMAD_STATE_COMPONENT_SELECTION -> transition fail.
  auto callback2 = [](const TransitionStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_TRANSITION_FAILED, reply.error());
    EXPECT_EQ(RMAD_STATE_COMPONENT_SELECTION, reply.state());
  };
  rmad_interface.TransitionState(request, base::Bind(callback2));
}

TEST_F(RmadInterfaceImplTest, TransitionState_NotSet) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateNotSetJson,
                      std::size(kCurrentStateNotSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(json_store,
                                   CreateStateHandlerManager(json_store));

  TransitionStateRequest request;
  auto callback = [](const TransitionStateReply& reply) {
    EXPECT_EQ(RMAD_ERROR_TRANSITION_FAILED, reply.error());
    EXPECT_EQ(RMAD_STATE_RMA_NOT_REQUIRED, reply.state());
  };
  rmad_interface.TransitionState(request, base::Bind(callback));
}

TEST_F(RmadInterfaceImplTest, AbortRma) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  auto json_store = base::MakeRefCounted<JsonStore>(json_store_file_path);
  RmadInterfaceImpl rmad_interface(json_store,
                                   CreateStateHandlerManager(json_store));

  AbortRmaRequest request;
  // RMAD_STATE_WELCOME_SCREEN doesn't allow abort.
  auto callback1 = [](const AbortRmaReply& reply) {
    EXPECT_EQ(RMAD_ERROR_ABORT_FAILED, reply.error());
  };
  rmad_interface.AbortRma(request, base::Bind(callback1));
  // Do a state transition.
  TransitionStateRequest transition_request;
  auto transition_callback = [](const TransitionStateReply& reply) {};
  rmad_interface.TransitionState(transition_request,
                                 base::Bind(transition_callback));
  // RMAD_STATE_UNKNOWN allows abort.
  auto callback2 = [](const AbortRmaReply& reply) {
    EXPECT_EQ(RMAD_ERROR_NOT_SET, reply.error());
  };
  rmad_interface.AbortRma(request, base::Bind(callback2));
}

}  // namespace rmad
