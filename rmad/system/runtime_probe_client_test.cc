// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/system/fake_runtime_probe_client.h"
#include "rmad/system/runtime_probe_client_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <brillo/errors/error.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/runtime_probe/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <runtime_probe/dbus-proxy-mocks.h>

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace rmad {

class RuntimeProbeClientTest : public testing::Test {
 public:
  RuntimeProbeClientTest() = default;
  ~RuntimeProbeClientTest() override = default;
};

TEST_F(RuntimeProbeClientTest, ProbeAllCategories_Success) {
  auto mock_runtime_probe_proxy =
      std::make_unique<StrictMock<org::chromium::RuntimeProbeProxyMock>>();
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
  EXPECT_CALL(*mock_runtime_probe_proxy, ProbeCategories(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(probe_result_proto), Return(true)));

  auto runtime_probe_client = std::make_unique<RuntimeProbeClientImpl>(
      std::move(mock_runtime_probe_proxy));
  ComponentsWithIdentifier components;
  EXPECT_TRUE(runtime_probe_client->ProbeCategories({}, &components));
  EXPECT_EQ(2, components.size());
  EXPECT_EQ(components[0].first, RMAD_COMPONENT_CAMERA);
  EXPECT_EQ(components[0].second, "camera_0001_0002");
  EXPECT_EQ(components[1].first, RMAD_COMPONENT_ETHERNET);
  EXPECT_EQ(components[1].second, "network(ethernet:pci)_0005_0006");
}

TEST_F(RuntimeProbeClientTest, ProbeSingleCategory_Success) {
  auto mock_runtime_probe_proxy =
      std::make_unique<StrictMock<org::chromium::RuntimeProbeProxyMock>>();
  runtime_probe::ProbeResult probe_result_proto;
  probe_result_proto.add_battery();
  EXPECT_CALL(*mock_runtime_probe_proxy, ProbeCategories(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(probe_result_proto), Return(true)));

  auto runtime_probe_client = std::make_unique<RuntimeProbeClientImpl>(
      std::move(mock_runtime_probe_proxy));
  ComponentsWithIdentifier components;
  EXPECT_TRUE(runtime_probe_client->ProbeCategories({}, &components));
  EXPECT_EQ(1, components.size());
  EXPECT_EQ(components[0].first, RMAD_COMPONENT_BATTERY);
}

TEST_F(RuntimeProbeClientTest, ProbeCategories_NoResponse) {
  auto mock_runtime_probe_proxy =
      std::make_unique<StrictMock<org::chromium::RuntimeProbeProxyMock>>();
  EXPECT_CALL(*mock_runtime_probe_proxy, ProbeCategories(_, _, _, _))
      .WillOnce([](const runtime_probe::ProbeRequest&,
                   runtime_probe::ProbeResult*, brillo::ErrorPtr* error, int) {
        brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                             "0", "message");
        return false;
      });

  auto runtime_probe_client = std::make_unique<RuntimeProbeClientImpl>(
      std::move(mock_runtime_probe_proxy));
  ComponentsWithIdentifier components;
  EXPECT_FALSE(runtime_probe_client->ProbeCategories({}, &components));
}

TEST_F(RuntimeProbeClientTest, ProbeCategories_ErrorResponse) {
  auto mock_runtime_probe_proxy =
      std::make_unique<StrictMock<org::chromium::RuntimeProbeProxyMock>>();
  runtime_probe::ProbeResult probe_result_proto;
  probe_result_proto.set_error(
      runtime_probe::RUNTIME_PROBE_ERROR_PROBE_CONFIG_INVALID);
  EXPECT_CALL(*mock_runtime_probe_proxy, ProbeCategories(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(probe_result_proto), Return(true)));

  auto runtime_probe_client = std::make_unique<RuntimeProbeClientImpl>(
      std::move(mock_runtime_probe_proxy));
  ComponentsWithIdentifier components;
  EXPECT_FALSE(runtime_probe_client->ProbeCategories({}, &components));
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
