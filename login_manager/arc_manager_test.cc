// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_manager.h"

#include <memory>
#include <string>
#include <utility>

#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <base/types/expected.h>
#include <brillo/errors/error.h>
#include <dbus/debugd/dbus-constants.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/dbus_test_util.h"
#include "login_manager/fake_system_utils.h"
#include "login_manager/init_daemon_controller.h"
#include "login_manager/mock_arc_sideload_status.h"
#include "login_manager/mock_init_daemon_controller.h"
#include "login_manager/session_manager_impl.h"

namespace login_manager {

using ::testing::_;
using ::testing::ByMove;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StartsWith;
using ::testing::WithArg;

constexpr char kSaneEmail[] = "user@somewhere.com";

// TODO(crbug.com/390297821): Move out into independent file.
class ArcManagerTest : public testing::Test {
 public:
  void SetUp() override {
    arc_init_controller_ = new MockInitDaemonController();
    arc_sideload_status_ = new MockArcSideloadStatus();
    arc_manager_ = ArcManager::CreateForTesting(
        system_utils_, /*bus=*/nullptr, base::WrapUnique(arc_init_controller_),
        debugd_proxy_.get(),
        std::unique_ptr<ArcSideloadStatusInterface>(arc_sideload_status_));
  }

  void TearDown() override {
    arc_init_controller_ = nullptr;
    arc_sideload_status_ = nullptr;
    arc_manager_.reset();
  }

 protected:
  FakeSystemUtils system_utils_;
  MockInitDaemonController* arc_init_controller_ = nullptr;
  MockArcSideloadStatus* arc_sideload_status_ = nullptr;
  scoped_refptr<dbus::MockObjectProxy> debugd_proxy_ =
      new dbus::MockObjectProxy(nullptr, "", dbus::ObjectPath("/fake/debugd"));
  std::unique_ptr<ArcManager> arc_manager_;
};

TEST_F(ArcManagerTest, OnUserSessionStarted) {
  EXPECT_CALL(*debugd_proxy_, CallMethodAndBlock(_, _))
      .WillOnce(WithArg<0>(Invoke([](dbus::MethodCall* method_call) {
        EXPECT_EQ(method_call->GetInterface(), debugd::kDebugdInterface);
        EXPECT_EQ(method_call->GetMember(), debugd::kDeleteArcBugReportBackup);
        return base::ok(dbus::Response::CreateEmpty());
      })));
  arc_manager_->OnUserSessionStarted(kSaneEmail);
}

TEST_F(ArcManagerTest, EmitStopArcVmInstanceImpulse) {
  EXPECT_CALL(
      *arc_init_controller_,
      TriggerImpulse(ArcManager::kStopArcVmInstanceImpulse, ElementsAre(),
                     InitDaemonController::TriggerMode::SYNC))
      .WillOnce(Return(ByMove(dbus::Response::CreateEmpty())));
  arc_manager_->EmitStopArcVmInstanceImpulse();
}

TEST_F(ArcManagerTest, EmitArcBooted) {
#if USE_CHEETS
  {
    EXPECT_CALL(*arc_init_controller_,
                TriggerImpulse(ArcManager::kArcBootedImpulse,
                               ElementsAre(StartsWith("CHROMEOS_USER=")),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(ByMove(nullptr)));
    brillo::ErrorPtr error;
    EXPECT_TRUE(arc_manager_->EmitArcBooted(&error, kSaneEmail));
    EXPECT_FALSE(error.get());
    testing::Mock::VerifyAndClearExpectations(arc_init_controller_);
  }

  {
    EXPECT_CALL(*arc_init_controller_,
                TriggerImpulse(ArcManager::kArcBootedImpulse, ElementsAre(),
                               InitDaemonController::TriggerMode::ASYNC))
        .WillOnce(Return(ByMove(nullptr)));
    brillo::ErrorPtr error;
    EXPECT_TRUE(arc_manager_->EmitArcBooted(&error, std::string()));
    EXPECT_FALSE(error.get());
    testing::Mock::VerifyAndClearExpectations(arc_init_controller_);
  }
#else
  brillo::ErrorPtr error;
  EXPECT_FALSE(arc_manager_->EmitArcBooted(&error, kSaneEmail));
  ASSERT_TRUE(error.get());
  EXPECT_EQ(dbus_error::kNotAvailable, error->GetCode());
#endif
}

TEST_F(ArcManagerTest, EnableAdbSideload) {
  EXPECT_CALL(*arc_sideload_status_, EnableAdbSideload(_));
  ResponseCapturer capturer;
  arc_manager_->EnableAdbSideload(capturer.CreateMethodResponse<bool>());
}

TEST_F(ArcManagerTest, EnableAdbSideloadAfterLoggedIn) {
  base::FilePath logged_in_path(SessionManagerImpl::kLoggedInFlag);
  ASSERT_FALSE(system_utils_.Exists(logged_in_path));
  ASSERT_TRUE(system_utils_.WriteStringToFile(logged_in_path, "1"));

  EXPECT_CALL(*arc_sideload_status_, EnableAdbSideload(_)).Times(0);

  ResponseCapturer capturer;
  arc_manager_->EnableAdbSideload(capturer.CreateMethodResponse<bool>());

  ASSERT_NE(capturer.response(), nullptr);
  EXPECT_EQ(dbus_error::kSessionExists, capturer.response()->GetErrorName());
}

TEST_F(ArcManagerTest, QueryAdbSideload) {
  EXPECT_CALL(*arc_sideload_status_, QueryAdbSideload(_));
  ResponseCapturer capturer;
  arc_manager_->QueryAdbSideload(capturer.CreateMethodResponse<bool>());
}

}  // namespace login_manager
