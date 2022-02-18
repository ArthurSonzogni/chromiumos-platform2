// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <memory>
#include <utility>

#include <base/dcheck_is_on.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/single_thread_task_runner.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "featured/feature_library.h"
#include "featured/service.h"

namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

}  // namespace

namespace feature {

class FeatureLibraryTest : public testing::Test {
 protected:
  FeatureLibraryTest()
      : mock_bus_(new dbus::MockBus{dbus::Bus::Options{}}),
        mock_proxy_(new dbus::MockObjectProxy(
            mock_bus_.get(),
            chromeos::kChromeFeaturesServiceName,
            dbus::ObjectPath(chromeos::kChromeFeaturesServicePath))) {}

  ~FeatureLibraryTest() { mock_bus_->ShutdownAndBlock(); }

  void SetUp() override {
    features_ = std::unique_ptr<PlatformFeatures>(
        new PlatformFeatures(mock_bus_, mock_proxy_.get()));
  }

  std::unique_ptr<dbus::Response> CreateResponse(dbus::MethodCall* call,
                                                 bool enabled) {
    if (call->GetInterface() == "org.chromium.ChromeFeaturesServiceInterface" &&
        call->GetMember() == "IsFeatureEnabled") {
      std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
      dbus::MessageWriter writer(response.get());
      writer.AppendBool(enabled);
      return response;
    }
    LOG(ERROR) << "Unexpected method call " << call->ToString();
    return nullptr;
  }

  base::test::SingleThreadTaskEnvironment task_environment_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  std::unique_ptr<PlatformFeatures> features_;
  std::unique_ptr<base::RunLoop> run_loop_;
};

// Parameterized tests, with a boolean indicating whether the feature should be
// enabled.
class FeatureLibraryParameterizedTest
    : public FeatureLibraryTest,
      public ::testing::WithParamInterface<bool> {};

INSTANTIATE_TEST_SUITE_P(FeatureLibraryParameterizedTest,
                         FeatureLibraryParameterizedTest,
                         testing::Values(true, false));

TEST_P(FeatureLibraryParameterizedTest, IsEnabled_Success) {
  bool enabled = GetParam();

  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke([this, enabled](
                           dbus::MethodCall* call, int timeout_ms,
                           dbus::MockObjectProxy::ResponseCallback* callback) {
        std::unique_ptr<dbus::Response> resp = CreateResponse(call, enabled);
        std::move(*callback).Run(resp.get());
      }));

  run_loop_ = std::make_unique<base::RunLoop>();

  Feature f{"Feature", FEATURE_DISABLED_BY_DEFAULT};
  features_->IsEnabled(f,
                       base::BindLambdaForTesting([this, enabled](bool actual) {
                         EXPECT_EQ(enabled, actual);
                         run_loop_->Quit();
                       }));

  run_loop_->Run();
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabled_Failure_WaitForService) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(false); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .Times(0);

  run_loop_ = std::make_unique<base::RunLoop>();

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  Feature f{"Feature", feature_state};

  features_->IsEnabled(f,
                       base::BindLambdaForTesting([this, enabled](bool actual) {
                         EXPECT_EQ(enabled, actual);
                         run_loop_->Quit();
                       }));
  run_loop_->Run();
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabled_Failure_NullResponse) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke([](dbus::MethodCall* call, int timeout_ms,
                          dbus::MockObjectProxy::ResponseCallback* callback) {
        std::move(*callback).Run(nullptr);
      }));

  run_loop_ = std::make_unique<base::RunLoop>();

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  Feature f{"Feature", feature_state};

  features_->IsEnabled(f,
                       base::BindLambdaForTesting([this, enabled](bool actual) {
                         EXPECT_EQ(enabled, actual);
                         run_loop_->Quit();
                       }));
  run_loop_->Run();
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabled_Failure_EmptyResponse) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(true); }));

  std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .WillOnce(Invoke(
          [&response](dbus::MethodCall* call, int timeout_ms,
                      dbus::MockObjectProxy::ResponseCallback* callback) {
            std::move(*callback).Run(response.get());
          }));

  run_loop_ = std::make_unique<base::RunLoop>();

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  Feature f{"Feature", feature_state};

  features_->IsEnabled(f,
                       base::BindLambdaForTesting([this, enabled](bool actual) {
                         EXPECT_EQ(enabled, actual);
                         run_loop_->Quit();
                       }));

  run_loop_->Run();
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabledBlocking_Success) {
  bool enabled = GetParam();

  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke([this, enabled](dbus::MethodCall* call, int timeout_ms) {
        return CreateResponse(call, enabled);
      }));

  Feature f{"Feature", FEATURE_DISABLED_BY_DEFAULT};
  EXPECT_EQ(enabled, features_->IsEnabledBlocking(f));
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabledBlocking_Failure_Null) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke(
          [](dbus::MethodCall* call, int timeout_ms) { return nullptr; }));

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  Feature f{"Feature", feature_state};

  EXPECT_EQ(enabled, features_->IsEnabledBlocking(f));
}

TEST_P(FeatureLibraryParameterizedTest, IsEnabledBlocking_Failure_Empty) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .WillOnce(Invoke([](dbus::MethodCall* call, int timeout_ms) {
        return dbus::Response::CreateEmpty();
      }));

  bool enabled = GetParam();
  FeatureState feature_state =
      GetParam() ? FEATURE_ENABLED_BY_DEFAULT : FEATURE_DISABLED_BY_DEFAULT;
  Feature f{"Feature", feature_state};

  EXPECT_EQ(enabled, features_->IsEnabledBlocking(f));
}

TEST_F(FeatureLibraryTest, CheckFeatureIdentity) {
  Feature f1{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  // A new, unseen feature should pass the check.
  EXPECT_TRUE(features_->CheckFeatureIdentity(f1));
  // As should a feature seen a second time.
  EXPECT_TRUE(features_->CheckFeatureIdentity(f1));

  Feature f2{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  // A separate feature with the same name should fail.
  EXPECT_FALSE(features_->CheckFeatureIdentity(f2));

  Feature f3{"Feature3", FEATURE_ENABLED_BY_DEFAULT};
  // A distinct feature with a distinct name should pass.
  EXPECT_TRUE(features_->CheckFeatureIdentity(f3));
  EXPECT_TRUE(features_->CheckFeatureIdentity(f3));
}

#if DCHECK_IS_ON()
using FeatureLibraryDeathTest = FeatureLibraryTest;
TEST_F(FeatureLibraryDeathTest, IsEnabledDistinctFeatureDefs) {
  EXPECT_CALL(*mock_proxy_, DoWaitForServiceToBeAvailable(_))
      .WillOnce(
          Invoke([](dbus::MockObjectProxy::WaitForServiceToBeAvailableCallback*
                        callback) { std::move(*callback).Run(false); }));

  EXPECT_CALL(*mock_proxy_,
              DoCallMethod(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, _))
      .Times(0);

  run_loop_ = std::make_unique<base::RunLoop>();

  Feature f{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  features_->IsEnabled(f, base::BindLambdaForTesting([this](bool enabled) {
                         EXPECT_TRUE(enabled);  // Default value
                         run_loop_->Quit();
                       }));
  run_loop_->Run();

  Feature f2{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  EXPECT_DEATH(
      features_->IsEnabled(f2, base::BindLambdaForTesting([this](bool enabled) {
                             EXPECT_TRUE(enabled);  // Default value
                             run_loop_->Quit();
                           })),
      "Feature");
}

TEST_F(FeatureLibraryDeathTest, IsEnabledBlockingDistinctFeatureDefs) {
  EXPECT_CALL(*mock_proxy_,
              CallMethodAndBlock(_, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT))
      .Times(1);

  Feature f{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  features_->IsEnabledBlocking(f);

  Feature f2{"Feature", FEATURE_ENABLED_BY_DEFAULT};
  EXPECT_DEATH(features_->IsEnabledBlocking(f2), "Feature");
}
#endif  // DCHECK_IS_ON()

class FeatureLibraryCmdTest : public testing::Test {
 public:
  FeatureLibraryCmdTest() {}
  ~FeatureLibraryCmdTest() {}
};

TEST_F(FeatureLibraryCmdTest, MkdirTest) {
  if (base::PathExists(
          base::FilePath("/sys/kernel/debug/tracing/instances/"))) {
    const std::string sys_path = "/sys/kernel/debug/tracing/instances/unittest";
    EXPECT_FALSE(base::PathExists(base::FilePath(sys_path)));
    EXPECT_TRUE(featured::MkdirCommand(sys_path).Execute());
    EXPECT_TRUE(base::PathExists(base::FilePath(sys_path)));
    EXPECT_TRUE(base::DeleteFile(base::FilePath(sys_path)));
    EXPECT_FALSE(base::PathExists(base::FilePath(sys_path)));
  }

  if (base::PathExists(base::FilePath("/mnt"))) {
    const std::string mnt_path = "/mnt/notallowed";
    EXPECT_FALSE(base::PathExists(base::FilePath(mnt_path)));
    EXPECT_FALSE(featured::MkdirCommand(mnt_path).Execute());
    EXPECT_FALSE(base::PathExists(base::FilePath(mnt_path)));
  }
}

}  // namespace feature
