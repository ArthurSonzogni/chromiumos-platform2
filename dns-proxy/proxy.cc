// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/proxy.h"

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#include <optional>
#include <set>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/rtnl_handler.h>
#include <chromeos/patchpanel/address_manager.h>
#include <chromeos/patchpanel/message_dispatcher.h>
#include <shill/dbus-constants.h>

#include "dns-proxy/ipc.pb.h"

// Using directive is necessary to have the overloaded function for socket data
// structure available.
using patchpanel::operator<<;

namespace dns_proxy {
namespace {

// The DoH provider URLs that come from Chrome may be URI templates instead.
// Per https://datatracker.ietf.org/doc/html/rfc8484#section-4.1 these will
// include the {?dns} parameter template for GET requests. These can be safely
// removed since any compliant server must support both GET and POST requests
// and this services only uses POST.
constexpr char kDNSParamTemplate[] = "{?dns}";
std::string TrimParamTemplate(const std::string& url) {
  const size_t pos = url.find(kDNSParamTemplate);
  if (pos == std::string::npos) {
    return url;
  }
  return url.substr(0, pos);
}

Metrics::ProcessType ProcessTypeOf(Proxy::Type t) {
  switch (t) {
    case Proxy::Type::kSystem:
      return Metrics::ProcessType::kProxySystem;
    case Proxy::Type::kDefault:
      return Metrics::ProcessType::kProxyDefault;
    case Proxy::Type::kARC:
      return Metrics::ProcessType::kProxyARC;
    default:
      NOTREACHED_IN_MIGRATION();
  }
}

template <typename T>
std::vector<std::string> ToStringVec(const std::vector<T>& addrs) {
  std::vector<std::string> ret;
  for (const auto& addr : addrs) {
    ret.push_back(addr.ToString());
  }
  return ret;
}

}  // namespace

constexpr base::TimeDelta kShillPropertyAttemptDelay = base::Milliseconds(200);
constexpr base::TimeDelta kRequestTimeout = base::Seconds(5);
constexpr base::TimeDelta kRequestRetryDelay = base::Milliseconds(200);

constexpr char kSystemProxyType[] = "system";
constexpr char kDefaultProxyType[] = "default";
constexpr char kARCProxyType[] = "arc";
constexpr int32_t kRequestMaxRetry = 1;
constexpr uint16_t kDefaultPort = 13568;  // port 53 in network order.

// static
const char* Proxy::TypeToString(Type t) {
  switch (t) {
    case Type::kSystem:
      return kSystemProxyType;
    case Type::kDefault:
      return kDefaultProxyType;
    case Type::kARC:
      return kARCProxyType;
    default:
      NOTREACHED_IN_MIGRATION();
  }
}

// static
std::optional<Proxy::Type> Proxy::StringToType(const std::string& s) {
  if (s == kSystemProxyType) {
    return Type::kSystem;
  }

  if (s == kDefaultProxyType) {
    return Type::kDefault;
  }

  if (s == kARCProxyType) {
    return Type::kARC;
  }

  return std::nullopt;
}

std::ostream& operator<<(std::ostream& stream, Proxy::Type type) {
  stream << Proxy::TypeToString(type);
  return stream;
}

std::ostream& operator<<(std::ostream& stream, Proxy::Options opts) {
  stream << "{" << Proxy::TypeToString(opts.type) << ":" << opts.ifname << "}";
  return stream;
}

std::ostream& operator<<(std::ostream& stream, const Proxy& proxy) {
  stream << "{" << Proxy::TypeToString(proxy.opts_.type) << ":";
  if (!proxy.opts_.ifname.empty()) {
    stream << proxy.opts_.ifname;
  } else if (proxy.device_ && !proxy.device_->ifname.empty()) {
    stream << proxy.device_->ifname;
  } else {
    stream << "_";
  }
  if (proxy.device_) {
    stream << " sid=" << proxy.device_->session_id;
  }
  return stream << "}";
}

Proxy::Proxy(const Proxy::Options& opts, int32_t fd, bool root_ns_enabled)
    : opts_(opts),
      metrics_proc_type_(ProcessTypeOf(opts_.type)),
      root_ns_enabled_(root_ns_enabled) {
  doh_config_.set_logger(
      base::BindRepeating(&Proxy::LogName, weak_factory_.GetWeakPtr()));
  if (opts_.type == Type::kSystem) {
    doh_config_.set_metrics(&metrics_);
  }

  // Set up communication with the controller process.
  msg_dispatcher_ =
      std::make_unique<patchpanel::MessageDispatcher<SubprocessMessage>>(
          base::ScopedFD(fd));
  msg_dispatcher_->RegisterFailureHandler(base::BindRepeating(
      &Proxy::OnControllerMessageFailure, weak_factory_.GetWeakPtr()));
  msg_dispatcher_->RegisterMessageHandler(base::BindRepeating(
      &Proxy::OnControllerMessage, weak_factory_.GetWeakPtr()));

  // Track IPv6 address changes.
  addr_listener_ = std::make_unique<net_base::RTNLListener>(
      net_base::RTNLHandler::kRequestAddr,
      base::BindRepeating(&Proxy::RTNLMessageHandler,
                          weak_factory_.GetWeakPtr()));
  net_base::RTNLHandler::GetInstance()->Start(RTMGRP_IPV6_IFADDR);

  // Fetch initial IPv6 address.
  auto msg = std::make_unique<net_base::RTNLMessage>(
      net_base::RTNLMessage::kTypeAddress, net_base::RTNLMessage::kModeGet,
      NLM_F_REQUEST | NLM_F_DUMP, /*seq=*/0, /*pid=*/0, /*ifindex=*/0,
      AF_INET6);
  if (!net_base::RTNLHandler::GetInstance()->SendMessage(std::move(msg),
                                                         /*msg_seq=*/nullptr)) {
    LOG(WARNING) << "Failed to send address dump message";
  }
}

// This ctor is only used for testing.
Proxy::Proxy(const Options& opts,
             std::unique_ptr<patchpanel::Client> patchpanel,
             std::unique_ptr<shill::Client> shill,
             std::unique_ptr<patchpanel::MessageDispatcher<SubprocessMessage>>
                 msg_dispatcher,
             bool root_ns_enabled)
    : opts_(opts),
      patchpanel_(std::move(patchpanel)),
      shill_(std::move(shill)),
      metrics_proc_type_(ProcessTypeOf(opts_.type)),
      root_ns_enabled_(root_ns_enabled) {
  msg_dispatcher_ = std::move(msg_dispatcher);
}

int Proxy::OnInit() {
  LOG(INFO) << *this << " Starting DNS proxy";

  /// Run after Daemon::OnInit()
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&Proxy::Setup, weak_factory_.GetWeakPtr()));
  return DBusDaemon::OnInit();
}

void Proxy::OnShutdown(int* code) {
  LOG(INFO) << *this << " Stopping DNS proxy (" << *code << ")";
  addr_listener_.reset();
  if (opts_.type == Type::kSystem) {
    ClearShillDNSProxyAddresses();
    if (msg_dispatcher_) {
      ClearIPAddressesInController();
    }
  }
}

void Proxy::Setup() {
  if (!patchpanel_) {
    patchpanel_ = patchpanel::Client::New(bus_);
  }

  if (!patchpanel_) {
    metrics_.RecordProcessEvent(
        metrics_proc_type_, Metrics::ProcessEvent::kPatchpanelNotInitialized);
    LOG(ERROR) << *this << " Failed to initialize patchpanel client";
    QuitWithExitCode(EX_UNAVAILABLE);
    return;
  }

  patchpanel_->RegisterOnAvailableCallback(
      base::BindOnce(&Proxy::OnPatchpanelReady, weak_factory_.GetWeakPtr()));
  patchpanel_->RegisterProcessChangedCallback(base::BindRepeating(
      &Proxy::OnPatchpanelReset, weak_factory_.GetWeakPtr()));
}

bool Proxy::ConnectNamespace() {
  // The default network proxy might actually be carrying Chrome, Crostini or
  // if a VPN is on, even ARC traffic, but we attribute this as as "user"
  // sourced.
  patchpanel::Client::TrafficSource traffic_source;
  switch (opts_.type) {
    case Type::kSystem:
      traffic_source = patchpanel::Client::TrafficSource::kSystem;
      break;
    case Type::kARC:
      traffic_source = patchpanel::Client::TrafficSource::kArc;
      break;
    default:
      traffic_source = patchpanel::Client::TrafficSource::kUser;
  }

  // Note that using getpid() here requires that this minijail is not creating a
  // new PID namespace.
  // The default proxy (only) needs to use the VPN, if applicable, the others
  // expressly need to avoid it.
  // TODO(b/273744897): Use the patchpanel Network id of the shill Device that
  // this Proxy is associated to. Until the Network id is available, using the
  // shill's Device kInterfaceProperty is consistent with patchpanel's tracking
  // of shill's Devices. For multiplexed Cellular interfaces, patchpanel is
  // responsible for using the correct multiplexed network interface
  // (b/273741099). Callers of ConnectNamespace are expected to use the shill's
  // Device kInterfaceProperty.
  auto res = patchpanel_->ConnectNamespace(
      getpid(), opts_.ifname, /*forward_user_traffic=*/true,
      /*route_on_vpn=*/opts_.type == Type::kDefault, traffic_source,
      /*static_ipv6=*/true);
  if (!res.first.is_valid()) {
    metrics_.RecordProcessEvent(metrics_proc_type_,
                                Metrics::ProcessEvent::kPatchpanelNoNamespace);
    LOG(ERROR) << *this << " Failed to establish private network namespace";
    return false;
  }
  ns_fd_ = std::move(res.first);
  ns_ = res.second;
  ipv4_address_ = ns_.peer_ipv4_address;
  LOG(INFO) << *this << " Successfully connected private network namespace: "
            << ns_.host_ifname << " <--> " << ns_.peer_ifname;
  return true;
}

void Proxy::OnPatchpanelReady(bool success) {
  if (!success) {
    metrics_.RecordProcessEvent(metrics_proc_type_,
                                Metrics::ProcessEvent::kPatchpanelNotReady);
    LOG(ERROR) << *this << " Failed to connect to patchpanel";
    QuitWithExitCode(EX_UNAVAILABLE);
    return;
  }

  if (root_ns_enabled_) {
    switch (opts_.type) {
      case Type::kSystem:
        ipv4_address_ = patchpanel::kDnsProxySystemIPv4Address;
        ipv6_address_ = patchpanel::kDnsProxySystemIPv6Address;
        break;
      case Type::kDefault:
        ipv4_address_ = patchpanel::kDnsProxyDefaultIPv4Address;
        ipv6_address_ = patchpanel::kDnsProxyDefaultIPv6Address;
        break;
      case Type::kARC:
        break;
    }
  } else if (!ConnectNamespace()) {
    QuitWithExitCode(EX_CANTCREAT);
    return;
  }
  initialized_ = true;

  // Now it's safe to connect shill.
  InitShill();

  // Track single-networked guests' start up and shut down for redirecting
  // traffic to the proxy.
  if (opts_.type == Type::kDefault) {
    patchpanel_->RegisterVirtualDeviceEventHandler(base::BindRepeating(
        &Proxy::OnVirtualDeviceChanged, weak_factory_.GetWeakPtr()));
  }
}

void Proxy::StartDnsRedirection(const std::string& ifname,
                                const net_base::IPAddress& addr,
                                const std::vector<std::string>& nameservers) {
  // Reset last created rules.
  sa_family_t sa_family = net_base::ToSAFamily(addr.GetFamily());
  lifeline_fds_.erase(std::make_pair(ifname, sa_family));

  patchpanel::Client::DnsRedirectionRequestType type;
  switch (opts_.type) {
    case Type::kSystem:
      type = patchpanel::Client::DnsRedirectionRequestType::kExcludeDestination;
      break;
    case Type::kDefault:
      type = patchpanel::Client::DnsRedirectionRequestType::kDefault;
      // If |ifname| is empty, request SetDnsRedirectionRule rule for USER.
      if (ifname.empty()) {
        type = patchpanel::Client::DnsRedirectionRequestType::kUser;
      }
      break;
    case Type::kARC:
      type = patchpanel::Client::DnsRedirectionRequestType::kArc;
      break;
    default:
      LOG(DFATAL) << *this << " Unexpected proxy type " << opts_.type;
      return;
  }

  auto fd = patchpanel_->RedirectDns(type, ifname, addr.ToString(), nameservers,
                                     ns_.host_ifname);
  // Restart the proxy if DNS redirection rules are failed to be set up. This
  // is necessary because when DNS proxy is running, /etc/resolv.conf is
  // replaced by the IP address of system proxy. This causes non-system traffic
  // to be routed incorrectly without the redirection rules.
  if (!fd.is_valid()) {
    metrics_.RecordProcessEvent(metrics_proc_type_,
                                Metrics::ProcessEvent::kPatchpanelNoRedirect);
    LOG(ERROR) << *this << " Failed to start DNS redirection";
    QuitWithExitCode(EX_CONFIG);
    return;
  }
  lifeline_fds_.emplace(std::make_pair(ifname, sa_family), std::move(fd));
}

void Proxy::StopDnsRedirection(const std::string& ifname,
                               sa_family_t sa_family) {
  lifeline_fds_.erase(std::make_pair(ifname, sa_family));
}

void Proxy::OnPatchpanelReset(bool reset) {
  if (reset) {
    metrics_.RecordProcessEvent(metrics_proc_type_,
                                Metrics::ProcessEvent::kPatchpanelReset);
    LOG(WARNING) << *this << " Patchpanel has been reset";
    return;
  }

  // If patchpanel crashes, the proxy is useless since the connected virtual
  // network is gone. So the best bet is to exit and have the controller restart
  // us. Note if this is the system proxy, it will inform shill on shutdown.
  metrics_.RecordProcessEvent(metrics_proc_type_,
                              Metrics::ProcessEvent::kPatchpanelShutdown);
  LOG(ERROR) << *this << " Patchpanel has been shutdown - restarting DNS proxy";
  QuitWithExitCode(EX_UNAVAILABLE);
}

void Proxy::InitShill() {
  // shill_ should always be null unless a test has injected a client.
  if (!shill_) {
    shill_.reset(new shill::Client(bus_));
  }

  shill_->RegisterOnAvailableCallback(
      base::BindOnce(&Proxy::OnShillReady, weak_factory_.GetWeakPtr()));
  shill_->RegisterProcessChangedHandler(
      base::BindRepeating(&Proxy::OnShillReset, weak_factory_.GetWeakPtr()));
}

void Proxy::OnShillReady(bool success) {
  shill_ready_ = success;
  if (!shill_ready_) {
    metrics_.RecordProcessEvent(metrics_proc_type_,
                                Metrics::ProcessEvent::kShillNotReady);
    LOG(ERROR) << *this << " Failed to connect to shill";
    QuitWithExitCode(EX_UNAVAILABLE);
    return;
  }

  shill_->RegisterDefaultDeviceChangedHandler(base::BindRepeating(
      &Proxy::OnDefaultDeviceChanged, weak_factory_.GetWeakPtr()));
  shill_->RegisterDeviceChangedHandler(
      base::BindRepeating(&Proxy::OnDeviceChanged, weak_factory_.GetWeakPtr()));
  if (opts_.type == Proxy::Type::kARC) {
    for (const auto& d : shill_->GetDevices()) {
      OnDeviceChanged(d.get());
    }
  }
}

void Proxy::OnShillReset(bool reset) {
  if (reset) {
    metrics_.RecordProcessEvent(metrics_proc_type_,
                                Metrics::ProcessEvent::kShillReset);
    LOG(WARNING) << *this << " Shill has been reset";

    // If applicable, restore the address of the system proxy.
    if (opts_.type == Type::kSystem && initialized_) {
      SetShillDNSProxyAddresses(ipv4_address_, ipv6_address_);
      // Start DNS redirection rule to exclude traffic with destination not
      // equal to the underlying name server.
      if (ipv4_address_) {
        StartDnsRedirection(/*ifname=*/"", net_base::IPAddress(*ipv4_address_));
      }
      if (ipv6_address_) {
        StartDnsRedirection(/*ifname=*/"", net_base::IPAddress(*ipv6_address_));
      }
    }

    return;
  }

  metrics_.RecordProcessEvent(metrics_proc_type_,
                              Metrics::ProcessEvent::kShillShutdown);
  LOG(WARNING) << *this << " Shill has been shutdown";
  shill_ready_ = false;
  shill_props_.reset();
  shill_.reset();
  InitShill();
}

void Proxy::ApplyDeviceUpdate() {
  if (!initialized_ || !device_) {
    return;
  }

  MaybeCreateResolver();
  UpdateNameServers();

  if (opts_.type == Type::kSystem) {
    // Start DNS redirection rule to exclude traffic with destination not equal
    // to the underlying name server.
    if (ipv4_address_) {
      StartDnsRedirection(/*ifname=*/"", net_base::IPAddress(*ipv4_address_));
    }
    if (ipv6_address_) {
      StartDnsRedirection(/*ifname=*/"", net_base::IPAddress(*ipv6_address_));
    }
    return;
  }

  if (opts_.type == Type::kDefault) {
    // Start DNS redirection rule for user traffic (cups, chronos, update
    // engine, etc).
    if (ipv4_address_) {
      StartDnsRedirection(/*ifname=*/"", net_base::IPAddress(*ipv4_address_),
                          ToStringVec(doh_config_.ipv4_nameservers()));
    }
    if (ipv6_address_) {
      StartDnsRedirection(/*ifname=*/"", net_base::IPAddress(*ipv6_address_),
                          ToStringVec(doh_config_.ipv6_nameservers()));
    }
  }

  // Process the current set of patchpanel devices and add necessary
  // redirection rules.
  for (const auto& d : patchpanel_->GetDevices()) {
    StartGuestDnsRedirection(d, AF_INET);
    StartGuestDnsRedirection(d, AF_INET6);
  }
}

void Proxy::Stop() {
  doh_config_.clear();
  resolver_.reset();
  device_.reset();
  lifeline_fds_.clear();
  if (opts_.type == Type::kSystem) {
    ClearShillDNSProxyAddresses();
    ClearIPAddressesInController();
  }
}

std::unique_ptr<Resolver> Proxy::NewResolver(base::TimeDelta timeout,
                                             base::TimeDelta retry_delay,
                                             int max_num_retries) {
  // ARC proxies listen on a specific network interface. Bind the sending socket
  // to the interface.
  std::string ifname = "";
  if (root_ns_enabled_ && opts_.type == Type::kARC) {
    ifname = opts_.ifname;
  }
  return std::make_unique<Resolver>(
      base::BindRepeating(&Proxy::LogName, weak_factory_.GetWeakPtr()), ifname,
      timeout, retry_delay, max_num_retries);
}

void Proxy::OnDefaultDeviceChanged(const shill::Client::Device* const device) {
  // ARC proxies will handle changes to their network in OnDeviceChanged.
  if (opts_.type == Proxy::Type::kARC) {
    return;
  }

  // Default service is either not ready yet or has just disconnected.
  if (!device) {
    // If it disconnected, shutdown the resolver.
    if (device_) {
      LOG(WARNING) << *this
                   << " is stopping because there is no default service";
      Stop();
    }
    return;
  }

  shill::Client::Device new_default_device = *device;

  // The system proxy should ignore when a VPN is turned on as it must continue
  // to work with the underlying physical interface.
  if (opts_.type == Proxy::Type::kSystem &&
      device->type == shill::Client::Device::Type::kVPN) {
    if (device_) {
      return;
    }

    // No device means that the system proxy has started up with a VPN as the
    // default network; which means we need to dig out the physical network
    // device and use that from here forward.
    auto dd = shill_->DefaultDevice(/*exclude_vpn=*/true);
    if (!dd) {
      LOG(ERROR) << *this << " No default non-VPN device found";
      return;
    }
    new_default_device = *dd.get();
  }

  // While this is enforced in shill as well, only enable resolution if the
  // service online.
  if (new_default_device.state !=
      shill::Client::Device::ConnectionState::kOnline) {
    if (device_) {
      LOG(WARNING) << *this << " is stopping because the default device ["
                   << new_default_device.ifname << "] is offline";
      Stop();
    }
    return;
  }

  if (!device_) {
    device_ = std::make_unique<shill::Client::Device>();
  }

  // The default network has changed.
  if (new_default_device.ifname != device_->ifname) {
    LOG(INFO) << *this << " is now tracking [" << new_default_device.ifname
              << "]";
  }

  *device_.get() = new_default_device;
  ApplyDeviceUpdate();
}

shill::Client::ManagerPropertyAccessor* Proxy::shill_props() {
  if (!shill_props_) {
    shill_props_ = shill_->ManagerProperties();
    shill_props_->Watch(shill::kDNSProxyDOHProvidersProperty,
                        base::BindRepeating(&Proxy::OnDoHProvidersChanged,
                                            weak_factory_.GetWeakPtr()));
    shill_props_->Watch(shill::kDOHExcludedDomainsProperty,
                        base::BindRepeating(&Proxy::OnDoHExcludedDomainsChanged,
                                            weak_factory_.GetWeakPtr()));
    shill_props_->Watch(shill::kDOHIncludedDomainsProperty,
                        base::BindRepeating(&Proxy::OnDoHIncludedDomainsChanged,
                                            weak_factory_.GetWeakPtr()));
  }

  return shill_props_.get();
}

void Proxy::OnDeviceChanged(const shill::Client::Device* const device) {
  if (!device || (device_ && device_->ifname != device->ifname)) {
    return;
  }

  switch (opts_.type) {
    case Type::kDefault:
      // We don't need to worry about this here since the default proxy
      // always/only tracks the default device and any update will be handled by
      // OnDefaultDeviceChanged.
      return;

    case Type::kSystem:
      if (!device_ || device_->network_config == device->network_config) {
        return;
      }

      device_->network_config = device->network_config;
      UpdateNameServers();
      return;

    case Type::kARC:
      // TODO(b/273744897): Change this checks to compare the Network id
      // associated with the shill's Device (primary Network) once patchpanel
      // Network ids are available and once dnsproxy uses the patchpanel
      // Network id.
      if (opts_.ifname != device->ifname) {
        return;
      }

      if (device->state != shill::Client::Device::ConnectionState::kOnline) {
        if (device_) {
          LOG(WARNING) << *this << " is stopping because the device ["
                       << device->ifname << "] is offline";
          Stop();
        }
        return;
      }

      if (!device_) {
        device_ = std::make_unique<shill::Client::Device>();
      }

      *device_.get() = *device;
      ApplyDeviceUpdate();
      break;

    default:
      NOTREACHED_IN_MIGRATION();
  }
}

bool Proxy::Listen(struct sockaddr* addr, std::string_view ifname) {
  if (!resolver_->ListenTCP(addr, ifname)) {
    metrics_.RecordProcessEvent(
        metrics_proc_type_, Metrics::ProcessEvent::kResolverListenTCPFailure);
    LOG(ERROR) << *this << " failed to start TCP relay loop"
               << (ifname.empty() ? "" : " on interface ") << ifname;
  }
  if (!resolver_->ListenUDP(addr, ifname)) {
    metrics_.RecordProcessEvent(
        metrics_proc_type_, Metrics::ProcessEvent::kResolverListenUDPFailure);
    LOG(ERROR) << *this << " failed to start UDP relay loop"
               << (ifname.empty() ? "" : " on interface ") << ifname;
    return false;
  }
  return true;
}

void Proxy::MaybeCreateResolver() {
  if (resolver_) {
    return;
  }

  resolver_ =
      NewResolver(kRequestTimeout, kRequestRetryDelay, kRequestMaxRetry);
  doh_config_.set_resolver(resolver_.get());
  resolver_->SetDomainDoHConfigs(doh_included_domains_, doh_excluded_domains_);

  if (root_ns_enabled_) {
    // Listen on the loopback interface.
    if (ipv4_address_) {
      struct sockaddr_in addr4 = {0};
      addr4.sin_family = AF_INET;
      addr4.sin_port = kDefaultPort;
      addr4.sin_addr = ipv4_address_->ToInAddr();
      if (!Listen(reinterpret_cast<struct sockaddr*>(&addr4))) {
        QuitWithExitCode(EX_IOERR);
      }
    }
    if (ipv6_address_) {
      struct sockaddr_in6 addr6 = {0};
      addr6.sin6_family = AF_INET6;
      addr6.sin6_port = kDefaultPort;
      addr6.sin6_addr = ipv6_address_->ToIn6Addr();
      if (!Listen(reinterpret_cast<struct sockaddr*>(&addr6))) {
        QuitWithExitCode(EX_IOERR);
      }
    }
    // Listen on the virtual interfaces.
    for (const auto& d : patchpanel_->GetDevices()) {
      if (!ListenOnVirtualDevice(d, AF_INET)) {
        QuitWithExitCode(EX_IOERR);
      }
      if (!ListenOnVirtualDevice(d, AF_INET6)) {
        QuitWithExitCode(EX_IOERR);
      }
    }
  } else {
    // Listen on IPv4 and IPv6. Listening on AF_INET explicitly is not needed
    // because net.ipv6.bindv6only sysctl is defaulted to 0 and is not
    // explicitly turned on in the codebase.
    struct sockaddr_in6 addr = {0};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = kDefaultPort;
    addr.sin6_addr =
        in6addr_any;  // Since we're running in the private namespace.
    if (!Listen(reinterpret_cast<struct sockaddr*>(&addr))) {
      QuitWithExitCode(EX_IOERR);
    }
  }

  // Fetch the DoH settings.
  brillo::ErrorPtr error;
  brillo::VariantDictionary doh_providers;
  if (shill_props()->Get(shill::kDNSProxyDOHProvidersProperty, &doh_providers,
                         &error)) {
    OnDoHProvidersChanged(brillo::Any(doh_providers));
  } else {
    // Only log this metric in the system proxy to avoid replicating the data.
    if (opts_.type == Type::kSystem) {
      metrics_.RecordDnsOverHttpsMode(Metrics::DnsOverHttpsMode::kUnknown);
    }
    LOG(ERROR) << *this << " failed to obtain DoH configuration from shill: "
               << error->GetMessage();
  }
}

void Proxy::UpdateNameServers() {
  if (!device_) {
    LOG(ERROR) << *this << " updating name servers with invalid shill device";
    return;
  }

  // Use pointer to avoid unnecessary copies.
  auto* network_config = &device_->network_config;
  // Special case for VPN without nameserver. Fallback to default physical
  // network's nameserver(s).
  if (device_->type == shill::Client::Device::Type::kVPN &&
      device_->network_config.dns_servers.empty()) {
    auto dd = shill_->DefaultDevice(/*exclude_vpn=*/true);
    if (!dd) {
      LOG(ERROR) << *this << " no default non-VPN device found";
      return;
    }
    network_config = &dd->network_config;
  }

  std::vector<net_base::IPv4Address> ipv4_nameservers;
  std::vector<net_base::IPv6Address> ipv6_nameservers;

  for (const auto& addr : network_config->dns_servers) {
    switch (addr.GetFamily()) {
      case net_base::IPFamily::kIPv4:
        ipv4_nameservers.push_back(*addr.ToIPv4Address());
        break;
      case net_base::IPFamily::kIPv6:
        ipv6_nameservers.push_back(*addr.ToIPv6Address());
        break;
    }
  }

  if (ipv4_nameservers.empty() && ipv6_nameservers.empty()) {
    LOG(WARNING) << *this << " has empty name servers";
  }

  doh_config_.set_nameservers(ipv4_nameservers, ipv6_nameservers);
  metrics_.RecordNameservers(doh_config_.ipv4_nameservers().size(),
                             doh_config_.ipv6_nameservers().size());

  if (opts_.type == Type::kSystem) {
    SetShillDNSProxyAddresses(ipv4_address_, ipv6_address_);
    SendIPAddressesToController(ipv4_address_, ipv6_address_);
  }

  LOG(INFO) << *this << " applied device DNS configuration";
}

void Proxy::OnDoHProvidersChanged(const brillo::Any& value) {
  doh_config_.set_providers(value.Get<brillo::VariantDictionary>());
}

void Proxy::OnDoHExcludedDomainsChanged(const brillo::Any& value) {
  doh_excluded_domains_ = value.Get<std::vector<std::string>>();
  if (!resolver_) {
    return;
  }
  resolver_->SetDomainDoHConfigs(doh_included_domains_, doh_excluded_domains_);
}

void Proxy::OnDoHIncludedDomainsChanged(const brillo::Any& value) {
  doh_included_domains_ = value.Get<std::vector<std::string>>();
  if (!resolver_) {
    return;
  }
  resolver_->SetDomainDoHConfigs(doh_included_domains_, doh_excluded_domains_);
}

void Proxy::SetShillDNSProxyAddresses(
    const std::optional<net_base::IPv4Address>& ipv4_addr,
    const std::optional<net_base::IPv6Address>& ipv6_addr,
    bool die_on_failure,
    uint8_t num_retries) {
  if (opts_.type != Type::kSystem) {
    LOG(DFATAL) << *this << " " << __func__
                << " must be called from system proxy only";
    return;
  }

  if (num_retries == 0) {
    metrics_.RecordProcessEvent(
        metrics_proc_type_,
        Metrics::ProcessEvent::kShillSetProxyAddressRetryExceeded);
    LOG(ERROR) << *this << " Maximum number of retries exceeding attempt to"
               << " set dns-proxy address property on shill";

    if (die_on_failure) {
      QuitWithExitCode(EX_UNAVAILABLE);
    }

    return;
  }

  // If doesn't ever come back, there is no point in retrying here; and
  // if it does, then initialization process will eventually come back
  // into this function and succeed.
  if (!shill_ready_) {
    LOG(WARNING) << *this
                 << " No connection to shill - cannot set dns-proxy address "
                    "property IPv4 ["
                 << (ipv4_addr ? ipv4_addr->ToString() : "") << "], IPv6 ["
                 << (ipv6_addr ? ipv6_addr->ToString() : "") << "]";
    return;
  }

  std::vector<std::string> addrs;
  if (ipv4_addr && !doh_config_.ipv4_nameservers().empty()) {
    addrs.push_back(ipv4_addr->ToString());
  }
  if (ipv6_addr && !doh_config_.ipv6_nameservers().empty()) {
    addrs.push_back(ipv6_addr->ToString());
  }
  if (addrs.empty()) {
    shill_->GetManagerProxy()->ClearDNSProxyAddresses(/*error=*/nullptr);
    LOG(INFO) << *this << " Successfully cleared dns-proxy address property";
    return;
  }

  brillo::ErrorPtr error;
  if (shill_->GetManagerProxy()->SetDNSProxyAddresses(addrs, &error)) {
    LOG(INFO) << *this << " Successfully set dns-proxy address property ["
              << base::JoinString(addrs, ",") << "]";
    return;
  }

  LOG(ERROR) << *this << " Failed to set dns-proxy address property ["
             << base::JoinString(addrs, ",")
             << "] on shill: " << error->GetMessage() << ". Retrying...";

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Proxy::SetShillDNSProxyAddresses,
                     weak_factory_.GetWeakPtr(), ipv4_addr, ipv6_addr,
                     die_on_failure, num_retries - 1),
      kShillPropertyAttemptDelay);
}

void Proxy::ClearShillDNSProxyAddresses() {
  SetShillDNSProxyAddresses(/*ipv4_address=*/std::nullopt,
                            /*ipv6_address=*/std::nullopt);
}

void Proxy::SendIPAddressesToController(
    const std::optional<net_base::IPv4Address>& ipv4_addr,
    const std::optional<net_base::IPv6Address>& ipv6_addr) {
  if (opts_.type != Type::kSystem) {
    LOG(DFATAL) << *this << " Must be called from system proxy only";
    return;
  }

  ProxyMessage proxy_msg;
  proxy_msg.set_type(ProxyMessage::SET_ADDRS);
  if (ipv4_addr && !doh_config_.ipv4_nameservers().empty()) {
    proxy_msg.add_addrs(ipv4_addr->ToString());
  }
  if (ipv6_addr && !doh_config_.ipv6_nameservers().empty()) {
    proxy_msg.add_addrs(ipv6_addr->ToString());
  }

  // Don't send empty proxy address.
  if (proxy_msg.addrs().empty()) {
    return;
  }
  SendProxyMessage(proxy_msg);
}

void Proxy::ClearIPAddressesInController() {
  ProxyMessage proxy_msg;
  proxy_msg.set_type(ProxyMessage::CLEAR_ADDRS);
  SendProxyMessage(proxy_msg);
}

void Proxy::SendProxyMessage(const ProxyMessage& proxy_msg) {
  SubprocessMessage msg;
  *msg.mutable_proxy_message() = proxy_msg;
  if (msg_dispatcher_->SendMessage(msg)) {
    return;
  }
  LOG(ERROR) << *this << " Failed to set IP addresses to controller";
  // This might be caused by the file descriptor getting invalidated. Quit the
  // process to let the controller restart the proxy. Restarting allows a new
  // clean state.
  Quit();
}

void Proxy::OnControllerMessageFailure() {
  LOG(ERROR) << "Quitting because the parent process died";
  msg_dispatcher_.reset();
  Quit();
}

void Proxy::OnControllerMessage(const SubprocessMessage& msg) {
  if (!msg.has_controller_message()) {
    LOG(ERROR) << "Unexpected message type";
    return;
  }
  ControllerMessage controller_msg = msg.controller_message();
  if (controller_msg.type() != ControllerMessage::SHUT_DOWN) {
    LOG(ERROR) << "Unsupported controller message: " << controller_msg.type();
    return;
  }
  Quit();
}

const std::vector<net_base::IPv4Address>& Proxy::DoHConfig::ipv4_nameservers() {
  return ipv4_nameservers_;
}

const std::vector<net_base::IPv6Address>& Proxy::DoHConfig::ipv6_nameservers() {
  return ipv6_nameservers_;
}

void Proxy::DoHConfig::set_resolver(Resolver* resolver) {
  resolver_ = resolver;
  update();
}

void Proxy::DoHConfig::set_nameservers(
    const std::vector<net_base::IPv4Address>& ipv4_nameservers,
    const std::vector<net_base::IPv6Address>& ipv6_nameservers) {
  ipv4_nameservers_ = ipv4_nameservers;
  ipv6_nameservers_ = ipv6_nameservers;
  update();
}

void Proxy::DoHConfig::set_providers(
    const brillo::VariantDictionary& providers) {
  secure_providers_.clear();
  secure_providers_with_fallback_.clear();
  auto_providers_.clear();

  if (providers.empty()) {
    if (metrics_) {
      metrics_->RecordDnsOverHttpsMode(Metrics::DnsOverHttpsMode::kOff);
    }
    LOG(INFO) << *this << " DoH: off";
    update();
    return;
  }

  for (const auto& [endpoint, value] : providers) {
    // We expect that in secure, always-on to find one (or more) endpoints with
    // no nameservers.
    const auto nameservers = value.TryGet<std::string>("");
    if (nameservers.empty()) {
      secure_providers_.insert(TrimParamTemplate(endpoint));
      continue;
    }

    // On secure DNS automatic mode with fallback, we expect a wildcard
    // nameserver ("*"). See also b/333757554.
    if (nameservers == shill::kDNSProxyDOHProvidersMatchAnyIPAddress) {
      secure_providers_with_fallback_.insert(TrimParamTemplate(endpoint));
      continue;
    }

    // Remap nameserver -> secure endpoint so we can quickly determine if DoH
    // should be attempted when the name servers change.
    for (const auto& ns_str :
         base::SplitString(nameservers, ",", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY)) {
      const auto ns = net_base::IPAddress::CreateFromString(ns_str);
      if (ns) {
        auto_providers_[*ns] = TrimParamTemplate(endpoint);
      } else {
        LOG(WARNING) << "Invalid nameserver string: " << ns_str;
      }
    }
  }

  // If for some reason, both collections are non-empty, prefer the automatic
  // upgrade configuration or the secure DNS with fallback configuration.
  if (!secure_providers_with_fallback_.empty() || !auto_providers_.empty()) {
    secure_providers_.clear();
    if (metrics_) {
      metrics_->RecordDnsOverHttpsMode(Metrics::DnsOverHttpsMode::kAutomatic);
    }
    LOG(INFO) << *this << " DoH: automatic";
  }
  if (!secure_providers_.empty()) {
    if (metrics_) {
      metrics_->RecordDnsOverHttpsMode(Metrics::DnsOverHttpsMode::kAlwaysOn);
    }
    LOG(INFO) << *this << " DoH: always-on";
  }
  update();
}

void Proxy::DoHConfig::update() {
  if (!resolver_) {
    return;
  }

  std::vector<net_base::IPAddress> nameservers;
  for (const auto& ipv4_nameservers : ipv4_nameservers_) {
    nameservers.push_back(net_base::IPAddress(ipv4_nameservers));
  }
  for (const auto& ipv6_nameservers : ipv6_nameservers_) {
    nameservers.push_back(net_base::IPAddress(ipv6_nameservers));
  }
  resolver_->SetNameServers(ToStringVec(nameservers));

  std::set<std::string> doh_providers;
  bool doh_always_on = false;
  if (!secure_providers_.empty()) {
    doh_providers = secure_providers_;
    doh_always_on = true;
  } else if (!secure_providers_with_fallback_.empty()) {
    doh_providers = secure_providers_with_fallback_;
  } else if (!auto_providers_.empty()) {
    for (const auto& ns : nameservers) {
      const auto it = auto_providers_.find(ns);
      if (it != auto_providers_.end()) {
        doh_providers.emplace(it->second);
      }
    }
  }

  resolver_->SetDoHProviders(
      std::vector(doh_providers.begin(), doh_providers.end()), doh_always_on);
}

void Proxy::DoHConfig::clear() {
  resolver_ = nullptr;
  secure_providers_.clear();
  secure_providers_with_fallback_.clear();
  auto_providers_.clear();
}

void Proxy::DoHConfig::set_metrics(Metrics* metrics) {
  metrics_ = metrics;
}

void Proxy::DoHConfig::set_logger(Proxy::Logger logger) {
  logger_ = std::move(logger);
}

void Proxy::RTNLMessageHandler(const net_base::RTNLMessage& msg) {
  if (root_ns_enabled_) {
    RootNSRTNLMessageHandler(msg);
  } else {
    NetNSRTNLMessageHandler(msg);
  }
}

void Proxy::NetNSRTNLMessageHandler(const net_base::RTNLMessage& msg) {
  // Listen only for global or site-local IPv6 address changes.
  if (msg.address_status().scope != RT_SCOPE_UNIVERSE &&
      msg.address_status().scope != RT_SCOPE_SITE) {
    return;
  }

  // Listen only for the peer interface IPv6 changes.
  if (msg.interface_index() != IfNameToIndex(ns_.peer_ifname.c_str())) {
    return;
  }

  switch (msg.mode()) {
    case net_base::RTNLMessage::kModeGet:
    case net_base::RTNLMessage::kModeAdd: {
      const auto ifa_addr = msg.GetAddress();
      if (!ifa_addr || ifa_addr->GetFamily() != net_base::IPFamily::kIPv6) {
        LOG(ERROR) << *this << " RTNL message does not have valid IPv6 address";
        return;
      }

      const auto peer_ipv6_addr = ifa_addr->ToIPv6CIDR()->address();
      if (ipv6_address_ == peer_ipv6_addr) {
        return;
      }
      ipv6_address_ = peer_ipv6_addr;
      LOG(INFO) << *this << " Peer IPv6 addr updated to "
                << peer_ipv6_addr.ToString();
      if (opts_.type == Type::kDefault && device_) {
        StartDnsRedirection(/*ifname=*/"", net_base::IPAddress(*ipv6_address_),
                            ToStringVec(doh_config_.ipv6_nameservers()));
      }
      for (const auto& d : patchpanel_->GetDevices()) {
        StartGuestDnsRedirection(d, AF_INET6);
      }
      if (opts_.type == Type::kSystem && device_) {
        SetShillDNSProxyAddresses(ipv4_address_, ipv6_address_);
        SendIPAddressesToController(ipv4_address_, ipv6_address_);
        StartDnsRedirection(/*ifname=*/"", net_base::IPAddress(*ipv6_address_));
      }
      return;
    }
    case net_base::RTNLMessage::kModeDelete:
      ipv6_address_ = std::nullopt;
      LOG(INFO) << *this << " Peer IPv6 addr removed";
      if (opts_.type == Type::kDefault) {
        StopDnsRedirection(/*ifname=*/"", AF_INET6);
      }
      for (const auto& d : patchpanel_->GetDevices()) {
        StopGuestDnsRedirection(d, AF_INET6);
      }
      if (opts_.type == Type::kSystem && device_) {
        SetShillDNSProxyAddresses(/*ipv4_addr=*/ipv4_address_,
                                  /*ipv6_addr=*/std::nullopt);
        SendIPAddressesToController(/*ipv4_addr=*/ipv4_address_,
                                    /*ipv6_addr=*/std::nullopt);
        StopDnsRedirection(/*ifname=*/"", AF_INET6);
      }
      return;
    default:
      return;
  }
}

void Proxy::RootNSRTNLMessageHandler(const net_base::RTNLMessage& msg) {
  // Listen only for link-local IPv6 address changes.
  if (msg.address_status().scope != RT_SCOPE_LINK) {
    return;
  }

  uint32_t ifindex = msg.interface_index();
  switch (msg.mode()) {
    case net_base::RTNLMessage::kModeGet:
    case net_base::RTNLMessage::kModeAdd: {
      // No need to process tentative addresses.
      if (msg.address_status().flags & IFA_F_TENTATIVE) {
        return;
      }
      std::optional<net_base::IPv6Address> ipv6_addr = std::nullopt;
      const auto it = link_local_addresses_.find(ifindex);
      if (it != link_local_addresses_.end()) {
        ipv6_addr = it->second;
      }
      const auto ifa_addr = msg.GetAddress();
      if (!ifa_addr || ifa_addr->GetFamily() != net_base::IPFamily::kIPv6) {
        LOG(ERROR) << *this << " RTNL message does not have valid IPv6 address";
        return;
      }
      const auto new_ipv6_addr = ifa_addr->ToIPv6CIDR()->address();
      if (ipv6_addr && ipv6_addr == new_ipv6_addr) {
        return;
      }
      link_local_addresses_[ifindex] = new_ipv6_addr;
      for (const auto& d : patchpanel_->GetDevices()) {
        if (ifindex != IfNameToIndex(d.ifname.c_str())) {
          continue;
        }
        if (!ListenOnVirtualDevice(d, AF_INET6)) {
          QuitWithExitCode(EX_IOERR);
        }
        StartGuestDnsRedirection(d, AF_INET6);
        break;
      }
      return;
    }
    case net_base::RTNLMessage::kModeDelete:
      link_local_addresses_.erase(ifindex);
      for (const auto& d : patchpanel_->GetDevices()) {
        if (ifindex != IfNameToIndex(d.ifname.c_str())) {
          continue;
        }
        StopGuestDnsRedirection(d, AF_INET6);
        StopListenOnVirtualDevice(d, AF_INET6);
        break;
      }
      return;
    default:
      return;
  }
}

void Proxy::OnVirtualDeviceChanged(
    patchpanel::Client::VirtualDeviceEvent event,
    const patchpanel::Client::VirtualDevice& device) {
  switch (event) {
    case patchpanel::Client::VirtualDeviceEvent::kAdded:
      if (root_ns_enabled_) {
        if (!ListenOnVirtualDevice(device, AF_INET)) {
          QuitWithExitCode(EX_IOERR);
        }
        if (!ListenOnVirtualDevice(device, AF_INET6)) {
          QuitWithExitCode(EX_IOERR);
        }
      }
      StartGuestDnsRedirection(device, AF_INET);
      StartGuestDnsRedirection(device, AF_INET6);
      break;
    case patchpanel::Client::VirtualDeviceEvent::kRemoved:
      StopGuestDnsRedirection(device, AF_INET);
      StopGuestDnsRedirection(device, AF_INET6);
      if (root_ns_enabled_) {
        StopListenOnVirtualDevice(device, AF_INET);
        StopListenOnVirtualDevice(device, AF_INET6);
      }
      break;
    default:
      NOTREACHED_IN_MIGRATION();
  }
}

bool Proxy::ListenOnVirtualDevice(
    const patchpanel::Client::VirtualDevice& device, sa_family_t sa_family) {
  if (!IsValidVirtualDevice(device)) {
    return true;
  }

  if (!resolver_) {
    return true;
  }

  if (sa_family == AF_INET) {
    struct sockaddr_in addr4 = {0};
    addr4.sin_family = AF_INET;
    addr4.sin_port = kDefaultPort;
    addr4.sin_addr = device.host_ipv4_addr.ToInAddr();
    return Listen(reinterpret_cast<struct sockaddr*>(&addr4), device.ifname);
  }

  // IPv6 case.
  int ifindex = IfNameToIndex(device.ifname.c_str());
  const auto it = link_local_addresses_.find(ifindex);
  if (it == link_local_addresses_.end()) {
    return true;
  }
  struct sockaddr_in6 addr6 = {0};
  addr6.sin6_family = AF_INET6;
  addr6.sin6_port = kDefaultPort;
  addr6.sin6_addr = it->second.ToIn6Addr();
  addr6.sin6_scope_id = ifindex;
  return Listen(reinterpret_cast<struct sockaddr*>(&addr6), device.ifname);
}

void Proxy::StopListenOnVirtualDevice(
    const patchpanel::Client::VirtualDevice& device, sa_family_t sa_family) {
  if (!IsValidVirtualDevice(device)) {
    return;
  }
  if (!resolver_) {
    return;
  }
  resolver_->StopListen(sa_family, device.ifname);
}

void Proxy::StartGuestDnsRedirection(
    const patchpanel::Client::VirtualDevice& device, sa_family_t sa_family) {
  if (!IsValidVirtualDevice(device)) {
    return;
  }
  if (!device_ ||
      base::Contains(lifeline_fds_, std::make_pair(device.ifname, sa_family))) {
    return;
  }

  if (root_ns_enabled_) {
    if (sa_family == AF_INET) {
      StartDnsRedirection(device.ifname,
                          net_base::IPAddress(device.host_ipv4_addr));
    }
    if (sa_family == AF_INET6) {
      uint32_t ifindex = IfNameToIndex(device.ifname.c_str());
      const auto it = link_local_addresses_.find(ifindex);
      if (it != link_local_addresses_.end()) {
        StartDnsRedirection(device.ifname, net_base::IPAddress(it->second));
      }
    }
  } else {
    if (sa_family == AF_INET && ipv4_address_) {
      StartDnsRedirection(device.ifname, net_base::IPAddress(*ipv4_address_));
    }
    if (sa_family == AF_INET6 && ipv6_address_) {
      StartDnsRedirection(device.ifname, net_base::IPAddress(*ipv6_address_));
    }
  }
}

void Proxy::StopGuestDnsRedirection(
    const patchpanel::Client::VirtualDevice& device, sa_family_t sa_family) {
  if (!IsValidVirtualDevice(device)) {
    return;
  }
  // For ARC, upon removal of the virtual device, the corresponding proxy
  // will also be removed. This will undo the created firewall rules.
  // However, if IPv6 is removed, firewall rules created need to be
  // removed.
  StopDnsRedirection(device.ifname, sa_family);
}

bool Proxy::IsValidVirtualDevice(
    const patchpanel::Client::VirtualDevice& device) const {
  switch (device.guest_type) {
    case patchpanel::Client::GuestType::kTerminaVm:
    case patchpanel::Client::GuestType::kParallelsVm:
      return opts_.type == Type::kDefault;
    case patchpanel::Client::GuestType::kArcContainer:
    case patchpanel::Client::GuestType::kArcVm:
      return opts_.type == Type::kARC && opts_.ifname == device.phys_ifname;
    default:
      return false;
  }
}

int Proxy::IfNameToIndex(const char* ifname) {
  uint32_t ifindex = if_nametoindex(ifname);
  if (ifindex > INT_MAX) {
    errno = EINVAL;
    return 0;
  }
  return static_cast<int>(ifindex);
}

void Proxy::LogName(std::ostream& stream) const {
  stream << *this;
}

}  // namespace dns_proxy
