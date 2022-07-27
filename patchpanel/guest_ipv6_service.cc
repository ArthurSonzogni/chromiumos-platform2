// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/guest_ipv6_service.h"

#include <set>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/notreached.h>
#include "patchpanel/ipc.pb.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {

bool IsIPv6NDProxyEnabled(ShillClient::Device::Type type) {
  static const std::set<ShillClient::Device::Type> ndproxy_allowed_types{
      ShillClient::Device::Type::kCellular,
      ShillClient::Device::Type::kEthernet,
      ShillClient::Device::Type::kEthernetEap,
      ShillClient::Device::Type::kWifi,
  };
  return ndproxy_allowed_types.find(type) != ndproxy_allowed_types.end();
}

}  // namespace

GuestIPv6Service::GuestIPv6Service(SubprocessController* nd_proxy,
                                   Datapath* datapath,
                                   ShillClient* shill_client)
    : nd_proxy_(nd_proxy), datapath_(datapath), shill_client_(shill_client) {}

void GuestIPv6Service::Start() {
  nd_proxy_->RegisterFeedbackMessageHandler(base::BindRepeating(
      &GuestIPv6Service::OnNDProxyMessage, weak_factory_.GetWeakPtr()));
  nd_proxy_->Listen();
}

void GuestIPv6Service::StartForwarding(const std::string& ifname_uplink,
                                       const std::string& ifname_downlink,
                                       bool downlink_is_tethering) {
  ControlMessage cm;
  DeviceMessage* msg = cm.mutable_device_message();
  msg->set_dev_ifname(ifname_uplink);
  msg->set_br_ifname(ifname_downlink);

  ShillClient::Device upstream_shill_device;
  shill_client_->GetDeviceProperties(ifname_uplink, &upstream_shill_device);

  // b/187462665, b/187918638: If the physical interface is a cellular
  // modem, the network connection is expected to work as a point to point
  // link where neighbor discovery of the remote gateway is not possible.
  // Therefore force guests are told to see the host as their next hop.
  if (upstream_shill_device.type == ShillClient::Device::Type::kCellular) {
    msg->set_force_local_next_hop(true);
  }

  if (IsIPv6NDProxyEnabled(upstream_shill_device.type)) {
    LOG(INFO) << "Starting IPv6 forwarding from " << ifname_uplink << " to "
              << ifname_downlink;

    if (!datapath_->MaskInterfaceFlags(ifname_uplink, IFF_ALLMULTI)) {
      LOG(WARNING) << "Failed to setup all multicast mode for interface "
                   << ifname_uplink;
    }
    if (!datapath_->MaskInterfaceFlags(ifname_downlink, IFF_ALLMULTI)) {
      LOG(WARNING) << "Failed to setup all multicast mode for interface "
                   << ifname_downlink;
    }
    nd_proxy_->SendControlMessage(cm);
  }
}

void GuestIPv6Service::StopForwarding(const std::string& ifname_uplink,
                                      const std::string& ifname_downlink) {
  ControlMessage cm;
  DeviceMessage* msg = cm.mutable_device_message();
  msg->set_dev_ifname(ifname_uplink);
  msg->set_teardown(true);
  msg->set_br_ifname(ifname_downlink);
  LOG(INFO) << "Stopping IPv6 forwarding from " << ifname_uplink << " to "
            << ifname_downlink;
  nd_proxy_->SendControlMessage(cm);
}

void GuestIPv6Service::StopUplink(const std::string& ifname_uplink) {
  ControlMessage cm;
  DeviceMessage* msg = cm.mutable_device_message();
  msg->set_dev_ifname(ifname_uplink);
  msg->set_teardown(true);
  LOG(INFO) << "Stopping IPv6 forwarding on " << ifname_uplink;
  nd_proxy_->SendControlMessage(cm);
}

void GuestIPv6Service::StartLocalHotspot(
    const std::string& ifname_hotspot_link,
    const std::string& prefix,
    const std::vector<std::string>& rdnss,
    const std::vector<std::string>& dnssl) {
  NOTIMPLEMENTED();
}

void GuestIPv6Service::StopLocalHotspot(
    const std::string& ifname_hotspot_link) {
  NOTIMPLEMENTED();
}

void GuestIPv6Service::SetForwardMethod(const std::string& ifname_uplink,
                                        ForwardMethod method) {
  NOTIMPLEMENTED();
}

void GuestIPv6Service::OnNDProxyMessage(const FeedbackMessage& fm) {
  if (!fm.has_ndproxy_message()) {
    LOG(ERROR) << "Unexpected feedback message type";
    return;
  }
  const NDProxyMessage& msg = fm.ndproxy_message();
  LOG_IF(DFATAL, msg.ifname().empty())
      << "Received DeviceMessage w/ empty dev_ifname";
  switch (msg.type()) {
    case NDProxyMessage::ADD_ROUTE:
      if (!datapath_->AddIPv6HostRoute(msg.ifname(), msg.ip6addr(), 128)) {
        LOG(WARNING) << "Failed to setup the IPv6 route for interface "
                     << msg.ifname() << ", addr " << msg.ip6addr();
      }
      break;
    case NDProxyMessage::ADD_ADDR:
      if (!datapath_->AddIPv6Address(msg.ifname(), msg.ip6addr())) {
        LOG(WARNING) << "Failed to setup the IPv6 address for interface "
                     << msg.ifname() << ", addr " << msg.ip6addr();
      }
      break;
    case NDProxyMessage::DEL_ADDR:
      datapath_->RemoveIPv6Address(msg.ifname(), msg.ip6addr());
      break;
    default:
      LOG(ERROR) << "Unknown NDProxy event " << msg.type();
      NOTREACHED();
  }
}

}  // namespace patchpanel
