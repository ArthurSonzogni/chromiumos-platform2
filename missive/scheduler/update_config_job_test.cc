// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/scheduler/update_config_job.h"

#include <memory>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/memory/scoped_refptr.h>
#include <base/run_loop.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <base/test/task_environment.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/health/health_module.h"
#include "missive/health/health_module_delegate_mock.h"
#include "missive/proto/interface.pb.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/test_support_callbacks.h"
#include "missive/util/test_util.h"

namespace reporting {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::NotNull;
using ::testing::StrEq;
using ::testing::WithArgs;

class MockServerConfigurationController : public ServerConfigurationController {
 public:
  explicit MockServerConfigurationController(bool is_enabled)
      : ServerConfigurationController(is_enabled) {}

  MOCK_METHOD(void,
              UpdateConfiguration,
              (ListOfBlockedDestinations list, HealthModule::Recorder recorder),
              (override));
};

class UpdateConfigInMissiveJobTest : public ::testing::Test {
 public:
  UpdateConfigInMissiveJobTest() = default;

 protected:
  void SetUp() override {
    response_ = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
        UpdateConfigInMissiveResponse>>();

    health_module_ =
        HealthModule::Create(std::make_unique<HealthModuleDelegateMock>());

    list_destinations_.add_destinations(Destination::CRD_EVENTS);
    list_destinations_.add_destinations(Destination::KIOSK_HEARTBEAT_EVENTS);

    server_configuration_controller_ =
        base::MakeRefCounted<MockServerConfigurationController>(
            /*is_enabled=*/true);
  }

  void TearDown() override {
    // Let everything ongoing to finish.
    task_environment_.RunUntilIdle();
  }

  base::test::TaskEnvironment task_environment_;

  std::unique_ptr<
      brillo::dbus_utils::MockDBusMethodResponse<UpdateConfigInMissiveResponse>>
      response_;

  scoped_refptr<HealthModule> health_module_;

  ListOfBlockedDestinations list_destinations_;

  scoped_refptr<MockServerConfigurationController>
      server_configuration_controller_;
};

TEST_F(UpdateConfigInMissiveJobTest, CompletesSuccessfully) {
  response_->set_return_callback(
      base::BindOnce([](const UpdateConfigInMissiveResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(error::OK));
      }));
  auto delegate = std::make_unique<
      UpdateConfigInMissiveJob::UpdateConfigInMissiveResponseDelegate>(
      std::move(response_));

  UpdateConfigInMissiveRequest request;
  *request.mutable_list_of_blocked_destinations() = list_destinations_;

  EXPECT_CALL(*server_configuration_controller_,
              UpdateConfiguration(EqualsProto(list_destinations_), _))
      .Times(1);

  auto job = UpdateConfigInMissiveJob::Create(health_module_,
                                              server_configuration_controller_,
                                              request, std::move(delegate));

  test::TestEvent<Status> enqueued;
  job->Start(enqueued.cb());
  const Status status = enqueued.result();
  EXPECT_OK(status) << status;
}

TEST_F(UpdateConfigInMissiveJobTest, CancelsSuccessfully) {
  Status failure_status(error::INTERNAL, "Failing for tests");
  response_->set_return_callback(base::BindOnce(
      [](Status failure_status, const UpdateConfigInMissiveResponse& response) {
        EXPECT_THAT(response.status().code(), Eq(failure_status.error_code()));
        EXPECT_THAT(response.status().error_message(),
                    StrEq(std::string(failure_status.error_message())));
      },
      failure_status));
  auto delegate = std::make_unique<
      UpdateConfigInMissiveJob::UpdateConfigInMissiveResponseDelegate>(
      std::move(response_));

  UpdateConfigInMissiveRequest request;
  *request.mutable_list_of_blocked_destinations() = list_destinations_;

  EXPECT_CALL(*server_configuration_controller_, UpdateConfiguration(_, _))
      .Times(0);

  auto job = UpdateConfigInMissiveJob::Create(health_module_,
                                              server_configuration_controller_,
                                              request, std::move(delegate));

  job->Cancel(failure_status);
}

}  // namespace
}  // namespace reporting
