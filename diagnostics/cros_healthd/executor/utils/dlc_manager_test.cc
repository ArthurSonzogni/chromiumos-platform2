// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>
#include <utility>

#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/errors/error.h>
#include <dbus/mock_object_proxy.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
// NOLINTNEXTLINE(build/include_alpha) dbus-proxy-mocks.h needs dlcservice.pb.h
#include <dlcservice/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/executor/utils/dlc_manager.h"

namespace diagnostics {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::WithArg;
using ::testing::WithArgs;

class DlcManagerTest : public testing::Test {
 protected:
  DlcManagerTest() = default;
  DlcManagerTest(const DlcManagerTest&) = delete;
  DlcManagerTest& operator=(const DlcManagerTest&) = delete;

  void SetUp() override {
    dlc_service_object_proxy_ =
        new dbus::MockObjectProxy(nullptr, "", dbus::ObjectPath("/"));
    ON_CALL(mock_dlc_service_, GetObjectProxy())
        .WillByDefault(Return(dlc_service_object_proxy_.get()));
  }

  std::optional<std::string> GetBinaryRootPathSync(const std::string& dlc_id) {
    base::test::TestFuture<std::optional<std::string>> future;
    dlc_manager_.GetBinaryRootPath(dlc_id, future.GetCallback());
    return future.Get();
  }

  void SetDlcServiceAvailability(bool available) {
    EXPECT_CALL(*dlc_service_object_proxy_.get(),
                DoWaitForServiceToBeAvailable(_))
        .WillOnce(WithArg<0>(
            [=](dbus::ObjectProxy::WaitForServiceToBeAvailableCallback*
                    callback) { std::move(*callback).Run(available); }));
  }

  void SetInstallDlcCall(bool is_success) {
    EXPECT_CALL(mock_dlc_service_, InstallDlcAsync(_, _, _, _))
        .WillOnce(WithArgs<0, 1, 2>(Invoke(
            [=](const std::string& in_id,
                base::OnceCallback<void()> success_callback,
                base::OnceCallback<void(brillo::Error*)> error_callback) {
              last_install_dlc_id = in_id;
              if (is_success) {
                std::move(success_callback).Run();
              } else {
                auto error = brillo::Error::Create(FROM_HERE, "", "", "");
                std::move(error_callback).Run(error.get());
              }
            })));
  }

  void SetGetDlcStateCall(const dlcservice::DlcState& state, bool is_success) {
    EXPECT_CALL(mock_dlc_service_, GetDlcStateAsync(_, _, _, _))
        .WillOnce(WithArgs<0, 1, 2>(Invoke(
            [=](const std::string& in_id,
                base::OnceCallback<void(const dlcservice::DlcState&)>
                    success_callback,
                base::OnceCallback<void(brillo::Error*)> error_callback) {
              last_get_dlc_state_id = in_id;
              if (is_success) {
                std::move(success_callback).Run(state);
              } else {
                auto error = brillo::Error::Create(FROM_HERE, "", "", "");
                std::move(error_callback).Run(error.get());
              }
            })));
  }

 private:
  base::test::TaskEnvironment task_environment_;
  scoped_refptr<dbus::MockObjectProxy> dlc_service_object_proxy_;
  org::chromium::DlcServiceInterfaceProxyMock mock_dlc_service_;
  DlcManager dlc_manager_{&mock_dlc_service_};

 protected:
  std::optional<std::string> last_install_dlc_id;
  std::optional<std::string> last_get_dlc_state_id;
};

TEST_F(DlcManagerTest, GetRootPathSuccess) {
  SetDlcServiceAvailability(/*available=*/true);
  SetInstallDlcCall(/*is_success=*/true);
  auto state = dlcservice::DlcState{};
  state.set_is_verified(true);
  state.set_root_path("/run/imageloader/test-dlc/package/root");
  SetGetDlcStateCall(state, /*is_success=*/true);

  EXPECT_EQ(GetBinaryRootPathSync("test-dlc"), state.root_path());
  EXPECT_EQ(last_install_dlc_id, "test-dlc");
  EXPECT_EQ(last_get_dlc_state_id, "test-dlc");
}

TEST_F(DlcManagerTest, DlcServiceUnavailableError) {
  SetDlcServiceAvailability(/*available=*/false);
  EXPECT_FALSE(GetBinaryRootPathSync("test-dlc").has_value());
  EXPECT_FALSE(last_install_dlc_id.has_value());
  EXPECT_FALSE(last_get_dlc_state_id.has_value());
}

TEST_F(DlcManagerTest, InstallDlcError) {
  SetDlcServiceAvailability(/*available=*/true);
  SetInstallDlcCall(/*is_success=*/false);
  EXPECT_FALSE(GetBinaryRootPathSync("test-dlc").has_value());
  EXPECT_EQ(last_install_dlc_id, "test-dlc");
  EXPECT_FALSE(last_get_dlc_state_id.has_value());
}

TEST_F(DlcManagerTest, GetDlcStateError) {
  SetDlcServiceAvailability(/*available=*/true);
  SetInstallDlcCall(/*is_success=*/true);
  auto state = dlcservice::DlcState{};
  SetGetDlcStateCall(state, /*is_success=*/false);
  EXPECT_FALSE(GetBinaryRootPathSync("test-dlc").has_value());
  EXPECT_EQ(last_install_dlc_id, "test-dlc");
  EXPECT_EQ(last_get_dlc_state_id, "test-dlc");
}

TEST_F(DlcManagerTest, GetInvalidDlcError) {
  SetDlcServiceAvailability(/*available=*/true);
  SetInstallDlcCall(/*is_success=*/true);
  auto state = dlcservice::DlcState{};
  state.set_is_verified(false);
  SetGetDlcStateCall(state, /*is_success=*/true);
  EXPECT_FALSE(GetBinaryRootPathSync("test-dlc").has_value());
  EXPECT_EQ(last_install_dlc_id, "test-dlc");
  EXPECT_EQ(last_get_dlc_state_id, "test-dlc");
}

}  // namespace
}  // namespace diagnostics
