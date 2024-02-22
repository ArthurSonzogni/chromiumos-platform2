// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tuple>
#include <utility>
#include <vector>

#include <base/test/simple_test_clock.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <update_engine/proto_bindings/update_engine.pb.h>

#include "dlcservice/installer.h"
#include "dlcservice/system_state.h"
#include "dlcservice/test_utils.h"

using testing::_;
using testing::Return;
using testing::WithArg;

namespace dlcservice {

class InstallerTest : public BaseTest {
 public:
  InstallerTest() = default;
  InstallerTest(const InstallerTest&) = delete;
  InstallerTest& operator=(const InstallerTest&) = delete;

 protected:
  Installer installer_;
};

TEST_F(InstallerTest, InstallTest) {
  EXPECT_FALSE(loop_.PendingTasks());
  installer_.Install({}, {}, {});
  EXPECT_TRUE(loop_.PendingTasks());
}

TEST_F(InstallerTest, OnReadyTest) {
  EXPECT_FALSE(loop_.PendingTasks());

  bool called = false;
  installer_.OnReady(
      base::BindOnce([](bool* bptr, bool) { *bptr = true; }, &called));

  EXPECT_TRUE(loop_.PendingTasks());
  loop_.RunOnce(/*may_block=*/false);
  EXPECT_FALSE(loop_.PendingTasks());
  EXPECT_TRUE(called);
}

TEST_F(InstallerTest, StatusSyncTest) {
  class ObserverTest : public InstallerInterface::Observer {
   public:
    void OnStatusSync(const InstallerInterface::Status& status) override {
      called = true;
    }
    bool called = false;
  } instance;
  installer_.AddObserver(&instance);
  EXPECT_FALSE(instance.called);
  installer_.StatusSync();
  EXPECT_TRUE(instance.called);
}

class UpdateEngineInstallerTest : public BaseTest {
 public:
  UpdateEngineInstallerTest() = default;
  UpdateEngineInstallerTest(const UpdateEngineInstallerTest&) = delete;
  UpdateEngineInstallerTest& operator=(const UpdateEngineInstallerTest&) =
      delete;

 protected:
  UpdateEngineInstaller ue_installer_;
};

template <typename T>
class UpdateEngineInstallerWithParamsTest
    : public ::testing::WithParamInterface<T>,
      public UpdateEngineInstallerTest {
 public:
  UpdateEngineInstallerWithParamsTest() = default;
  UpdateEngineInstallerWithParamsTest(
      const UpdateEngineInstallerWithParamsTest&) = delete;
  UpdateEngineInstallerWithParamsTest& operator=(
      const UpdateEngineInstallerWithParamsTest&) = delete;
};

TEST_F(UpdateEngineInstallerTest, InitTest) {
  EXPECT_CALL(*mock_update_engine_proxy_ptr_,
              DoRegisterStatusUpdateAdvancedSignalHandler(_, _))
      .Times(1);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetObjectProxy())
      .WillOnce(Return(mock_update_engine_object_proxy_.get()));
  EXPECT_CALL(*mock_update_engine_object_proxy_,
              DoWaitForServiceToBeAvailable(_))
      .Times(1);

  EXPECT_TRUE(ue_installer_.Init());

  const auto& status = SystemState::Get()->installer_status();
  EXPECT_EQ(status.state, InstallerInterface::Status::State::OK);
  EXPECT_FALSE(status.is_install);
  EXPECT_EQ(status.progress, 0.);
}

TEST_F(UpdateEngineInstallerTest, InstallTest) {
  struct {
    const std::string kId = "foo-id", kUrl = "foo-url";
    const bool kScaled = false, kForceOta = true;
  } kStack;
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, InstallAsync(_, _, _, _))
      .WillOnce(WithArg<0>([&kStack](const auto& ip) {
        EXPECT_EQ(ip.id(), kStack.kId);
        EXPECT_EQ(ip.omaha_url(), kStack.kUrl);
        EXPECT_EQ(ip.scaled(), kStack.kScaled);
        EXPECT_EQ(ip.force_ota(), kStack.kForceOta);
      }));
  ue_installer_.Install(
      InstallerInterface::InstallArgs{
          .id = kStack.kId,
          .url = kStack.kUrl,
          .scaled = kStack.kScaled,
          .force_ota = kStack.kForceOta,
      },
      InstallerInterface::InstallSuccessCallback{},
      InstallerInterface::InstallFailureCallback{});
}

TEST_F(UpdateEngineInstallerTest, IsReadyTest) {
  // Default check.
  EXPECT_FALSE(ue_installer_.IsReady());
}

using UpdateEngineInstallerWithBoolParamsTest =
    UpdateEngineInstallerWithParamsTest<bool>;

INSTANTIATE_TEST_SUITE_P(VaryingBoolean,
                         UpdateEngineInstallerWithBoolParamsTest,
                         ::testing::Bool());

TEST_P(UpdateEngineInstallerWithBoolParamsTest, OnReadyTest) {
  bool called = false, expected_available = GetParam();
  auto callback = base::BindOnce(
      [](bool expected_available, bool* called, bool available) {
        *called = true;
        EXPECT_EQ(available, expected_available);
      },
      expected_available, &called);
  ue_installer_.OnReady(std::move(callback));
  EXPECT_FALSE(called);
  EXPECT_FALSE(loop_.PendingTasks());
  ue_installer_.OnWaitForUpdateEngineServiceToBeAvailable(
      /*available=*/expected_available);
  EXPECT_TRUE(loop_.PendingTasks() && loop_.RunOnce(/*may_block=*/false));
  EXPECT_TRUE(called);
}

using PseudoStatusResult = std::tuple<update_engine::Operation, bool, double>;
using ExpectedResult = std::tuple<InstallerInterface::Status>;
using UpdateEngineInstallerWithStatusParamsTest =
    UpdateEngineInstallerWithParamsTest<
        std::tuple<PseudoStatusResult, ExpectedResult>>;

INSTANTIATE_TEST_SUITE_P(
    VaryingStatus,
    UpdateEngineInstallerWithStatusParamsTest,
    ::testing::ValuesIn(std::vector<
                        UpdateEngineInstallerWithStatusParamsTest::ParamType>{
        {
            {
                {
                    update_engine::Operation::IDLE,
                    false,
                    0.,
                },
                {
                    InstallerInterface::Status{
                        .state = InstallerInterface::Status::State::OK,
                        .is_install = false,
                        .progress = 0.,
                    },
                },
            },
            {
                {
                    update_engine::Operation::CHECKING_FOR_UPDATE,
                    false,
                    0.,
                },
                {
                    InstallerInterface::Status{
                        .state = InstallerInterface::Status::State::CHECKING,
                        .is_install = false,
                        .progress = 0.,
                    },
                },
            },
            {
                {
                    update_engine::Operation::DOWNLOADING,
                    false,
                    0.,
                },
                {
                    InstallerInterface::Status{
                        .state = InstallerInterface::Status::State::DOWNLOADING,
                        .is_install = false,
                        .progress = 0.,
                    },
                },
            },
            {
                {
                    update_engine::Operation::DOWNLOADING,
                    true,
                    0.8,
                },
                {
                    InstallerInterface::Status{
                        .state = InstallerInterface::Status::State::DOWNLOADING,
                        .is_install = true,
                        .progress = 0.8,
                    },
                },
            },
            {
                {
                    update_engine::Operation::VERIFYING,
                    false,
                    0.,
                },
                {
                    InstallerInterface::Status{
                        .state = InstallerInterface::Status::State::VERIFYING,
                        .is_install = false,
                        .progress = 0.,
                    },
                },
            },
            {
                {
                    update_engine::Operation::REPORTING_ERROR_EVENT,
                    false,
                    0.,
                },
                {
                    InstallerInterface::Status{
                        .state = InstallerInterface::Status::State::ERROR,
                        .is_install = false,
                        .progress = 0.,
                    },
                },
            },
            {
                {
                    update_engine::Operation::UPDATED_NEED_REBOOT,
                    false,
                    0.,
                },
                {
                    InstallerInterface::Status{
                        .state = InstallerInterface::Status::State::BLOCKED,
                        .is_install = false,
                        .progress = 0.,
                    },
                },
            },
        },
    }));

TEST_P(UpdateEngineInstallerWithStatusParamsTest, StatusSyncTest) {
  const auto& [pseudo_status_result, expected_result] = GetParam();
  const auto& expected_status = std::get<0>(expected_result);
  class ObserverTest : public InstallerInterface::Observer {
   public:
    void OnStatusSync(const InstallerInterface::Status& status) override {
      status_ = status;
    }
    InstallerInterface::Status status_;
  } instance;
  ue_installer_.AddObserver(&instance);

  update_engine::StatusResult status_result;
  const auto& [op, is_install, progress] = pseudo_status_result;
  status_result.set_current_operation(op);
  status_result.set_is_install(is_install);
  status_result.set_progress(progress);
  EXPECT_CALL(*mock_update_engine_proxy_ptr_, GetStatusAdvancedAsync(_, _, _))
      .WillOnce(WithArg<0>([&status_result](auto&& success_callback) {
        std::move(success_callback).Run(status_result);
      }));

  ue_installer_.StatusSync();
  EXPECT_EQ(instance.status_.state, expected_status.state);
  EXPECT_EQ(instance.status_.is_install, expected_status.is_install);
  EXPECT_EQ(instance.status_.progress, expected_status.progress);
}

}  // namespace dlcservice
