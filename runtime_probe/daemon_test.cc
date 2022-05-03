// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <utility>

#include <base/files/file_path.h>
#include <base/values.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/daemon.h"
#include "runtime_probe/fake_probe_config_loader.h"
#include "runtime_probe/probe_config_loader.h"
#include "runtime_probe/proto_bindings/runtime_probe.pb.h"

namespace runtime_probe {

namespace {

using ::brillo::dbus_utils::MockDBusMethodResponse;

using ::testing::NiceMock;
using ::testing::Return;

class DaemonTest : public ::testing::Test {
 protected:
  void SetUp() { SetDefaultProbeConfigData(); }
  void SetDefaultProbeConfigData() {
    config_loader_.set_fake_probe_config_data(ProbeConfigData{
        .path = base::FilePath{"/etc/runtime_probe/probe_config.json"},
        .config = base::Value(base::Value::Type::DICTIONARY),
        .sha1_hash = "0123456789abcdef"});
  }
  FakeProbeConfigLoader config_loader_;
  Daemon daemon_{&config_loader_};
  org::chromium::RuntimeProbeInterface* const dbus_adaptor_{&daemon_};
};

TEST_F(DaemonTest, ProbeCategories_LoadDefaultFailed) {
  config_loader_.clear_fake_probe_config_data();
  ProbeRequest request;
  request.set_probe_default_category(true);
  std::optional<ProbeResult> reply;
  auto response =
      std::make_unique<MockDBusMethodResponse<ProbeResult>>(nullptr);
  response->save_return_args(&reply);
  dbus_adaptor_->ProbeCategories(std::move(response), request);
  EXPECT_TRUE(reply);
  EXPECT_EQ(reply->error(), RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
}

// TODO(kevinptt): Add more test cases for D-Bus methods.

}  // namespace

}  // namespace runtime_probe
