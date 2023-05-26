// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/dlc_client.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <utility>

#include "dlcservice/proto_bindings/dlcservice.pb.h"
#include "dlcservice/dbus-proxy-mocks.h"  //NOLINT (build/include_alpha)

using ::testing::_;

namespace lorgnette {
namespace {

class DlcClientTest : public ::testing::Test {
 protected:
  DlcClientTest() {
    mock_dlcservice_proxy_ =
        std::make_unique<org::chromium::DlcServiceInterfaceProxyMock>();
  }
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

}  // namespace
}  // namespace lorgnette
