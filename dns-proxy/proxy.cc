// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/proxy.h"

#include <sys/types.h>
#include <unistd.h>

#include <utility>

#include <base/bind.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <chromeos/patchpanel/net_util.h>
#include <shill/dbus-constants.h>

namespace dns_proxy {

constexpr base::TimeDelta kShillPropertyAttemptDelay =
    base::TimeDelta::FromMilliseconds(200);

constexpr char kSystemProxyType[] = "sys";
constexpr char kDefaultProxyType[] = "def";
constexpr char kARCProxyType[] = "arc";

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

int Proxy::OnInit() {
  LOG(INFO) << "Starting DNS proxy " << opts_;

  /// Run after Daemon::OnInit()
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(&Proxy::Setup, weak_factory_.GetWeakPtr()));
  return DBusDaemon::OnInit();
}

void Proxy::OnShutdown(int*) {
  LOG(INFO) << "Stopping DNS proxy " << opts_;
  SetShillProperty("");
}

void Proxy::Setup() {
  shill_.reset(new shill::Client(bus_));
  shill_->Init();
  shill_->RegisterDefaultServiceChangedHandler(base::BindRepeating(
      &Proxy::OnDefaultServiceChanged, weak_factory_.GetWeakPtr()));
  shill_->RegisterDefaultDeviceChangedHandler(
      base::BindRepeating(&Proxy::OnDeviceChanged, weak_factory_.GetWeakPtr(),
                          true /* is_default */));
  shill_->RegisterDeviceChangedHandler(
      base::BindRepeating(&Proxy::OnDeviceChanged, weak_factory_.GetWeakPtr(),
                          false /* is_default */));

  if (opts_.type == Type::kSystem)
    shill_->RegisterProcessChangedHandler(
        base::BindRepeating(&Proxy::OnShillReset, weak_factory_.GetWeakPtr()));

  // TODO(garrick) - add service state callback - proxy cannot run unless the
  // device's selected service is online

  patchpanel_ = patchpanel::Client::New();
  CHECK(patchpanel_) << "Failed to initialize patchpanel client";
  patchpanel_->RegisterOnAvailableCallback(base::BindRepeating(
      &Proxy::OnPatchpanelReady, weak_factory_.GetWeakPtr()));
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

  // If this is the system proxy, tell shill about it. We should start receiving
  // DNS traffic on success. If this fails, we don't have much choice but to
  // just crash out and try again...
  if (opts_.type == Type::kSystem)
    SetShillProperty(patchpanel::IPv4AddressToString(ns_.host_ipv4_address()),
                     true /* die_on_failure */);
}

void Proxy::OnShillReset(bool reset) {
  if (!reset) {
    LOG(WARNING) << "Shill has been shutdown";
    return;
  }

  // Really this means shill crashed. To be safe, explicitly set/clear the proxy
  // address.
  LOG(WARNING) << "Shill has been reset";
  std::string addr;
  if (ns_fd_.is_valid())
    addr = patchpanel::IPv4AddressToString(ns_.host_ipv4_address());

  // We don't want to crash on failure here because if
  // (a) addr is empty, it means we're not set up yet, or
  // (b) otherwise, shill might still have this address and try to use it, we
  // can hope...
  SetShillProperty(addr);
}

void Proxy::OnDefaultServiceChanged(const std::string& type) {}

void Proxy::OnDeviceChanged(bool is_default,
                            const shill::Client::Device* const device) {}

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
  if (shill_->ManagerProperties()->Set(shill::kDNSProxyIPv4AddressProperty,
                                       addr, &error))
    return;

  LOG(ERROR) << "Failed to set dns-proxy address property [" << addr
             << "] on shill: " << error->GetMessage() << ". Retrying...";

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&Proxy::SetShillProperty, weak_factory_.GetWeakPtr(), addr,
                 die_on_failure, num_retries - 1),
      kShillPropertyAttemptDelay);
}

}  // namespace dns_proxy
