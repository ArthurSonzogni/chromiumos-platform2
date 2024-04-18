// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/socket_service_adaptor.h"

#include <utility>

#include <brillo/dbus/async_event_sequencer.h>
#include <chromeos/dbus/patchpanel/dbus-constants.h>
#include <dbus/object_path.h>
#include <patchpanel/proto_bindings/traffic_annotation.pb.h>

#include "patchpanel/routing_service.h"

namespace patchpanel {

SocketServiceAdaptor::SocketServiceAdaptor(
    scoped_refptr<::dbus::Bus> bus, std::unique_ptr<RoutingService> routing_svc)
    : org::chromium::SocketServiceAdaptor(this),
      routing_svc_(std::move(routing_svc)),
      dbus_object_(nullptr, bus, dbus::ObjectPath(kSocketServicePath)) {}

SocketServiceAdaptor::~SocketServiceAdaptor() {}

void SocketServiceAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

TagSocketResponse SocketServiceAdaptor::TagSocket(
    const TagSocketRequest& in_request, const base::ScopedFD& in_socket_fd) {
  TagSocketResponse response;

  if (!in_socket_fd.is_valid()) {
    LOG(ERROR) << __func__ << ": Invalid socket fd";
    response.set_success(false);
    return response;
  }

  std::optional<int> network_id = std::nullopt;
  if (in_request.has_network_id()) {
    network_id = in_request.network_id();
  }

  auto policy = VPNRoutingPolicy::kDefault;
  switch (in_request.vpn_policy()) {
    case TagSocketRequest::DEFAULT_ROUTING:
      policy = VPNRoutingPolicy::kDefault;
      break;
    case TagSocketRequest::ROUTE_ON_VPN:
      policy = VPNRoutingPolicy::kRouteOnVPN;
      break;
    case TagSocketRequest::BYPASS_VPN:
      policy = VPNRoutingPolicy::kBypassVPN;
      break;
    default:
      LOG(ERROR) << __func__ << ": Invalid vpn policy value"
                 << in_request.vpn_policy();
      response.set_success(false);
      return response;
  }

  std::optional<TrafficAnnotationId> annotation_id;
  if (in_request.has_traffic_annotation()) {
    switch (in_request.traffic_annotation().host_id()) {
      case traffic_annotation::TrafficAnnotation::UNSPECIFIED:
        annotation_id = TrafficAnnotationId::kUnspecified;
        break;
      case traffic_annotation::TrafficAnnotation::SHILL_PORTAL_DETECTOR:
        annotation_id = TrafficAnnotationId::kShillPortalDetector;
        break;
      case traffic_annotation::TrafficAnnotation::SHILL_CAPPORT_CLIENT:
        annotation_id = TrafficAnnotationId::kShillCapportClient;
        break;
      case traffic_annotation::TrafficAnnotation::SHILL_CARRIER_ENTITLEMENT:
        annotation_id = TrafficAnnotationId::kShillCarrierEntitlement;
        break;
      default:
        LOG(ERROR) << __func__ << ": Invalid traffic annotation id "
                   << in_request.traffic_annotation().host_id();
        response.set_success(false);
        return response;
    }
  }

  // TODO(b/345417108): synchronize the network_ids/interfaces relation with
  // patchpanel main daemon.
  bool success = routing_svc_->TagSocket(in_socket_fd.get(), network_id, policy,
                                         annotation_id);
  response.set_success(success);
  return response;
}

}  // namespace patchpanel
