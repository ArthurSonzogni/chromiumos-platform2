// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "patchpanel/qos_service.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/flat_set.h>
#include <base/containers/flat_map.h>
#include <base/strings/string_split.h>
#include <base/types/cxx23_to_underlying.h>
#include <net-base/dns_client.h>

#include "patchpanel/datapath.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/routing_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

namespace {
std::vector<std::string_view> GetHostnamesFromDoHProviders(
    const ShillClient::DoHProviders& doh_providers) {
  // Trim the "https://" prefix and the path after the hostname before
  // passing it to the iptables.
  //
  // Currently, Chrome checks that each entry must contain the "https://"
  // prefix. See net/dns/public/dns_over_https_server_config.cc:GetHttpsHost()
  // in the Chromium code. It's possible that the url may contain a port. We
  // will just ignore it since it's uncommon to use non-443 port.
  //
  // We only need a preliminary preprocessing instead of checking whether it
  // is a valid hostname carefully.
  //
  // TODO(b/299892389): Use the URL util function in net-base when it's ready.
  const auto get_hostname = [](std::string_view url) -> std::string_view {
    static constexpr std::string_view kHTTPSPrefix = "https://";
    if (!base::StartsWith(url, kHTTPSPrefix)) {
      return {};
    }
    const auto segments =
        base::SplitStringPiece(url.substr(kHTTPSPrefix.size()), "/",
                               base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
    return segments.size() > 0 ? segments[0] : "";
  };

  // Get the list of valid hostnames.
  std::vector<std::string_view> hostnames;
  for (const auto& provider : doh_providers) {
    const auto hostname = get_hostname(provider);
    if (hostname.empty()) {
      // The value can be input by users so use WARNING instead of ERROR here.
      LOG(WARNING) << "Invalid DoH provider URL: " << provider;
      continue;
    }
    hostnames.push_back(hostname);
  }
  return hostnames;
}
}  // namespace

class QoSService::DoHUpdater {
 public:
  using DNSClient = net_base::DNSClient;
  using DNSClientFactory = net_base::DNSClientFactory;

  DoHUpdater(Datapath* datapath,
             DNSClientFactory* dns_client_factory,
             const ShillClient::DoHProviders& doh_providers)
      : datapath_(datapath) {
    std::vector<std::string_view> hostnames =
        GetHostnamesFromDoHProviders(doh_providers);

    LOG(INFO) << __func__ << " called with " << hostnames.size()
              << " valid hostnames";

    // Empty list can be intentional (no DoH providers) or all the input are
    // invalid. We only need to flush the rules here.
    if (hostnames.empty()) {
      UpdateDatapath();
      return;
    }

    // Used for identifying the DNSClient objects.
    int id = 0;

    // Start a DNSClient for each hostname x {IPv4, IPv6}.
    for (const std::string_view name : hostnames) {
      for (const auto family :
           {net_base::IPFamily::kIPv4, net_base::IPFamily::kIPv6}) {
        // Unretained() is safe here since DNSClient is owned by |this| and
        // callback won't be invoked after DNSClient is destroyed.
        auto client = dns_client_factory->Resolve(
            family, name,
            base::BindOnce(&DoHUpdater::OnAddressesResolved,
                           base::Unretained(this), id, family,
                           std::string(name)),
            /*options=*/{});
        dns_clients_.emplace(id, std::move(client));
        id++;
      }
    }
  }

 private:
  void OnAddressesResolved(int id,
                           net_base::IPFamily family,
                           const std::string& hostname,
                           const DNSClient::Result& result) {
    // Remove the entry from the map. When the map is empty, it means we have
    // finished all the DNS query.
    if (dns_clients_.erase(id) != 1) {
      LOG(ERROR) << "Invalid client id: " << id;
      return;
    }

    if (result.has_value()) {
      for (const auto& ip : result.value()) {
        switch (ip.GetFamily()) {
          case net_base::IPFamily::kIPv4:
            ipv4_addrs_.insert(ip.ToIPv4Address().value());
            break;
          case net_base::IPFamily::kIPv6:
            ipv6_addrs_.insert(ip.ToIPv6Address().value());
            break;
        }
      }
    } else {
      // kNoData means there is no record (either A or AAAA) for this hostname,
      // which is expected.
      if (result.error() != DNSClient::Error::kNoData) {
        LOG(ERROR) << "Failed to resolve " << hostname << " with " << family
                   << ", error_code=" << base::to_underlying(result.error());
      }
    }

    if (!dns_clients_.empty()) {
      return;
    }

    // It can be guaranteed that we can reach here at most once for each
    // DoHUpdater object, since `dns_clients_.size()` is decreasing every time
    // this function is called.
    UpdateDatapath();
  }

  void UpdateDatapath() {
    DCHECK(dns_clients_.empty());

    LOG(INFO) << "Updating iptables rules for QoS with " << ipv4_addrs_.size()
              << " IPv4 addrs and " << ipv6_addrs_.size() << " IPv6 addrs";

    datapath_->UpdateDoHProvidersForQoS(
        IpFamily::kIPv4, {ipv4_addrs_.begin(), ipv4_addrs_.end()});
    datapath_->UpdateDoHProvidersForQoS(
        IpFamily::kIPv6, {ipv6_addrs_.begin(), ipv6_addrs_.end()});
  }

  // Keyed by a unique id.
  using DNSClientMap = base::flat_map<int, std::unique_ptr<DNSClient>>;
  DNSClientMap dns_clients_;

  // Store the resolving results we have got. Use set here to dedup the results
  // by any chance.
  base::flat_set<net_base::IPv4Address> ipv4_addrs_;
  base::flat_set<net_base::IPv6Address> ipv6_addrs_;

  Datapath* datapath_;
};

QoSService::QoSService(Datapath* datapath, ConntrackMonitor* monitor)
    : datapath_(datapath), conntrack_monitor_(monitor) {
  dns_client_factory_ = std::make_unique<net_base::DNSClientFactory>();
  process_runner_ = std::make_unique<MinijailedProcessRunner>();
}

QoSService::QoSService(
    Datapath* datapath,
    std::unique_ptr<net_base::DNSClientFactory> dns_client_factory,
    std::unique_ptr<MinijailedProcessRunner> process_runner,
    ConntrackMonitor* monitor)
    : datapath_(datapath), conntrack_monitor_(monitor) {
  dns_client_factory_ = std::move(dns_client_factory);
  process_runner_ = std::move(process_runner);
}

QoSService::~QoSService() = default;

namespace {
// TCP protocol used to set protocol field in conntrack command.
constexpr char kProtocolTCP[] = "TCP";

// UDP protocol used to set protocol field in conntrack command.
constexpr char kProtocolUDP[] = "UDP";

// Limit of how many pending UDP connections can be added to
// |pending_connections_|.
constexpr int kPendingConnectionListLimit = 128;

// Types of conntrack events QoSService gets notified.
constexpr ConntrackMonitor::EventType kConntrackEvents[] = {
    ConntrackMonitor::EventType::kNew};
}  // namespace

void QoSService::Enable() {
  if (is_enabled_) {
    return;
  }
  is_enabled_ = true;

  datapath_->EnableQoSDetection();
  for (const auto& ifname : interfaces_) {
    datapath_->EnableQoSApplyingDSCP(ifname);
  }
  listener_ = conntrack_monitor_->AddListener(
      kConntrackEvents, base::BindRepeating(&QoSService::HandleConntrackEvent,
                                            weak_factory_.GetWeakPtr()));
}

void QoSService::Disable() {
  if (!is_enabled_) {
    return;
  }
  is_enabled_ = false;

  for (const auto& ifname : interfaces_) {
    datapath_->DisableQoSApplyingDSCP(ifname);
  }
  datapath_->DisableQoSDetection();
  listener_.reset();
  pending_connections_.clear();
}

void QoSService::HandleConntrackEvent(const ConntrackMonitor::Event& event) {
  // Currently we only cares about UDP connections, see more explanation in the
  // comment of |pending_connections_|.
  if (event.proto != IPPROTO_UDP) {
    return;
  }
  UDPConnection conn = {.src_addr = event.src,
                        .dst_addr = event.dst,
                        .sport = event.sport,
                        .dport = event.dport};
  // Find the connection in |pending_connections_|, if it is in the list, try
  // updating connmark and delete the connection from the list.
  auto it = pending_connections_.find(conn);
  if (it == pending_connections_.end()) {
    return;
  }
  std::vector<std::string> args = {"-p",      kProtocolUDP,
                                   "-s",      event.src.ToString(),
                                   "-d",      event.dst.ToString(),
                                   "--sport", std::to_string(event.sport),
                                   "--dport", std::to_string(event.dport),
                                   "-m",      QoSFwmarkWithMask(it->second)};
  if (process_runner_->conntrack("-U", args) != 0) {
    LOG(ERROR) << "Updating connmark failed, deleting connection from pending "
                  "connection list.";
  }
  // Whether the update succeeded or not, there would not be another conntrack
  // event to trigger update, delete the connection from pending list.
  pending_connections_.erase(it);
}

void QoSService::OnPhysicalDeviceAdded(const ShillClient::Device& device) {
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  if (!interfaces_.insert(device.ifname).second) {
    LOG(ERROR) << "Failed to start tracking " << device.ifname;
    return;
  }
  if (!is_enabled_) {
    return;
  }
  datapath_->EnableQoSApplyingDSCP(device.ifname);
}

void QoSService::OnPhysicalDeviceRemoved(const ShillClient::Device& device) {
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  if (interfaces_.erase(device.ifname) != 1) {
    LOG(ERROR) << "Failed to stop tracking " << device.ifname;
    return;
  }
  if (!is_enabled_) {
    return;
  }
  datapath_->DisableQoSApplyingDSCP(device.ifname);
}

void QoSService::OnPhysicalDeviceDisconnected(
    const ShillClient::Device& device) {
  if (device.type != ShillClient::Device::Type::kWifi) {
    return;
  }
  // Clean up pending connections list to avoid excessive unused entries in the
  // list.
  // Currently |pending_connections_| only tracks connections on the WiFi
  // interface and we assume that there will be only one active WiFi interface
  // on the CrOS device, so we can clear this map directly here.
  pending_connections_.clear();
}

void QoSService::ProcessSocketConnectionEvent(
    const patchpanel::SocketConnectionEvent& msg) {
  if (!is_enabled_) {
    return;
  }

  const auto src_addr = net_base::IPAddress::CreateFromBytes(msg.saddr());
  if (!src_addr.has_value()) {
    LOG(ERROR) << __func__ << ": failed to convert source IP address.";
    return;
  }

  const auto dst_addr = net_base::IPAddress::CreateFromBytes(msg.daddr());
  if (!dst_addr.has_value()) {
    LOG(ERROR) << __func__ << ": failed to convert destination IP address.";
    return;
  }

  std::string proto;
  if (msg.proto() == patchpanel::SocketConnectionEvent::IpProtocol::
                         SocketConnectionEvent_IpProtocol_TCP) {
    proto = kProtocolTCP;
  } else if (msg.proto() == patchpanel::SocketConnectionEvent::IpProtocol::
                                SocketConnectionEvent_IpProtocol_UDP) {
    proto = kProtocolUDP;
  } else {
    LOG(ERROR) << __func__ << ": invalid protocol: " << msg.proto();
  }

  QoSCategory qos_category;
  if (msg.category() ==
      patchpanel::SocketConnectionEvent::QosCategory::
          SocketConnectionEvent_QosCategory_REALTIME_INTERACTIVE) {
    qos_category = QoSCategory::kRealTimeInteractive;
  } else if (msg.category() ==
             patchpanel::SocketConnectionEvent::QosCategory::
                 SocketConnectionEvent_QosCategory_MULTIMEDIA_CONFERENCING) {
    qos_category = QoSCategory::kMultimediaConferencing;
  } else {
    LOG(ERROR) << __func__ << ": invalid QoS category: " << msg.category();
  }

  if (msg.event() == patchpanel::SocketConnectionEvent::SocketEvent::
                         SocketConnectionEvent_SocketEvent_CLOSE) {
    qos_category = QoSCategory::kDefault;
  } else if (msg.event() != patchpanel::SocketConnectionEvent::SocketEvent::
                                SocketConnectionEvent_SocketEvent_OPEN) {
    LOG(ERROR) << __func__ << ": invalid socket event: " << msg.event();
  }

  // Update connmark based on QoS category or set to default connmark if socket
  // connection event is CLOSE.
  std::vector<std::string> args = {"-p",      proto,
                                   "-s",      src_addr.value().ToString(),
                                   "-d",      dst_addr.value().ToString(),
                                   "--sport", std::to_string(msg.sport()),
                                   "--dport", std::to_string(msg.dport()),
                                   "-m",      QoSFwmarkWithMask(qos_category)};
  // Only add UDP connections that failed to update connmark to pending list
  // because TCP connections are guaranteed to be established on ARC side.
  if (process_runner_->conntrack("-U", args) != 0) {
    if (proto == kProtocolTCP) {
      LOG(ERROR) << "Failed to update connmark for TCP connection.";
      return;
    }
    if (pending_connections_.size() >= kPendingConnectionListLimit) {
      LOG(WARNING) << "Failed to add UDP connection to pending connection "
                      "list, reaching limit size.";
      return;
    }
    pending_connections_.insert(
        {UDPConnection{.src_addr = *src_addr,
                       .dst_addr = *dst_addr,
                       .sport = static_cast<uint16_t>(msg.sport()),
                       .dport = static_cast<uint16_t>(msg.dport())},
         qos_category});
  }
}

void QoSService::UpdateDoHProviders(
    const ShillClient::DoHProviders& doh_providers) {
  // Notes:
  // - We don't check if QoS is enabled here to simplify the logic. The iptables
  //   rules for DoH won't have any effect when the service is not enabled.
  // - If the current |doh_updater_| is still running, this reset() will cancel
  //   it.
  doh_updater_.reset(
      new DoHUpdater(datapath_, dns_client_factory_.get(), doh_providers));
}

void QoSService::OnBorealisVMStarted(const std::string_view ifname) {
  // We don't need to check if QoS is enabled here since the iptables rules for
  // Borealis won't have any effect when the service is not enabled.
  datapath_->AddBorealisQoSRule(ifname);
}

void QoSService::OnBorealisVMStopped(const std::string_view ifname) {
  datapath_->RemoveBorealisQoSRule(ifname);
}

std::strong_ordering operator<=>(const QoSService::UDPConnection&,
                                 const QoSService::UDPConnection&) = default;

}  // namespace patchpanel
