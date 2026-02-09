// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/hosts_connectivity_diagnostics.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/mock_bus.h>
#include <gtest/gtest.h>
#include <hosts_connectivity_diagnostics/proto_bindings/hosts_connectivity_diagnostics.pb.h>

namespace shill {
namespace {

using ResultCode = hosts_connectivity_diagnostics::ConnectivityResultCode;
using TestConnectivityResponse =
    hosts_connectivity_diagnostics::TestConnectivityResponse;

constexpr char kExampleDotCom[] = "example.com";
constexpr char kLoggingTag[] = "test_logging_tag";

class HostsConnectivityDiagnosticsTest : public testing::Test {
 protected:
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = base::MakeRefCounted<dbus::MockBus>(std::move(options));
    diagnostics_ =
        std::make_unique<HostsConnectivityDiagnostics>(mock_bus_, kLoggingTag);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};
  scoped_refptr<dbus::MockBus> mock_bus_;
  std::unique_ptr<HostsConnectivityDiagnostics> diagnostics_;
};

TEST_F(HostsConnectivityDiagnosticsTest, SkeletonReturnsInternalError) {
  base::test::TestFuture<const TestConnectivityResponse&> future;

  HostsConnectivityDiagnostics::RequestInfo request_info;
  request_info.raw_hostnames.emplace_back(kExampleDotCom);
  request_info.callback = future.GetCallback();
  diagnostics_->TestHostsConnectivity(std::move(request_info));

  const auto& response = future.Get();
  ASSERT_EQ(response.connectivity_results_size(), 1);
  EXPECT_EQ(response.connectivity_results(0).result_code(),
            ResultCode::INTERNAL_ERROR);
}

}  // namespace
}  // namespace shill
