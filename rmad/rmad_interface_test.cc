// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "rmad/rmad_interface_impl.h"
#include "rmad/utils/json_store.h"

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

 protected:
  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir temp_dir_;
};

// TODO(chenghan): Make RmadInterfaceImpl able to inject state_manager_handler
//                 so we don't depend on actual return values of state handlers.

TEST_F(RmadInterfaceImplTest, GetCurrentState_Set) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateSetJson,
                      std::size(kCurrentStateSetJson) - 1);
  RmadInterfaceImpl rmad_interface(json_store_file_path);

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
  RmadInterfaceImpl rmad_interface(json_store_file_path);

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
  RmadInterfaceImpl rmad_interface(json_store_file_path);

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
  RmadInterfaceImpl rmad_interface(json_store_file_path);

  TransitionStateRequest request;
  auto callback = [](const TransitionStateReply& reply) {
    EXPECT_EQ(RMAD_STATE_UNKNOWN, reply.state());
  };
  rmad_interface.TransitionState(request, base::Bind(callback));
}

TEST_F(RmadInterfaceImplTest, TransitionState_NotSet) {
  base::FilePath json_store_file_path =
      CreateInputFile(kJsonStoreFileName, kCurrentStateNotSetJson,
                      std::size(kCurrentStateNotSetJson) - 1);
  RmadInterfaceImpl rmad_interface(json_store_file_path);

  TransitionStateRequest request;
  auto callback = [](const TransitionStateReply& reply) {
    EXPECT_EQ(RMAD_STATE_RMA_NOT_REQUIRED, reply.state());
  };
  rmad_interface.TransitionState(request, base::Bind(callback));
}

}  // namespace rmad
