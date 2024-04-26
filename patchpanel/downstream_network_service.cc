// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/downstream_network_service.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/mac_address.h>

#include "patchpanel/counters_service.h"
#include "patchpanel/datapath.h"
#include "patchpanel/dhcp_server_controller.h"
#include "patchpanel/downstream_network_info.h"
#include "patchpanel/forwarding_service.h"
#include "patchpanel/guest_ipv6_service.h"
#include "patchpanel/lifeline_fd_service.h"
#include "patchpanel/metrics.h"
#include "patchpanel/proto_utils.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/rtnl_client.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/system.h"

namespace patchpanel {

DownstreamNetworkService::DownstreamNetworkService(
    MetricsLibraryInterface* metrics,
    System* system,
    Datapath* datapath,
    RoutingService* routing_svc,
    ForwardingService* forwarding_svc,
    RTNLClient* rtnl_client,
    LifelineFDService* lifeline_fd_svc,
    ShillClient* shill_client,
    GuestIPv6Service* ipv6_svc,
    CountersService* counters_svc)
    : metrics_(metrics),
      system_(system),
      datapath_(datapath),
      routing_svc_(routing_svc),
      forwarding_svc_(forwarding_svc),
      rtnl_client_(rtnl_client),
      lifeline_fd_svc_(lifeline_fd_svc),
      shill_client_(shill_client),
      ipv6_svc_(ipv6_svc),
      counters_svc_(counters_svc) {}

DownstreamNetworkService::~DownstreamNetworkService() {
  Stop();
}

patchpanel::TetheredNetworkResponse
DownstreamNetworkService::CreateTetheredNetwork(
    const patchpanel::TetheredNetworkRequest& request,
    base::ScopedFD client_fd) {
  patchpanel::TetheredNetworkResponse response;

  // b/273741099, b/293964582: patchpanel must support callers using either
  // the shill Device kInterfaceProperty value (Cellular multiplexing
  // disabled) or the kPrimaryMultiplexedInterfaceProperty value (Cellular
  // multiplexing enabled). This can be achieved by comparing the interface
  // name specified by the request for the upstream network with the
  // |ifname| value of the ShillClient's Devices.
  std::optional<ShillClient::Device> upstream_shill_device = std::nullopt;
  for (const auto& shill_device : shill_client_->GetDevices()) {
    if (shill_device.ifname == request.upstream_ifname()) {
      upstream_shill_device = shill_device;
      break;
    }
  }
  if (!upstream_shill_device) {
    // b/294287313: if the tethering request is asking for a multiplexed PDN
    // request, ShillClient has no knowledge of the associated Network as
    // there are no shill Device associated with the Network. If the network
    // interface specified in the request exists, create a fake
    // ShillClient::Device to represent that tethering Network.
    upstream_shill_device = StartTetheringUpstreamNetwork(request);
    if (!upstream_shill_device) {
      LOG(ERROR) << "Unknown shill Device " << request.upstream_ifname();
      response.set_response_code(
          patchpanel::DownstreamNetworkResult::UPSTREAM_UNKNOWN);
      return response;
    }
  }

  std::unique_ptr<DownstreamNetworkInfo> info = DownstreamNetworkInfo::Create(
      routing_svc_->AllocateNetworkID(), request, *upstream_shill_device);
  if (!info) {
    LOG(ERROR) << __func__ << ": Invalid request";
    response.set_response_code(
        patchpanel::DownstreamNetworkResult::INVALID_REQUEST);
    return response;
  }

  auto [response_code, downstream_network] =
      HandleDownstreamNetworkInfo(std::move(client_fd), std::move(info));
  response.set_response_code(response_code);
  if (downstream_network) {
    response.set_allocated_downstream_network(downstream_network.release());
  }
  return response;
}

patchpanel::LocalOnlyNetworkResponse
DownstreamNetworkService::CreateLocalOnlyNetwork(
    const patchpanel::LocalOnlyNetworkRequest& request,
    base::ScopedFD client_fd) {
  patchpanel::LocalOnlyNetworkResponse response;

  std::unique_ptr<DownstreamNetworkInfo> info =
      DownstreamNetworkInfo::Create(routing_svc_->AllocateNetworkID(), request);
  if (!info) {
    LOG(ERROR) << __func__ << ": Invalid request";
    response.set_response_code(
        patchpanel::DownstreamNetworkResult::INVALID_REQUEST);
    return response;
  }

  auto [response_code, downstream_network] =
      HandleDownstreamNetworkInfo(std::move(client_fd), std::move(info));
  response.set_response_code(response_code);
  if (downstream_network) {
    response.set_allocated_downstream_network(downstream_network.release());
  }
  return response;
}

std::pair<DownstreamNetworkResult, std::unique_ptr<DownstreamNetwork>>
DownstreamNetworkService::HandleDownstreamNetworkInfo(
    base::ScopedFD client_fd, std::unique_ptr<DownstreamNetworkInfo> info) {
  if (downstream_networks_.contains(info->downstream_ifname)) {
    LOG(ERROR) << __func__ << " " << *info
               << ": DownstreamNetwork already exist";
    return {patchpanel::DownstreamNetworkResult::INTERFACE_USED, nullptr};
  }

  // Dup the caller FD to register twice to LifelineFDService, once for the
  // DownstreamNetwork request itself and once for its network_id assignment.
  base::ScopedFD client_fd_dup =
      base::ScopedFD(HANDLE_EINTR(dup(client_fd.get())));
  if (!client_fd_dup.is_valid()) {
    PLOG(ERROR) << __func__ << " " << *info << ": Cannot dup client fd";
    return {patchpanel::DownstreamNetworkResult::ERROR, nullptr};
  }

  auto cancel_lifeline_fd = lifeline_fd_svc_->AddLifelineFD(
      std::move(client_fd),
      base::BindOnce(&DownstreamNetworkService::OnDownstreamNetworkAutoclose,
                     weak_factory_.GetWeakPtr(), info->downstream_ifname));
  if (!cancel_lifeline_fd) {
    LOG(ERROR) << __func__ << " " << *info << ": Failed to create lifeline fd";
    return {patchpanel::DownstreamNetworkResult::ERROR, nullptr};
  }

  if (!routing_svc_->AssignInterfaceToNetwork(info->network_id,
                                              info->downstream_ifname,
                                              std::move(client_fd_dup))) {
    LOG(ERROR) << __func__ << " " << *info << ": Cannot assign "
               << info->downstream_ifname << " to " << info->network_id;
    return {patchpanel::DownstreamNetworkResult::INTERFACE_USED, nullptr};
  }

  if (!datapath_->StartDownstreamNetwork(*info)) {
    LOG(ERROR) << __func__ << " " << *info
               << ": Failed to configure forwarding to downstream network";
    return {patchpanel::DownstreamNetworkResult::DATAPATH_ERROR, nullptr};
  }

  // Start the DHCP server at downstream.
  if (info->enable_ipv4_dhcp) {
    if (dhcp_server_controllers_.find(info->downstream_ifname) !=
        dhcp_server_controllers_.end()) {
      LOG(ERROR) << __func__ << " " << *info
                 << ": DHCP server is already running at "
                 << info->downstream_ifname;
      return {patchpanel::DownstreamNetworkResult::INTERFACE_USED, nullptr};
    }
    const auto config = info->ToDHCPServerConfig();
    if (!config) {
      LOG(ERROR) << __func__ << " " << *info
                 << ": Failed to get DHCP server config";
      return {patchpanel::DownstreamNetworkResult::INVALID_ARGUMENT, nullptr};
    }
    auto dhcp_server_controller = std::make_unique<DHCPServerController>(
        metrics_, kTetheringDHCPServerUmaEventMetrics, info->downstream_ifname);
    // TODO(b/274722417): Handle the DHCP server exits unexpectedly.
    if (!dhcp_server_controller->Start(*config, base::DoNothing())) {
      LOG(ERROR) << __func__ << " " << *info << ": Failed to start DHCP server";
      return {patchpanel::DownstreamNetworkResult::DHCP_SERVER_FAILURE,
              nullptr};
    }
    dhcp_server_controllers_[info->downstream_ifname] =
        std::move(dhcp_server_controller);
  }

  // Start IPv6 guest service on the downstream interface if IPv6 is
  // enabled.
  // TODO(b/278966909): Prevents neighbor discovery between the downstream
  // network and other virtual guests and interfaces in the same upstream
  // group.
  if (info->enable_ipv6 && info->upstream_device) {
    forwarding_svc_->StartIPv6NDPForwarding(
        *info->upstream_device, info->downstream_ifname, info->mtu,
        CalculateDownstreamCurHopLimit(system_, info->upstream_device->ifname));
  }

  std::unique_ptr<DownstreamNetwork> downstream_network =
      std::make_unique<DownstreamNetwork>();
  FillDownstreamNetworkProto(*info, downstream_network.get());
  info->cancel_lifeline_fd = std::move(cancel_lifeline_fd);
  downstream_networks_.emplace(info->downstream_ifname, std::move(info));
  return {patchpanel::DownstreamNetworkResult::SUCCESS,
          std::move(downstream_network)};
}

void DownstreamNetworkService::OnDownstreamNetworkAutoclose(
    std::string_view downstream_ifname) {
  auto downstream_network_it = downstream_networks_.find(downstream_ifname);
  if (downstream_network_it == downstream_networks_.end()) {
    return;
  }

  const auto& info = downstream_network_it->second;
  LOG(INFO) << __func__ << ": " << *info;

  // Stop IPv6 guest service on the downstream interface if IPv6 is enabled.
  if (info->enable_ipv6 && info->upstream_device) {
    forwarding_svc_->StopIPv6NDPForwarding(*info->upstream_device,
                                           info->downstream_ifname);
  }

  // Stop the DHCP server if exists.
  // TODO(b/274998094): Currently the DHCPServerController stop the process
  // asynchronously. It might cause the new DHCPServerController creation
  // failure if the new one is created before the process terminated. We
  // should polish the termination procedure to prevent this situation.
  dhcp_server_controllers_.erase(info->downstream_ifname);

  datapath_->StopDownstreamNetwork(*info);

  // b/294287313: if the upstream network was created in an ad-hoc
  // fashion through StartTetheringUpstreamNetwork and is not managed by
  // ShillClient, the datapath tear down must also be triggered specially.
  if (info->upstream_device &&
      !shill_client_->GetDeviceByIfindex(info->upstream_device->ifindex)) {
    StopTetheringUpstreamNetwork(*info->upstream_device);
  }

  routing_svc_->ForgetNetworkID(info->network_id);
  downstream_networks_.erase(downstream_network_it);
}

patchpanel::GetDownstreamNetworkInfoResponse
DownstreamNetworkService::GetDownstreamNetworkInfo(
    std::string_view downstream_ifname) const {
  patchpanel::GetDownstreamNetworkInfoResponse response;
  const auto it = downstream_networks_.find(std::string(downstream_ifname));
  if (it == downstream_networks_.end()) {
    response.set_success(false);
    return response;
  }

  response.set_success(true);
  FillDownstreamNetworkProto(*it->second,
                             response.mutable_downstream_network());
  for (const auto& client_info : GetDownstreamClientInfo(downstream_ifname)) {
    FillNetworkClientInfoProto(client_info, response.add_clients_info());
  }
  return response;
}

std::vector<DownstreamClientInfo>
DownstreamNetworkService::GetDownstreamClientInfo(
    std::string_view downstream_ifname) const {
  const auto ifindex = system_->IfNametoindex(downstream_ifname);
  if (!ifindex) {
    LOG(WARNING) << "Failed to get index of the interface:" << downstream_ifname
                 << ", skip querying the client info";
    return {};
  }

  std::map<net_base::MacAddress,
           std::pair<net_base::IPv4Address, std::vector<net_base::IPv6Address>>>
      mac_to_ip;
  for (const auto& [ipv4_addr, mac_addr] :
       rtnl_client_->GetIPv4NeighborMacTable(ifindex)) {
    mac_to_ip[mac_addr].first = ipv4_addr;
  }
  for (const auto& [ipv6_addr, mac_addr] :
       rtnl_client_->GetIPv6NeighborMacTable(ifindex)) {
    mac_to_ip[mac_addr].second.push_back(ipv6_addr);
  }

  const auto dhcp_server_controller_iter =
      dhcp_server_controllers_.find(std::string(downstream_ifname));
  std::vector<DownstreamClientInfo> client_infos;
  for (const auto& [mac_addr, ip] : mac_to_ip) {
    std::string hostname = "";
    if (dhcp_server_controller_iter != dhcp_server_controllers_.end()) {
      hostname = dhcp_server_controller_iter->second->GetClientHostname(
          mac_addr.ToString());
    }

    client_infos.push_back(
        {mac_addr, ip.first, ip.second, hostname, /*vendor_class=*/""});
  }
  return client_infos;
}

void DownstreamNetworkService::Stop() {
  // Tear down any remaining DownstreamNetwork setup.
  std::vector<std::string> downstream_ifnames;
  for (const auto& [ifname, _] : downstream_networks_) {
    downstream_ifnames.push_back(ifname);
  }
  for (const std::string& ifname : downstream_ifnames) {
    OnDownstreamNetworkAutoclose(ifname);
  }
}

void DownstreamNetworkService::UpdateDeviceIPConfig(
    const ShillClient::Device& shill_device) {
  for (auto& [_, info] : downstream_networks_) {
    if (info->upstream_device &&
        info->upstream_device->ifname == shill_device.ifname) {
      info->upstream_device = shill_device;
    }
  }
}

std::optional<ShillClient::Device>
DownstreamNetworkService::StartTetheringUpstreamNetwork(
    const TetheredNetworkRequest& request) {
  const auto& upstream_ifname = request.upstream_ifname();
  const int ifindex = system_->IfNametoindex(upstream_ifname);
  if (ifindex < 0) {
    LOG(ERROR) << __func__ << ": unknown interface " << upstream_ifname;
    return std::nullopt;
  }

  // Assume the Network is a Cellular network, and assume there is a known
  // Cellular Device for the primary multiplexed Network already tracked by
  // ShillClient.
  ShillClient::Device upstream_network;
  for (const auto& shill_device : shill_client_->GetDevices()) {
    if (shill_device.technology == net_base::Technology::kCellular) {
      // Copy the shill Device and Service properties common to both the
      // primary multiplexed Network and the tethering Network.
      upstream_network.shill_device_interface_property =
          shill_device.shill_device_interface_property;
      upstream_network.service_path = shill_device.service_path;
      break;
    }
  }
  if (upstream_network.shill_device_interface_property.empty()) {
    LOG(ERROR) << __func__
               << ": no Cellular ShillDevice to associate with tethering "
                  "uplink interface "
               << upstream_ifname;
    return std::nullopt;
  }
  upstream_network.technology = net_base::Technology::kCellular;
  upstream_network.ifindex = ifindex;
  upstream_network.ifname = upstream_ifname;
  // b/294287313: copy the IPv6 configuration of the upstream Network
  // directly from shill's tethering request, notify GuestIPv6Service about
  // the prefix of the upstream Network, and also call
  // Datapath::StartSourceIPv6PrefixEnforcement()
  if (request.has_uplink_ipv6_config()) {
    upstream_network.ipconfig.ipv6_cidr =
        net_base::IPv6CIDR::CreateFromBytesAndPrefix(
            request.uplink_ipv6_config().uplink_ipv6_cidr().addr(),
            request.uplink_ipv6_config().uplink_ipv6_cidr().prefix_len());
    if (!upstream_network.ipconfig.ipv6_cidr) {
      LOG(WARNING) << __func__ << ": failed to parse uplink IPv6 configuration";
    }
    for (const auto& dns : request.uplink_ipv6_config().dns_servers()) {
      auto addr = net_base::IPv6Address::CreateFromBytes(dns);
      if (addr) {
        upstream_network.ipconfig.ipv6_dns_addresses.push_back(
            addr->ToString());
      }
    }
  }

  // Setup the datapath for this interface, as if the device was advertised
  // in OnShillDevicesChanged. We skip services or setup that don'tr apply
  // to cellular (multicast traffic counters) or that are not interacting
  // with the separate PDN network exclusively used for tethering
  // (ConnectNamespace, dns-proxy redirection, ArcService, CrostiniService,
  // neighbor monitoring).
  LOG(INFO) << __func__ << ": Configuring datapath for fake shill Device "
            << upstream_network << " with IPConfig "
            << upstream_network.ipconfig;
  counters_svc_->OnPhysicalDeviceAdded(upstream_ifname);
  datapath_->StartConnectionPinning(upstream_network);
  if (upstream_network.ipconfig.ipv6_cidr) {
    ipv6_svc_->OnUplinkIPv6Changed(upstream_network);
    ipv6_svc_->UpdateUplinkIPv6DNS(upstream_network);
    datapath_->StartSourceIPv6PrefixEnforcement(upstream_network);
    // TODO(b/279871350): Support prefix shorter than /64.
    const auto ipv6_prefix = GuestIPv6Service::IPAddressTo64BitPrefix(
        upstream_network.ipconfig.ipv6_cidr->address());
    datapath_->UpdateSourceEnforcementIPv6Prefix(upstream_network, ipv6_prefix);
  }

  return upstream_network;
}

void DownstreamNetworkService::StopTetheringUpstreamNetwork(
    const ShillClient::Device& upstream_network) {
  LOG(INFO) << __func__ << ": Tearing down datapath for fake shill Device "
            << upstream_network;
  ipv6_svc_->StopUplink(upstream_network);
  datapath_->StopSourceIPv6PrefixEnforcement(upstream_network);
  datapath_->StopConnectionPinning(upstream_network);
  counters_svc_->OnPhysicalDeviceRemoved(upstream_network.ifname);
  // b/305257482: Ensure that GuestIPv6Service forgets the IPv6
  // configuration of the upstream network by faking IPv6 disconnection.
  auto fake_disconneted_network = upstream_network;
  fake_disconneted_network.ipconfig.ipv6_cidr = std::nullopt;
  ipv6_svc_->OnUplinkIPv6Changed(fake_disconneted_network);
}

// static
std::optional<int> DownstreamNetworkService::CalculateDownstreamCurHopLimit(
    System* system, std::string_view upstream_iface) {
  const std::string content =
      system->SysNetGet(System::SysNet::kIPv6HopLimit, upstream_iface);
  int value = 0;
  if (!base::StringToInt(content, &value)) {
    LOG(ERROR) << "Failed to convert `" << content << "` to int";
    return std::nullopt;
  }

  // The CurHopLimit of downstream should be the value of upstream minus 1.
  value -= 1;
  if (value < 0 || value > 255) {
    LOG(ERROR) << "The value of CurHopLimit is invalid: " << value;
    return std::nullopt;
  }

  return value;
}

}  // namespace patchpanel
