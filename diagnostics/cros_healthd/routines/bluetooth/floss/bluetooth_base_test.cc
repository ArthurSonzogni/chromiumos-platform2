// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_base.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/bluetooth/bluetooth_constants.h"
#include "diagnostics/cros_healthd/system/fake_floss_event_hub.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/system/mock_floss_controller.h"
#include "diagnostics/dbus_bindings/bluetooth_manager/dbus-proxy-mocks.h"
#include "diagnostics/dbus_bindings/floss/dbus-proxy-mocks.h"

namespace diagnostics::floss {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;
using ::testing::WithArg;

const dbus::ObjectPath kDefaultAdapterPath{
    "/org/chromium/bluetooth/hci0/adapter"};
const int32_t kDefaultHciInterface = 0;

class BluetoothRoutineBaseTest : public testing::Test {
 public:
  BluetoothRoutineBaseTest(const BluetoothRoutineBaseTest&) = delete;
  BluetoothRoutineBaseTest& operator=(const BluetoothRoutineBaseTest&) = delete;

 protected:
  BluetoothRoutineBaseTest() = default;

  void SetUp() override {
    ON_CALL(*mock_floss_controller(), GetManager())
        .WillByDefault(Return(&mock_manager_proxy_));
  }

  void TearDown() override {
    // Report null to ignore calls when routine is deconstructed.
    ON_CALL(*mock_floss_controller(), GetManager())
        .WillByDefault(Return(nullptr));
  }

  MockFlossController* mock_floss_controller() {
    return mock_context_.mock_floss_controller();
  }

  FakeFlossEventHub* fake_floss_event_hub() {
    return mock_context_.fake_floss_event_hub();
  }

  bool InitializeSync() {
    base::test::TestFuture<bool> future;
    routine_base_.Initialize(future.GetCallback());
    return future.Get();
  }

  std::optional<std::string> RunPreCheckSync() {
    base::test::TestFuture<std::optional<std::string>> future;
    routine_base_.RunPreCheck(future.GetCallback());
    return future.Get();
  }

  base::expected<bool, std::string> ChangeAdapterPoweredStateSync(
      bool powered) {
    base::test::TestFuture<const base::expected<bool, std::string>&> future;
    routine_base_.ChangeAdapterPoweredState(powered, future.GetCallback());
    return future.Get();
  }

  // Set the adapter with HCI interface 0 as default.
  void SetupGetDefaultAdapterCall(bool success = true) {
    if (success) {
      EXPECT_CALL(mock_manager_proxy_, GetDefaultAdapterAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<0>(kDefaultHciInterface));
    } else {
      error_ = brillo::Error::Create(FROM_HERE, "", "", "");
      EXPECT_CALL(mock_manager_proxy_, GetDefaultAdapterAsync(_, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(error_.get()));
    }
  }

  // Get the adapter with HCI interface 0.
  void SetupGetAdaptersCall() {
    EXPECT_CALL(*mock_floss_controller(), GetAdapters())
        .WillOnce(Return(
            std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{
                &mock_adapter_proxy_}));
    EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
        .WillOnce(ReturnRef(kDefaultAdapterPath));
  }

  // Set the adapter initial powered state to |powered|.
  void SetupGetAdapterEnabledCall(bool powered, bool success = true) {
    if (success) {
      EXPECT_CALL(mock_manager_proxy_,
                  GetAdapterEnabledAsync(kDefaultHciInterface, _, _, _))
          .WillOnce(base::test::RunOnceCallback<1>(/*enabled=*/powered));
    } else {
      error_ = brillo::Error::Create(FROM_HERE, "", "", "");
      EXPECT_CALL(mock_manager_proxy_,
                  GetAdapterEnabledAsync(kDefaultHciInterface, _, _, _))
          .WillOnce(base::test::RunOnceCallback<2>(error_.get()));
    }
  }

  // Setup all the required call for calling |Initialize| successfully.
  void SetupInitializeSuccessCall(bool initial_powered) {
    EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
        .WillOnce(base::test::RunOnceCallback<0>(/*floss_enabled=*/true));
    SetupGetDefaultAdapterCall();
    if (initial_powered) {
      SetupGetAdaptersCall();
    } else {
      EXPECT_CALL(*mock_floss_controller(), GetAdapters())
          .WillOnce(
              Return(std::vector<
                     org::chromium::bluetooth::BluetoothProxyInterface*>{}));
    }
    SetupGetAdapterEnabledCall(/*powered=*/initial_powered);
  }

  MockContext mock_context_;
  BluetoothRoutineBase routine_base_{&mock_context_};
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock> mock_adapter_proxy_;
  StrictMock<org::chromium::bluetooth::ManagerProxyMock> mock_manager_proxy_;
  brillo::ErrorPtr error_;

 private:
  base::test::TaskEnvironment task_environment_;
};

// Test that the BluetoothRoutineBase can get adapter successfully.
TEST_F(BluetoothRoutineBaseTest, GetAdapterSuccess) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_EQ(InitializeSync(), true);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), &mock_adapter_proxy_);
}

// Test that the BluetoothRoutineBase can handle error when getting manager
// proxy.
TEST_F(BluetoothRoutineBaseTest, GetManagerProxyError) {
  InSequence s;
  ON_CALL(*mock_floss_controller(), GetManager())
      .WillByDefault(Return(nullptr));

  EXPECT_EQ(InitializeSync(), false);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can handle error when floss is disabled.
TEST_F(BluetoothRoutineBaseTest, FlossDisabledError) {
  InSequence s;
  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*floss_enabled=*/false));

  EXPECT_EQ(InitializeSync(), false);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can handle error when getting floss
// enabled state.
TEST_F(BluetoothRoutineBaseTest, GetFlossEnabledError) {
  InSequence s;
  error_ = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error_.get()));

  EXPECT_EQ(InitializeSync(), false);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can handle error when getting default
// adapter.
TEST_F(BluetoothRoutineBaseTest, GetDefaultAdapterError) {
  InSequence s;

  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*floss_enabled=*/true));
  // Fails to setup default adapter.
  SetupGetDefaultAdapterCall(/*success=*/false);

  EXPECT_EQ(InitializeSync(), false);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can handle error when getting powered
// state of default adapter.
TEST_F(BluetoothRoutineBaseTest, GetAdapterEnabledError) {
  InSequence s;

  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*floss_enabled=*/true));
  SetupGetDefaultAdapterCall();
  SetupGetAdaptersCall();
  // Fails to get adapter enabled state.
  SetupGetAdapterEnabledCall(/*powered=*/true, /*success=*/false);

  EXPECT_EQ(InitializeSync(), false);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), &mock_adapter_proxy_);
}

// Test that the BluetoothRoutineBase can handle empty adapters and return
// null.
TEST_F(BluetoothRoutineBaseTest, EmptyAdapter) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  EXPECT_EQ(InitializeSync(), true);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can handle null adapter and return null.
TEST_F(BluetoothRoutineBaseTest, NullAdapter) {
  InSequence s;
  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*floss_enabled=*/true));
  SetupGetDefaultAdapterCall(/*success=*/true);
  EXPECT_CALL(*mock_floss_controller(), GetAdapters())
      .WillOnce(Return(
          std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{
              nullptr}));
  SetupGetAdapterEnabledCall(/*powered=*/false);

  EXPECT_EQ(InitializeSync(), true);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), nullptr);
}

// Test that the BluetoothRoutineBase can handle Multiple adapter and return
// the default one.
TEST_F(BluetoothRoutineBaseTest, MultipleAdapter) {
  InSequence s;
  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*floss_enabled=*/true));
  SetupGetDefaultAdapterCall(/*success=*/true);

  // Non-default adapter with HCI interface 1.
  const dbus::ObjectPath adapter_path_non_default{
      "/org/chromium/bluetooth/hci1/adapter"};
  StrictMock<org::chromium::bluetooth::BluetoothProxyMock>
      mock_adapter_proxy_non_default_;

  // Setup multiple adapters.
  EXPECT_CALL(*mock_floss_controller(), GetAdapters)
      .WillOnce(Return(
          std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{
              &mock_adapter_proxy_non_default_, &mock_adapter_proxy_}));
  EXPECT_CALL(mock_adapter_proxy_non_default_, GetObjectPath)
      .WillOnce(ReturnRef(adapter_path_non_default));
  EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
      .WillOnce(ReturnRef(kDefaultAdapterPath));
  SetupGetAdapterEnabledCall(/*powered=*/true);

  EXPECT_EQ(InitializeSync(), true);
  EXPECT_EQ(routine_base_.GetDefaultAdapter(), &mock_adapter_proxy_);
}

// Test that the BluetoothRoutineBase can handle the missing manager proxy
// when getting adapter powered during initialization.
TEST_F(BluetoothRoutineBaseTest, GetPoweredFailedMissingManagerProxy) {
  InSequence s;

  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*floss_enabled=*/true));
  EXPECT_CALL(mock_manager_proxy_, GetDefaultAdapterAsync(_, _, _))
      .WillOnce(WithArg<0>([&](base::OnceCallback<void(int32_t)> on_success) {
        // Manager proxy is removed unexpectedly.
        fake_floss_event_hub()->SendManagerRemoved();
        std::move(on_success).Run(kDefaultHciInterface);
      }));
  SetupGetAdaptersCall();
  EXPECT_EQ(InitializeSync(), false);
}

// Test that the BluetoothRoutineBase can pass the pre-check when the powered
// is off at first.
TEST_F(BluetoothRoutineBaseTest, PreCheckPassedPoweredOff) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  EXPECT_EQ(InitializeSync(), true);

  EXPECT_FALSE(RunPreCheckSync().has_value());
}

// Test that the BluetoothRoutineBase can pass the pre-check when the powered
// is on at first.
TEST_F(BluetoothRoutineBaseTest, PreCheckPassedPoweredOn) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_EQ(InitializeSync(), true);

  // Get the discovering off.
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  EXPECT_FALSE(RunPreCheckSync().has_value());
}

// Test that the BluetoothRoutineBase can handle that the adapter is missing
// when the powered is on at first.
TEST_F(BluetoothRoutineBaseTest, PreCheckFailedNoAdapter) {
  InSequence s;
  EXPECT_CALL(mock_manager_proxy_, GetFlossEnabledAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*floss_enabled=*/true));
  SetupGetDefaultAdapterCall();
  // The adapter is missing when the powered is on.
  EXPECT_CALL(*mock_floss_controller(), GetAdapters())
      .WillOnce(Return(
          std::vector<org::chromium::bluetooth::BluetoothProxyInterface*>{}));
  SetupGetAdapterEnabledCall(/*powered=*/true);

  EXPECT_EQ(InitializeSync(), true);

  EXPECT_EQ(RunPreCheckSync(), "Failed to get default adapter.");
}

// Test that the BluetoothRoutineBase can handle that the adapter is already
// in discovery mode when running pre-check.
TEST_F(BluetoothRoutineBaseTest, PreCheckFailedDiscoveringOn) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_EQ(InitializeSync(), true);

  // Get the discovering on.
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/true));

  EXPECT_EQ(RunPreCheckSync(), kBluetoothRoutineFailedDiscoveryMode);
}

// Test that the BluetoothRoutineBase can handle the error when getting
// adapter discovering state during pre-check.
TEST_F(BluetoothRoutineBaseTest, PreCheckFailedGetDiscoveringError) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_EQ(InitializeSync(), true);

  // Fail to get the discovering.
  error_ = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_adapter_proxy_, IsDiscoveringAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<1>(error_.get()));

  EXPECT_EQ(RunPreCheckSync(), "Failed to get adapter discovering state.");
}

// Test that the BluetoothRoutineBase can handle the missing manager proxy
// when running pre-check.
TEST_F(BluetoothRoutineBaseTest, PreCheckFailedMissingManagerProxy) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  EXPECT_EQ(InitializeSync(), true);

  fake_floss_event_hub()->SendManagerRemoved();
  EXPECT_EQ(RunPreCheckSync(), "Failed to access Bluetooth manager proxy.");
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered on
// when the powered is already on.
TEST_F(BluetoothRoutineBaseTest, EnsureAdapterPoweredAlreadyOn) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_EQ(InitializeSync(), true);

  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>());
  EXPECT_EQ(ChangeAdapterPoweredStateSync(/*powered=*/true), base::ok(true));
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered on
// when the powered is off at first.
TEST_F(BluetoothRoutineBaseTest, EnsureAdapterPoweredOnSuccess) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  EXPECT_EQ(InitializeSync(), true);

  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterAdded(&mock_adapter_proxy_);
        fake_floss_event_hub()->SendAdapterPoweredChanged(kDefaultHciInterface,
                                                          /*powered=*/true);
      }));

  // Call on adapter added in Floss event hub.
  EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
      .WillOnce(ReturnRef(kDefaultAdapterPath));
  EXPECT_CALL(mock_adapter_proxy_, RegisterCallbackAsync);
  EXPECT_CALL(mock_adapter_proxy_, RegisterConnectionCallbackAsync);

  EXPECT_CALL(mock_adapter_proxy_, GetObjectPath)
      .WillOnce(ReturnRef(kDefaultAdapterPath));
  EXPECT_EQ(ChangeAdapterPoweredStateSync(/*powered=*/true), base::ok(true));
}

// Test that the BluetoothRoutineBase can handle the error when powering on
// the adapter.
TEST_F(BluetoothRoutineBaseTest, EnsureAdapterPoweredOnError) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  EXPECT_EQ(InitializeSync(), true);

  error_ = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error_.get()));
  EXPECT_EQ(ChangeAdapterPoweredStateSync(/*powered=*/true), base::ok(false));
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered off
// when the powered is already off.
TEST_F(BluetoothRoutineBaseTest, EnsureAdapterPoweredAlreadyOff) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  EXPECT_EQ(InitializeSync(), true);

  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>());
  EXPECT_EQ(ChangeAdapterPoweredStateSync(/*powered=*/false), base::ok(true));
}

// Test that the BluetoothRoutineBase can ensure the adapter is powered off
// when the powered is on at first.
TEST_F(BluetoothRoutineBaseTest, EnsureAdapterPoweredOffSuccess) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_EQ(InitializeSync(), true);

  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(WithArg<1>([&](base::OnceCallback<void()> on_success) {
        std::move(on_success).Run();
        fake_floss_event_hub()->SendAdapterRemoved(kDefaultAdapterPath);
      }));
  EXPECT_EQ(ChangeAdapterPoweredStateSync(/*powered=*/false), base::ok(true));
}

// Test that the BluetoothRoutineBase can handle the error when powering off
// the adapter.
TEST_F(BluetoothRoutineBaseTest, EnsureAdapterPoweredOffError) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_EQ(InitializeSync(), true);

  error_ = brillo::Error::Create(FROM_HERE, "", "", "");
  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<2>(error_.get()));
  EXPECT_EQ(ChangeAdapterPoweredStateSync(/*powered=*/false), base::ok(false));
}

// Test that the BluetoothRoutineBase can handle the missing manager proxy
// when changing adapter powered.
TEST_F(BluetoothRoutineBaseTest, ChangePoweredErrorMissingManagerProxy) {
  InSequence s;
  SetupInitializeSuccessCall(/*initial_powered=*/true);
  EXPECT_EQ(InitializeSync(), true);

  fake_floss_event_hub()->SendManagerRemoved();
  EXPECT_EQ(ChangeAdapterPoweredStateSync(/*powered=*/false),
            base::unexpected("Failed to access Bluetooth manager proxy."));
}

// Test that the BluetoothRoutineBase can reset powered state to on when
// deconstructed.
TEST_F(BluetoothRoutineBaseTest, ResetPoweredOnDeconstructed) {
  InSequence s;
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);

  SetupInitializeSuccessCall(/*initial_powered=*/true);
  base::test::TestFuture<bool> future;
  routine_base->Initialize(future.GetCallback());
  EXPECT_EQ(future.Get(), true);

  // Reset.
  EXPECT_CALL(mock_manager_proxy_, StartAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>());
  routine_base.reset();
}

// Test that the BluetoothRoutineBase can reset powered state to off when
// deconstructed.
TEST_F(BluetoothRoutineBaseTest, ResetPoweredOffDeconstructed) {
  InSequence s;
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);

  SetupInitializeSuccessCall(/*initial_powered=*/false);
  base::test::TestFuture<bool> future;
  routine_base->Initialize(future.GetCallback());
  EXPECT_EQ(future.Get(), true);

  // Reset.
  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>());
  routine_base.reset();
}

// Test that the BluetoothRoutineBase can stop discovery when deconstructed.
TEST_F(BluetoothRoutineBaseTest, SetupStopDiscoveryJob) {
  InSequence s;
  auto routine_base = std::make_unique<BluetoothRoutineBase>(&mock_context_);

  // Initialize to setup default adapter.
  SetupInitializeSuccessCall(/*initial_powered=*/false);
  base::test::TestFuture<bool> future;
  routine_base->Initialize(future.GetCallback());
  EXPECT_EQ(future.Get(), true);

  // Stop discovery.
  routine_base->SetupStopDiscoveryJob();
  SetupGetAdaptersCall();
  EXPECT_CALL(mock_adapter_proxy_, CancelDiscoveryAsync(_, _, _))
      .WillOnce(base::test::RunOnceCallback<0>(/*discovering=*/false));

  // Reset.
  EXPECT_CALL(mock_manager_proxy_, StopAsync(kDefaultHciInterface, _, _, _))
      .WillOnce(base::test::RunOnceCallback<1>());
  routine_base.reset();
}

}  // namespace
}  // namespace diagnostics::floss
