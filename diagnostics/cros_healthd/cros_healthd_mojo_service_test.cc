// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "debugd/dbus-proxy-mocks.h"
#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "mojo/cros_healthd.mojom.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

constexpr uint32_t kExpectedId = 123;
constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
    mojo_ipc::DiagnosticRoutineStatusEnum::kReady;

// Saves |response| to |response_destination|.
template <class T>
void SaveMojoResponse(T* response_destination, T response) {
  *response_destination = std::move(response);
}

class MockCrosHealthdRoutineService : public CrosHealthdRoutineService {
 public:
  MOCK_METHOD0(GetAvailableRoutines,
               std::vector<mojo_ipc::DiagnosticRoutineEnum>());
  MOCK_METHOD4(RunBatteryCapacityRoutine,
               void(uint32_t low_mah,
                    uint32_t high_mah,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD4(RunBatteryHealthRoutine,
               void(uint32_t maximum_cycle_count,
                    uint32_t percent_battery_wear_allowed,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD3(RunUrandomRoutine,
               void(uint32_t length_seconds,
                    int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD2(RunSmartctlCheckRoutine,
               void(int32_t* id,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status));
  MOCK_METHOD4(GetRoutineUpdate,
               void(int32_t uuid,
                    mojo_ipc::DiagnosticRoutineCommandEnum command,
                    bool include_output,
                    mojo_ipc::RoutineUpdate* response));
};

}  // namespace

// Tests for the CrosHealthddMojoService class.
class CrosHealthdMojoServiceTest : public testing::Test {
 protected:
  CrosHealthdMojoServiceTest() {
    mojo::core::Init();
    mock_debugd_proxy_ = std::make_unique<org::chromium::debugdProxyMock>();
    options_.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new dbus::MockBus(options_);
    mock_power_manager_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), power_manager::kPowerManagerServiceName,
        dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    battery_fetcher_ = std::make_unique<BatteryFetcher>(
        mock_debugd_proxy_.get(), mock_power_manager_proxy_.get());
    service_ = std::make_unique<CrosHealthdMojoService>(battery_fetcher_.get(),
                                                        &routine_service_);
  }

  CrosHealthdMojoService* service() { return service_.get(); }

  MockCrosHealthdRoutineService* routine_service() { return &routine_service_; }

 private:
  base::MessageLoop message_loop_;
  StrictMock<MockCrosHealthdRoutineService> routine_service_;
  mojo_ipc::CrosHealthdServicePtr service_ptr_;
  std::unique_ptr<org::chromium::debugdProxyMock> mock_debugd_proxy_;
  dbus::Bus::Options options_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_power_manager_proxy_;
  std::unique_ptr<BatteryFetcher> battery_fetcher_;
  std::unique_ptr<CrosHealthdMojoService> service_;
};

// Test that we can request the battery capacity routine.
TEST_F(CrosHealthdMojoServiceTest, RequestBatteryCapacityRoutine) {
  constexpr uint32_t low_mah = 10;
  constexpr uint32_t high_mah = 100;

  EXPECT_CALL(*routine_service(),
              RunBatteryCapacityRoutine(low_mah, high_mah, _, _))
      .WillOnce(WithArgs<2, 3>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunBatteryCapacityRoutine(
      low_mah, high_mah,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the battery health routine.
TEST_F(CrosHealthdMojoServiceTest, RequestBatteryHealthRoutine) {
  constexpr uint32_t maximum_cycle_count = 44;
  constexpr uint32_t percent_battery_wear_allowed = 13;

  EXPECT_CALL(*routine_service(),
              RunBatteryHealthRoutine(maximum_cycle_count,
                                      percent_battery_wear_allowed, _, _))
      .WillOnce(WithArgs<2, 3>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunBatteryHealthRoutine(
      maximum_cycle_count, percent_battery_wear_allowed,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the urandom routine.
TEST_F(CrosHealthdMojoServiceTest, RequestUrandomRoutine) {
  constexpr uint32_t length_seconds = 22;

  EXPECT_CALL(*routine_service(), RunUrandomRoutine(length_seconds, _, _))
      .WillOnce(WithArgs<1, 2>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunUrandomRoutine(
      length_seconds,
      base::Bind(&SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>,
                 &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that we can request the smartctl-check routine.
TEST_F(CrosHealthdMojoServiceTest, RequestSmartctlCheckRoutine) {
  EXPECT_CALL(*routine_service(), RunSmartctlCheckRoutine(_, _))
      .WillOnce(WithArgs<0, 1>(Invoke(
          [](int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
            *id = kExpectedId;
            *status = kExpectedStatus;
          })));

  mojo_ipc::RunRoutineResponsePtr response;
  service()->RunSmartctlCheckRoutine(base::Bind(
      &SaveMojoResponse<mojo_ipc::RunRoutineResponsePtr>, &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->id, kExpectedId);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test an update request.
TEST_F(CrosHealthdMojoServiceTest, RequestRoutineUpdate) {
  constexpr int kId = 3;
  constexpr mojo_ipc::DiagnosticRoutineCommandEnum kCommand =
      mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus;
  constexpr bool kIncludeOutput = true;
  constexpr int kFakeProgressPercent = 13;

  EXPECT_CALL(*routine_service(),
              GetRoutineUpdate(kId, kCommand, kIncludeOutput, _))
      .WillOnce(WithArgs<3>(Invoke([](mojo_ipc::RoutineUpdate* update) {
        update->progress_percent = kFakeProgressPercent;
      })));

  mojo_ipc::RoutineUpdatePtr response;
  service()->GetRoutineUpdate(
      kId, kCommand, kIncludeOutput,
      base::Bind(&SaveMojoResponse<mojo_ipc::RoutineUpdatePtr>, &response));

  ASSERT_TRUE(!response.is_null());
  EXPECT_EQ(response->progress_percent, kFakeProgressPercent);
}

// Test that we report available routines correctly.
TEST_F(CrosHealthdMojoServiceTest, RequestAvailableRoutines) {
  const std::vector<mojo_ipc::DiagnosticRoutineEnum> available_routines = {
      mojo_ipc::DiagnosticRoutineEnum::kUrandom,
      mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck,
  };

  EXPECT_CALL(*routine_service(), GetAvailableRoutines())
      .WillOnce(Return(available_routines));

  std::vector<mojo_ipc::DiagnosticRoutineEnum> response;
  service()->GetAvailableRoutines(base::Bind(
      [](std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>* out,
         const std::vector<
             chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>& routines) {
        *out = routines;
      },
      &response));

  EXPECT_EQ(response, available_routines);
}

}  // namespace diagnostics
