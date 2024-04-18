// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/socket_service_adaptor.h"

#include <memory>
#include <optional>
#include <vector>

#include <base/memory/ptr_util.h>
#include <base/memory/scoped_refptr.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>
#include <patchpanel/proto_bindings/traffic_annotation.pb.h>

#include "patchpanel/routing_service.h"

using testing::_;
using testing::Ge;
using testing::Return;

namespace patchpanel {
namespace {

class MockRoutingService : public RoutingService {
 public:
  MOCK_METHOD(bool,
              TagSocket,
              (int,
               std::optional<int>,
               VPNRoutingPolicy,
               std::optional<TrafficAnnotationId>),
              (override));
};

class SocketServiceAdaptorTest : public testing::Test {
 public:
  SocketServiceAdaptorTest()
      : mock_routing_svc_(new MockRoutingService),
        adaptor_(mock_bus_, base::WrapUnique(mock_routing_svc_)) {}

 protected:
  void SetUp() override {}

  scoped_refptr<dbus::MockBus> mock_bus_{
      new dbus::MockBus{dbus::Bus::Options{}}};
  MockRoutingService* mock_routing_svc_;
  SocketServiceAdaptor adaptor_;
};

base::ScopedFD MakeFd() {
  return base::ScopedFD(socket(AF_INET, SOCK_DGRAM, 0));
}

TEST_F(SocketServiceAdaptorTest, TagSocketInvalidFd) {
  EXPECT_CALL(*mock_routing_svc_, TagSocket(_, _, _, _)).Times(0);

  TagSocketRequest request;
  auto response = adaptor_.TagSocket(request, base::ScopedFD(-1));
  EXPECT_FALSE(response.success());
}

TEST_F(SocketServiceAdaptorTest, TagSocketStatus) {
  EXPECT_CALL(*mock_routing_svc_, TagSocket(Ge(0), _, _, _))
      .WillOnce(Return(true))
      .WillOnce(Return(false));

  TagSocketRequest request;
  auto res1 = adaptor_.TagSocket(request, MakeFd());
  EXPECT_TRUE(res1.success());
  auto res2 = adaptor_.TagSocket(request, MakeFd());
  EXPECT_FALSE(res2.success());
}

TEST_F(SocketServiceAdaptorTest, TagSocketNetworkId) {
  EXPECT_CALL(*mock_routing_svc_,
              TagSocket(Ge(0), std::optional<int>(23), _, _))
      .WillOnce(Return(true));

  TagSocketRequest request;
  request.set_network_id(23);
  auto response = adaptor_.TagSocket(request, MakeFd());
  EXPECT_TRUE(response.success());
}

TEST_F(SocketServiceAdaptorTest, TagSocketVpnPolicy) {
  EXPECT_CALL(*mock_routing_svc_,
              TagSocket(Ge(0), _, VPNRoutingPolicy::kDefault, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_routing_svc_,
              TagSocket(Ge(0), _, VPNRoutingPolicy::kRouteOnVPN, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_routing_svc_,
              TagSocket(Ge(0), _, VPNRoutingPolicy::kBypassVPN, _))
      .WillOnce(Return(true));

  const std::vector<TagSocketRequest::VpnRoutingPolicy> policies = {
      TagSocketRequest::DEFAULT_ROUTING,
      TagSocketRequest::ROUTE_ON_VPN,
      TagSocketRequest::BYPASS_VPN,
  };
  for (const auto policy : policies) {
    TagSocketRequest request;
    request.set_vpn_policy(policy);
    auto response = adaptor_.TagSocket(request, MakeFd());
    EXPECT_TRUE(response.success());
  }
}

TEST_F(SocketServiceAdaptorTest, TagSocketTrafficAnnotation) {
  const std::vector<TrafficAnnotationId> annotations = {
      TrafficAnnotationId::kUnspecified,
      TrafficAnnotationId::kShillPortalDetector,
      TrafficAnnotationId::kShillCapportClient,
      TrafficAnnotationId::kShillCarrierEntitlement,
  };
  for (const auto id : annotations) {
    EXPECT_CALL(*mock_routing_svc_,
                TagSocket(Ge(0), _, _, std::optional<TrafficAnnotationId>(id)))
        .WillOnce(Return(true));
  }

  const std::vector<traffic_annotation::TrafficAnnotation::Id>
      mojo_annotations = {
          traffic_annotation::TrafficAnnotation::UNSPECIFIED,
          traffic_annotation::TrafficAnnotation::SHILL_PORTAL_DETECTOR,
          traffic_annotation::TrafficAnnotation::SHILL_CAPPORT_CLIENT,
          traffic_annotation::TrafficAnnotation::SHILL_CARRIER_ENTITLEMENT,
      };
  for (const auto id : mojo_annotations) {
    TagSocketRequest request;
    auto ta = request.mutable_traffic_annotation();
    ta->set_host_id(id);
    auto response = adaptor_.TagSocket(request, MakeFd());
    EXPECT_TRUE(response.success());
  }
}

}  // namespace
}  // namespace patchpanel
