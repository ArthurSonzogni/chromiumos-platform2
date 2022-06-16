// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_runtime_probe_client.h"
#include "rmad/system/runtime_probe_client_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/runtime_probe/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

  void SetUp() override {
    EXPECT_CALL(*mock_bus_,
                GetObjectProxy(
                    runtime_probe::kRuntimeProbeServiceName,
                    dbus::ObjectPath(runtime_probe::kRuntimeProbeServicePath)))
        .WillOnce(Return(mock_object_proxy_.get()));
    runtime_probe_client_ = std::make_unique<RuntimeProbeClientImpl>(mock_bus_);
  }

 protected:
  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> mock_object_proxy_;
  std::unique_ptr<RuntimeProbeClientImpl> runtime_probe_client_;
};

TEST_F(RuntimeProbeClientTest, ProbeAllCategories_Success) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> runtime_probe_response =
            dbus::Response::CreateEmpty();
        runtime_probe::ProbeResult probe_result_proto;
        // Fixed USB camera.
        runtime_probe::Camera* camera1 = probe_result_proto.add_camera();
        camera1->mutable_values()->set_usb_vendor_id(0x0001);
        camera1->mutable_values()->set_usb_product_id(0x0002);
        camera1->mutable_values()->set_usb_removable(runtime_probe::FIXED);
        // Removable USB camera. Will be filtered.
        runtime_probe::Camera* camera2 = probe_result_proto.add_camera();
        camera2->mutable_values()->set_usb_vendor_id(0x0003);
        camera2->mutable_values()->set_usb_product_id(0x0004);
        camera2->mutable_values()->set_usb_removable(runtime_probe::REMOVABLE);
        // PCI network.
        runtime_probe::Network* ethernet1 = probe_result_proto.add_ethernet();
        ethernet1->mutable_values()->set_type("ethernet");
        ethernet1->mutable_values()->set_bus_type("pci");
        ethernet1->mutable_values()->set_pci_vendor_id(0x0005);
        ethernet1->mutable_values()->set_pci_device_id(0x0006);
        // USB network. Will be filtered.
        runtime_probe::Network* ethernet2 = probe_result_proto.add_ethernet();
        ethernet2->mutable_values()->set_type("ethernet");
        ethernet2->mutable_values()->set_bus_type("usb");
        ethernet2->mutable_values()->set_usb_vendor_id(0x0007);
        ethernet2->mutable_values()->set_usb_product_id(0x0008);

        dbus::MessageWriter writer(runtime_probe_response.get());
        writer.AppendProtoAsArrayOfBytes(probe_result_proto);
        return runtime_probe_response;
      });

  ComponentsWithIdentifier components;
  EXPECT_TRUE(runtime_probe_client_->ProbeCategories({}, &components));
  EXPECT_EQ(2, components.size());
  EXPECT_EQ(components[0].first, RMAD_COMPONENT_CAMERA);
  EXPECT_EQ(components[0].second, "Camera_0001_0002");
  EXPECT_EQ(components[1].first, RMAD_COMPONENT_ETHERNET);
  EXPECT_EQ(components[1].second, "Network(ethernet:pci)_0005_0006");
}

TEST_F(RuntimeProbeClientTest, ProbeSingleCategory_Success) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> runtime_probe_response =
            dbus::Response::CreateEmpty();
        runtime_probe::ProbeResult probe_result_proto;
        probe_result_proto.add_battery();
        dbus::MessageWriter writer(runtime_probe_response.get());
        writer.AppendProtoAsArrayOfBytes(probe_result_proto);
        return runtime_probe_response;
      });

  ComponentsWithIdentifier components;
  EXPECT_TRUE(runtime_probe_client_->ProbeCategories({}, &components));
  EXPECT_EQ(1, components.size());
  EXPECT_EQ(components[0].first, RMAD_COMPONENT_BATTERY);
}

TEST_F(RuntimeProbeClientTest, ProbeCategories_NoResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
      .WillOnce([](dbus::MethodCall*, int) { return nullptr; });

  ComponentsWithIdentifier components;
  EXPECT_FALSE(runtime_probe_client_->ProbeCategories({}, &components));
}

TEST_F(RuntimeProbeClientTest, ProbeCategories_ErrorResponse) {
  EXPECT_CALL(*mock_object_proxy_, CallMethodAndBlock(_, _))
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

  ComponentsWithIdentifier components;
  EXPECT_FALSE(runtime_probe_client_->ProbeCategories({}, &components));
}

namespace fake {

// Tests for |FakeRuntimeProbeClient|.
class FakeRuntimeProbeClientTest : public testing::Test {
 public:
  FakeRuntimeProbeClientTest() = default;
  ~FakeRuntimeProbeClientTest() override = default;

  void SetUp() override {
    fake_runtime_probe_client_ = std::make_unique<FakeRuntimeProbeClient>();
  }

 protected:
  std::unique_ptr<FakeRuntimeProbeClient> fake_runtime_probe_client_;
};

TEST_F(FakeRuntimeProbeClientTest, ProbeCategories_Specified) {
  ComponentsWithIdentifier components;
  EXPECT_TRUE(fake_runtime_probe_client_->ProbeCategories(
      {RMAD_COMPONENT_CAMERA}, &components));
  EXPECT_EQ(1, components.size());
  EXPECT_EQ(components[0].first, RMAD_COMPONENT_CAMERA);
}

TEST_F(FakeRuntimeProbeClientTest, ProbeCategories_All) {
  ComponentsWithIdentifier components;
  EXPECT_TRUE(fake_runtime_probe_client_->ProbeCategories({}, &components));
  EXPECT_EQ(8, components.size());
}

}  // namespace fake

}  // namespace rmad
