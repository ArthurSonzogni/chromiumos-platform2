// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/proxy.h"

#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#include <set>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/strings/string_split.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <chromeos/patchpanel/net_util.h>
#include <shill/dbus-constants.h>

namespace dns_proxy {

constexpr base::TimeDelta kShillPropertyAttemptDelay =
    base::TimeDelta::FromMilliseconds(200);
constexpr base::TimeDelta kRequestTimeout = base::TimeDelta::FromSeconds(10000);
constexpr base::TimeDelta kRequestRetryDelay =
    base::TimeDelta::FromMilliseconds(200);

constexpr char kSystemProxyType[] = "sys";
constexpr char kDefaultProxyType[] = "def";
constexpr char kARCProxyType[] = "arc";
constexpr int32_t kRequestMaxRetry = 1;
constexpr uint16_t kDefaultPort = 13568;  // port 53 in network order.
constexpr char kIfAddrAny[] = "0.0.0.0";

// static
const char* Proxy::TypeToString(Type t) {
  switch (t) {
    case Type::kSystem:
      return kSystemProxyType;
    case Type::kDefault:
      return kDefaultProxyType;
    case Type::kARC:
      return kARCProxyType;
  }
}

// static
std::optional<Proxy::Type> Proxy::StringToType(const std::string& s) {
  if (s == kSystemProxyType)
    return Type::kSystem;

  if (s == kDefaultProxyType)
    return Type::kDefault;

  if (s == kARCProxyType)
    return Type::kARC;

  return std::nullopt;
}

std::ostream& operator<<(std::ostream& stream, Proxy::Type type) {
  stream << Proxy::TypeToString(type);
  return stream;
}

std::ostream& operator<<(std::ostream& stream, Proxy::Options opt) {
  stream << "{" << Proxy::TypeToString(opt.type) << ":" << opt.ifname << "}";
  return stream;
}

Proxy::Proxy(const Proxy::Options& opts) : opts_(opts) {}

Proxy::Proxy(const Options& opts,
             std::unique_ptr<patchpanel::Client> patchpanel,
             std::unique_ptr<shill::Client> shill)
    : opts_(opts),
      patchpanel_(std::move(patchpanel)),
      shill_(std::move(shill)) {}

Proxy::~Proxy() {
  if (bus_)
    bus_->ShutdownAndBlock();
}

int Proxy::OnInit() {
  LOG(INFO) << "Starting DNS proxy " << opts_;

  /// Run after Daemon::OnInit()
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&Proxy::Setup, weak_factory_.GetWeakPtr()));
  return DBusDaemon::OnInit();
}

void Proxy::OnShutdown(int*) {
  LOG(INFO) << "Stopping DNS proxy " << opts_;
  if (opts_.type == Type::kSystem)
    SetShillProperty("");
}

void Proxy::Setup() {
  // This is only to account for the injected client for testing.
  if (!patchpanel_)
    patchpanel_ = patchpanel::Client::New();

  CHECK(patchpanel_) << "Failed to initialize patchpanel client";

  // This is only to account for the injected client for testing.
  if (!shill_)
    shill_.reset(new shill::Client(bus_));

  patchpanel_->RegisterOnAvailableCallback(base::BindRepeating(
      &Proxy::OnPatchpanelReady, weak_factory_.GetWeakPtr()));
  patchpanel_->RegisterProcessChangedCallback(base::BindRepeating(
      &Proxy::OnPatchpanelReset, weak_factory_.GetWeakPtr()));

  shill_->RegisterOnAvailableCallback(
      base::BindOnce(&Proxy::OnShillReady, weak_factory_.GetWeakPtr()));
}

void Proxy::OnPatchpanelReady(bool success) {
  CHECK(success) << "Failed to connect to patchpanel";

  // The default network proxy might actually be carrying Chrome, Crostini or
  // if a VPN is on, even ARC traffic, but we attribute this as as "user"
  // sourced.
  patchpanel::TrafficCounter::Source traffic_source;
  switch (opts_.type) {
    case Type::kSystem:
      traffic_source = patchpanel::TrafficCounter::SYSTEM;
      break;
    case Type::kARC:
      traffic_source = patchpanel::TrafficCounter::ARC;
      break;
    default:
      traffic_source = patchpanel::TrafficCounter::USER;
  }

  // Note that using getpid() here requires that this minijail is not creating a
  // new PID namespace.
  // The default proxy (only) needs to use the VPN, if applicable, the others
  // expressly need to avoid it.
  auto res = patchpanel_->ConnectNamespace(
      getpid(), opts_.ifname, true /* forward_user_traffic */,
      opts_.type == Type::kDefault /* route_on_vpn */, traffic_source);
  CHECK(res.first.is_valid())
      << "Failed to establish private network namespace";
  ns_fd_ = std::move(res.first);
  ns_ = res.second;
  LOG(INFO) << "Sucessfully connected private network namespace:"
            << ns_.host_ifname() << " <--> " << ns_.peer_ifname();

  // Now it's safe to register these handlers and respond to them.
  shill_->RegisterDefaultDeviceChangedHandler(base::BindRepeating(
      &Proxy::OnDefaultDeviceChanged, weak_factory_.GetWeakPtr()));
  shill_->RegisterDeviceChangedHandler(
      base::BindRepeating(&Proxy::OnDeviceChanged, weak_factory_.GetWeakPtr()));

  if (opts_.type == Type::kSystem)
    shill_->RegisterProcessChangedHandler(
        base::BindRepeating(&Proxy::OnShillReset, weak_factory_.GetWeakPtr()));
}

void Proxy::OnPatchpanelReset(bool reset) {
  // If patchpanel crashes, the proxy is useless since the connected virtual
  // network is gone. So the best bet is to exit and have the controller restart
  // us. Note if this is the system proxy, it will inform shill on shutdown.
  LOG(ERROR) << "Patchpanel has been shutdown - restarting DNS proxy " << opts_;
  QuitWithExitCode(EX_UNAVAILABLE);

  LOG(WARNING) << "Patchpanel has been reset";
}

void Proxy::OnShillReady(bool success) {
  CHECK(success) << "Failed to connect to shill";
  shill_->Init();
}

void Proxy::OnShillReset(bool reset) {
  if (!reset) {
    LOG(WARNING) << "Shill has been shutdown";
    // Watch for it to return.
    shill_->RegisterOnAvailableCallback(
        base::BindOnce(&Proxy::OnShillReady, weak_factory_.GetWeakPtr()));
    return;
  }

  // Really this means shill crashed. To be safe, explicitly reset the proxy
  // address. We don't want to crash on failure here because shill might still
  // have this address and try to use it. This probably redundant though with us
  // rediscovering the default device.
  // TODO(garrick): Remove this if so.
  LOG(WARNING) << "Shill has been reset";
  SetShillProperty(patchpanel::IPv4AddressToString(ns_.peer_ipv4_address()));
}

std::unique_ptr<Resolver> Proxy::NewResolver(base::TimeDelta timeout,
                                             base::TimeDelta retry_delay,
                                             int max_num_retries) {
  return std::make_unique<Resolver>(timeout, retry_delay, max_num_retries);
}

void Proxy::OnDefaultDeviceChanged(const shill::Client::Device* const device) {
  // ARC proxies will handle changes to their network in OnDeviceChanged.
  if (opts_.type == Proxy::Type::kARC)
    return;

  // Default service is either not ready yet or has just disconnected.
  if (!device) {
    // If it disconnected, shutdown the resolver.
    if (device_) {
      LOG(WARNING) << opts_
                   << " is stopping because there is no default service";
      doh_config_.clear();
      resolver_.reset();
      device_.reset();
    }
    return;
  }

  shill::Client::Device new_default_device = *device;

  // The system proxy should ignore when a VPN is turned on as it must continue
  // to work with the underlying physical interface.
  if (opts_.type == Proxy::Type::kSystem &&
      device->type == shill::Client::Device::Type::kVPN) {
    if (device_)
      return;

    // No device means that the system proxy has started up with a VPN as the
    // default network; which means we need to dig out the physical network
    // device and use that from here forward.
    auto dd = shill_->DefaultDevice(true /* exclude_vpn */);
    if (!dd) {
      LOG(ERROR) << "No default non-VPN device found";
      return;
    }
    new_default_device = *dd.get();
  }

  // While this is enforced in shill as well, only enable resolution if the
  // service online.
  if (new_default_device.state !=
      shill::Client::Device::ConnectionState::kOnline) {
    if (device_) {
      LOG(WARNING) << opts_ << " is stopping because the default device ["
                   << new_default_device.ifname << "] is offline";
      doh_config_.clear();
      resolver_.reset();
      device_.reset();
    }
    return;
  }

  if (!device_)
    device_ = std::make_unique<shill::Client::Device>();

  // The default network has changed.
  if (new_default_device.ifname != device_->ifname)
    LOG(INFO) << opts_ << " is now tracking [" << new_default_device.ifname
              << "]";

  *device_.get() = new_default_device;

  if (!resolver_) {
    resolver_ =
        NewResolver(kRequestTimeout, kRequestRetryDelay, kRequestMaxRetry);
    doh_config_.set_resolver(resolver_.get());

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = kDefaultPort;
    addr.sin_addr.s_addr =
        INADDR_ANY;  // Since we're running in the private namespace.

    CHECK(resolver_->ListenUDP(reinterpret_cast<struct sockaddr*>(&addr)))
        << opts_ << " failed to start UDP relay loop";
    LOG_IF(DFATAL,
           !resolver_->ListenTCP(reinterpret_cast<struct sockaddr*>(&addr)))
        << opts_ << " failed to start TCP relay loop";

    // Fetch the DoH settings.
    brillo::ErrorPtr error;
    brillo::VariantDictionary doh_providers;
    if (shill_props()->Get(shill::kDNSProxyDOHProvidersProperty, &doh_providers,
                           &error))
      OnDoHProvidersChanged(brillo::Any(doh_providers));
    else
      LOG(ERROR) << opts_ << " failed to obtain DoH configuration from shill: "
                 << error->GetMessage();
  }

  // Update the resolver with the latest DNS config.
  UpdateNameServers(device_->ipconfig);

  // For the system proxy, we have to tell shill about it. We should start
  // receiving DNS traffic on success. But if this fails, we don't have much
  // choice but to just crash out and try again.
  if (opts_.type == Type::kSystem)
    SetShillProperty(patchpanel::IPv4AddressToString(ns_.peer_ipv4_address()),
                     true /* die_on_failure */);
}

shill::Client::ManagerPropertyAccessor* Proxy::shill_props() {
  if (!shill_props_) {
    shill_props_ = shill_->ManagerProperties();
    shill_props_->Watch(shill::kDNSProxyDOHProvidersProperty,
                        base::BindRepeating(&Proxy::OnDoHProvidersChanged,
                                            weak_factory_.GetWeakPtr()));
  }

  return shill_props_.get();
}

void Proxy::OnDeviceChanged(const shill::Client::Device* const device) {
  // Ignore if there is no tracked device or it's different.
  if (!device || !device_ || device_->ifname != device->ifname)
    return;

  // We don't need to worry about this here since the default proxy always/only
  // tracks the default device and any update will be handled by
  // OnDefaultDeviceChanged.
  if (opts_.type == Type::kDefault)
    return;

  if (device_->ipconfig == device->ipconfig)
    return;

  UpdateNameServers(device->ipconfig);
  device_->ipconfig = device->ipconfig;
}

void Proxy::UpdateNameServers(const shill::Client::IPConfig& ipconfig) {
  auto name_servers = ipconfig.ipv4_dns_addresses;
  // Shill sometimes adds 0.0.0.0 for some reason - so strip any if so.
  name_servers.erase(
      std::remove_if(name_servers.begin(), name_servers.end(),
                     [](const std::string& s) { return s == kIfAddrAny; }),
      name_servers.end());
  name_servers.insert(name_servers.end(), ipconfig.ipv6_dns_addresses.begin(),
                      ipconfig.ipv6_dns_addresses.end());
  doh_config_.set_nameservers(name_servers);
  LOG(INFO) << opts_ << " applied device DNS configuration";
}

void Proxy::OnDoHProvidersChanged(const brillo::Any& value) {
  doh_config_.set_providers(value.Get<brillo::VariantDictionary>());
}

void Proxy::SetShillProperty(const std::string& addr,
                             bool die_on_failure,
                             uint8_t num_retries) {
  if (opts_.type != Type::kSystem) {
    LOG(DFATAL) << "Must be called from system proxy only";
    return;
  }

  if (num_retries == 0) {
    LOG(ERROR) << "Maximum number of retries exceeding attempt to"
               << " set dns-proxy address property on shill";
    CHECK(!die_on_failure);
    return;
  }

  // This can only happen if called from OnShutdown and Setup had somehow failed
  // to create the client... it's unlikely but regardless, that shill client
  // isn't coming back so there's no point in retrying anything.
  if (!shill_) {
    LOG(ERROR)
        << "No connection to shill - cannot set dns-proxy address property ["
        << addr << "].";
    return;
  }

  brillo::ErrorPtr error;
  if (shill_props()->Set(shill::kDNSProxyIPv4AddressProperty, addr, &error))
    return;

  LOG(ERROR) << "Failed to set dns-proxy address property [" << addr
             << "] on shill: " << error->GetMessage() << ". Retrying...";

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&Proxy::SetShillProperty, weak_factory_.GetWeakPtr(), addr,
                 die_on_failure, num_retries - 1),
      kShillPropertyAttemptDelay);
}

void Proxy::DoHConfig::set_resolver(Resolver* resolver) {
  resolver_ = resolver;
  update();
}

void Proxy::DoHConfig::set_nameservers(
    const std::vector<std::string>& nameservers) {
  nameservers_ = nameservers;
  update();
}

void Proxy::DoHConfig::set_providers(
    const brillo::VariantDictionary& providers) {
  secure_providers_.clear();
  auto_providers_.clear();

  if (providers.empty()) {
    LOG(INFO) << "DoH: off";
    update();
    return;
  }

  for (const auto& [endpoint, value] : providers) {
    // We expect that in secure, always-on to find one (or more) endpoints with
    // no nameservers.
    const auto nameservers = value.TryGet<std::string>("");
    if (nameservers.empty()) {
      secure_providers_.insert(endpoint);
      continue;
    }

    // Remap nameserver -> secure endpoint so we can quickly determine if DoH
    // should be attempted when the name servers change.
    for (const auto& ns :
         base::SplitString(nameservers, ",", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY)) {
      auto_providers_[ns] = endpoint;
    }
  }

  // If for some reason, both collections are non-empty, prefer the automatic
  // upgrade configuration.
  if (!auto_providers_.empty()) {
    secure_providers_.clear();
    LOG(INFO) << "DoH: automatic";
  }
  if (!secure_providers_.empty()) {
    LOG(INFO) << "DoH: always-on";
  }
  update();
}

void Proxy::DoHConfig::update() {
  if (!resolver_)
    return;

  resolver_->SetNameServers(nameservers_);

  std::set<std::string> doh_providers;
  bool doh_always_on = false;
  if (!secure_providers_.empty()) {
    doh_providers = secure_providers_;
    doh_always_on = true;
  } else if (!auto_providers_.empty()) {
    for (const auto& ns : nameservers_) {
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
  auto_providers_.clear();
}

}  // namespace dns_proxy
