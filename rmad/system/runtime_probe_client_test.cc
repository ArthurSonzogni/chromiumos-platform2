// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/runtime_probe/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rmad/system/runtime_probe_client_impl.h"

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace rmad {

class RuntimeProbeClientTest : public testing::Test {
 public:
  RuntimeProbeClientTest()
      : mock_bus_(new StrictMock<dbus::MockBus>(dbus::Bus::Options())),
        mock_object_proxy_(new StrictMock<dbus::MockObjectProxy>(
            mock_bus_.get(),
            runtime_probe::kRuntimeProbeServiceName,
            dbus::ObjectPath(runtime_probe::kRuntimeProbeServicePath))) {}
  ~RuntimeProbeClientTest() override = default;

  dbus::MockObjectProxy* mock_object_proxy() const {
    return mock_object_proxy_.get();
  }

  RuntimeProbeClientImpl* runtime_probe_client() const {
    return runtime_probe_client_.get();
  }

  void SetUp() override {
    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(
                    runtime_probe::kRuntimeProbeServiceName,
                    dbus::ObjectPath(runtime_probe::kRuntimeProbeServicePath)))
        .WillOnce(Return(mock_object_proxy_.get()));
    runtime_probe_client_ = std::make_unique<RuntimeProbeClientImpl>(mock_bus_);
  }

 private:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  std::unique_ptr<RuntimeProbeClientImpl> runtime_probe_client_;
};

TEST_F(RuntimeProbeClientTest, ProbeCategories_Success) {
  EXPECT_CALL(*mock_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> runtime_probe_response =
            dbus::Response::CreateEmpty();
        runtime_probe::ProbeResult probe_result_proto;
        probe_result_proto.add_battery();
        dbus::MessageWriter writer(runtime_probe_response.get());
        writer.AppendProtoAsArrayOfBytes(probe_result_proto);
        return runtime_probe_response;
      });

  std::set<RmadComponent> components;
  EXPECT_TRUE(runtime_probe_client()->ProbeCategories(&components));
  EXPECT_EQ(1, components.size());
  EXPECT_NE(components.find(RMAD_COMPONENT_BATTERY), components.end());
}

TEST_F(RuntimeProbeClientTest, ProbeCategories_NoResponse) {
  EXPECT_CALL(*mock_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });

  std::set<RmadComponent> components;
  EXPECT_FALSE(runtime_probe_client()->ProbeCategories(&components));
}

TEST_F(RuntimeProbeClientTest, ProbeCategories_ErrorResponse) {
  EXPECT_CALL(*mock_object_proxy(), CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> runtime_probe_response =
            dbus::Response::CreateEmpty();
        runtime_probe::ProbeResult probe_result_proto;
        probe_result_proto.set_error(
            runtime_probe::RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
        dbus::MessageWriter writer(runtime_probe_response.get());
        writer.AppendProtoAsArrayOfBytes(probe_result_proto);
        return runtime_probe_response;
      });

  std::set<RmadComponent> components;
  EXPECT_FALSE(runtime_probe_client()->ProbeCategories(&components));
}

}  // namespace rmad
