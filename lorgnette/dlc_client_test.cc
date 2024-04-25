// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/dlc_client.h"

#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <chromeos/constants/lorgnette_dlc.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>

#include "dlcservice/proto_bindings/dlcservice.pb.h"
#include "dlcservice/dbus-proxy-mocks.h"  //NOLINT (build/include_alpha)
#include "lorgnette/test_util.h"

using ::testing::_;
using ::testing::InvokeWithoutArgs;
using ::testing::WithArgs;

namespace lorgnette {

constexpr char kRootPath[] = "/root/path";

namespace {

dlcservice::DlcState MakeDlcState(const std::string& dlc_id) {
  dlcservice::DlcState state;
  state.set_state(dlcservice::DlcState::INSTALLED);
  state.set_id(dlc_id);
  state.set_root_path(kRootPath);
  return state;
}

dlcservice::InstallRequest MakeInstallRequest(const std::string& dlc_id) {
  dlcservice::InstallRequest install_request;
  install_request.set_id(dlc_id);
  return install_request;
}

class DlcClientTest : public ::testing::Test {
 protected:
  DlcClientTest() {
    mock_dlcservice_proxy_ =
        std::make_unique<org::chromium::DlcServiceInterfaceProxyMock>();
  }
  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<org::chromium::DlcServiceInterfaceProxyMock>
      mock_dlcservice_proxy_;
};

TEST_F(DlcClientTest, InstallingReturnsNoRootPath) {
  EXPECT_CALL(*mock_dlcservice_proxy_, GetDlcState(_, _, _, _))
      .WillOnce(testing::Invoke(
          [](const std::string& in_id, dlcservice::DlcState* out_state,
             brillo::ErrorPtr* error, int /*timeout_ms*/) -> bool {
            out_state->set_state(dlcservice::DlcState::INSTALLING);
            return true;
          }));
  DlcClient dlc_client = DlcClient();
  dlc_client.Init(std::move(mock_dlcservice_proxy_));
  std::string error;

  EXPECT_FALSE(dlc_client.GetRootPath("dlc-test", &error).has_value());
  EXPECT_FALSE(error.empty());
}

TEST_F(DlcClientTest, NotInstalledReturnsNoRootPath) {
  EXPECT_CALL(*mock_dlcservice_proxy_, GetDlcState(_, _, _, _))
      .WillOnce(testing::Invoke(
          [](const std::string& in_id, dlcservice::DlcState* out_state,
             brillo::ErrorPtr* error, int /*timeout_ms*/) -> bool {
            out_state->set_state(dlcservice::DlcState::NOT_INSTALLED);
            return true;
          }));

  DlcClient dlc_client = DlcClient();
  dlc_client.Init(std::move(mock_dlcservice_proxy_));
  std::string error;

  EXPECT_FALSE(dlc_client.GetRootPath("dlc-test", &error).has_value());
  EXPECT_FALSE(error.empty());
}

TEST_F(DlcClientTest, InstalledReturnsRootPath) {
  EXPECT_CALL(*mock_dlcservice_proxy_, GetDlcState(_, _, _, _))
      .WillOnce(testing::Invoke(
          [](const std::string& in_id, dlcservice::DlcState* out_state,
             brillo::ErrorPtr* error, int /*timeout_ms*/) -> bool {
            EXPECT_EQ(in_id, "dlc-test");
            out_state->set_state(dlcservice::DlcState_State_INSTALLED);
            out_state->set_root_path("test/path/to/dlc/root");
            return true;
          }));

  DlcClient dlc_client = DlcClient();
  dlc_client.Init(std::move(mock_dlcservice_proxy_));
  std::string error;
  std::optional<std::string> root_path =
      dlc_client.GetRootPath("dlc-test", &error);

  EXPECT_TRUE(error.empty());
  EXPECT_TRUE(root_path.has_value());
  EXPECT_EQ(root_path.value(), "test/path/to/dlc/root");
}

TEST_F(DlcClientTest, RespondsToDlcStateChangeSignal) {
  base::RepeatingCallback<void(const dlcservice::DlcState&)> state_changed_cb;
  EXPECT_CALL(*mock_dlcservice_proxy_,
              DoRegisterDlcStateChangedSignalHandler(_, _))
      .WillOnce(WithArgs<0, 1>(
          [&](const base::RepeatingCallback<void(const dlcservice::DlcState&)>&
                  signal_callback,
              dbus::ObjectProxy::OnConnectedCallback* on_connected_callback) {
            state_changed_cb = signal_callback;
            std::move(*on_connected_callback).Run("", "", true);
          }));

  DlcClient dlc_client = DlcClient();
  base::RunLoop run_loop;
  bool called = false;
  dlc_client.SetCallbacks(
      base::BindLambdaForTesting(
          [&](const std::string& dlc_id, const base::FilePath& root_path) {
            EXPECT_EQ(dlc_id, kSaneBackendsPfuDlcId);
            EXPECT_EQ(root_path.value(), kRootPath);
            called = true;
            run_loop.Quit();
          }),
      base::BindLambdaForTesting(
          [](const std::string& dlc_id, const std::string& error_msg) {
            EXPECT_TRUE(false);
          }));
  dlc_client.Init(std::move(mock_dlcservice_proxy_));

  std::move(state_changed_cb).Run(MakeDlcState(kSaneBackendsPfuDlcId));
  run_loop.Run();

  EXPECT_TRUE(called);
}

TEST_F(DlcClientTest, InstallMultipleDlcs) {
  const std::string kFirstDlcId = "first_id";
  const std::string kSecondDlcId = "second_id";

  base::RunLoop run_loop_1;
  base::RunLoop run_loop_2;
  EXPECT_CALL(
      *mock_dlcservice_proxy_,
      InstallAsync(EqualsProto(MakeInstallRequest(kFirstDlcId)), _, _, _))
      .WillOnce(InvokeWithoutArgs([&]() { run_loop_1.Quit(); }));
  EXPECT_CALL(
      *mock_dlcservice_proxy_,
      InstallAsync(EqualsProto(MakeInstallRequest(kSecondDlcId)), _, _, _))
      .WillOnce(InvokeWithoutArgs([&]() { run_loop_2.Quit(); }));

  DlcClient dlc_client = DlcClient();
  dlc_client.Init(std::move(mock_dlcservice_proxy_));
  dlc_client.InstallDlc({kFirstDlcId, kSecondDlcId});

  run_loop_1.Run();
  run_loop_2.Run();
}

}  // namespace
}  // namespace lorgnette
