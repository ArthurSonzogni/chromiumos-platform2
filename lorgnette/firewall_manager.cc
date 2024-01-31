// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/firewall_manager.h"

#include <unistd.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <brillo/errors/error.h>

#include "lorgnette/enums.h"
#include "lorgnette/scanner_match.h"

using std::string;

namespace lorgnette {

namespace {

const uint16_t kCanonBjnpPort = 8612;
const uint16_t kEpson2Port = 1865;

}  // namespace

PortToken::PortToken(base::WeakPtr<FirewallManager> firewall_manager,
                     uint16_t port)
    : firewall_manager_(firewall_manager), port_(port) {}

PortToken::PortToken(PortToken&& token) : firewall_manager_(nullptr), port_(0) {
  firewall_manager_ = token.firewall_manager_;
  port_ = token.port_;

  token.firewall_manager_ = nullptr;
  token.port_ = 0;
}

PortToken::~PortToken() {
  if (firewall_manager_)
    firewall_manager_->ReleaseUdpPortAccess(port_);
}

FirewallManager::FirewallManager(const std::string& interface)
    : interface_(interface) {}

void FirewallManager::Init(
    std::unique_ptr<org::chromium::PermissionBrokerProxyInterface>
        permission_broker_proxy) {
  CHECK(!permission_broker_proxy_) << "Already started";

  if (!SetupLifelinePipe()) {
    return;
  }

  permission_broker_proxy_ = std::move(permission_broker_proxy);

  // This will connect the name owner changed signal in DBus object proxy,
  // The callback will be invoked as soon as service is avalilable and will
  // be cleared after it is invoked. So this will be an one time callback.
  permission_broker_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(
      base::BindOnce(&FirewallManager::OnServiceAvailable,
                     weak_factory_.GetWeakPtr()));

  // This will continuously monitor the name owner of the service. However,
  // it does not connect the name owner changed signal in DBus object proxy
  // for some reason. In order to connect the name owner changed signal,
  // either WaitForServiceToBeAvaiable or ConnectToSignal need to be invoked.
  // Since we're not interested in any signals from the proxy,
  // WaitForServiceToBeAvailable is used.
  permission_broker_proxy_->GetObjectProxy()->SetNameOwnerChangedCallback(
      base::BindRepeating(&FirewallManager::OnServiceNameChanged,
                          weak_factory_.GetWeakPtr()));
}

std::vector<PortToken> FirewallManager::RequestPortsForDiscovery() {
  std::vector<PortToken> ports;
  ports.emplace_back(PortToken(RequestPixmaPortAccess()));
  ports.emplace_back(PortToken(RequestEpsonPortAccess()));

  return ports;
}

PortToken FirewallManager::RequestPixmaPortAccess() {
  // Request access for the well-known port used by the Pixma backend.
  return RequestUdpPortAccess(kCanonBjnpPort);
}

PortToken FirewallManager::RequestEpsonPortAccess() {
  // Request access for the port used by the epson2 backend.
  return RequestUdpPortAccess(kEpson2Port);
}

bool FirewallManager::SetupLifelinePipe() {
  if (lifeline_read_.is_valid()) {
    LOG(ERROR) << "Lifeline pipe already created";
    return false;
  }

  // Setup lifeline pipe.
  int fds[2];
  if (pipe(fds) != 0) {
    PLOG(ERROR) << "Failed to create lifeline pipe";
    return false;
  }
  lifeline_read_ = base::ScopedFD(fds[0]);
  lifeline_write_ = base::ScopedFD(fds[1]);

  return true;
}

void FirewallManager::OnServiceAvailable(bool service_available) {
  LOG(INFO) << "FirewallManager::OnServiceAvailable " << service_available;
  // Nothing to be done if proxy service is not available.
  if (!service_available) {
    return;
  }
  RequestAllPortsAccess();
}

void FirewallManager::OnServiceNameChanged(const string& old_owner,
                                           const string& new_owner) {
  LOG(INFO) << "FirewallManager::OnServiceNameChanged old " << old_owner
            << " new " << new_owner;
  // Nothing to be done if no owner is attached to the proxy service.
  if (new_owner.empty()) {
    return;
  }
  RequestAllPortsAccess();
}

void FirewallManager::RequestAllPortsAccess() {
  std::map<uint16_t, size_t> attempted_ports;
  attempted_ports.swap(requested_ports_);
  for (const auto& [port, count] : attempted_ports) {
    // Just perform the actual request once and then manually set the count to
    // what it should be.
    SendPortAccessRequest(port);
    requested_ports_[port] = count;
  }
}

void FirewallManager::SendPortAccessRequest(uint16_t port) {
  LOG(INFO) << "Received port access request for UDP port " << port;

  // If this port is already open just increment the count.
  auto it = requested_ports_.find(port);
  if (it != requested_ports_.end()) {
    it->second++;
    LOG(INFO) << "Port " << port
              << " already requested.  Incrementing count to " << it->second;
    return;
  }

  if (!permission_broker_proxy_) {
    requested_ports_[port]++;
    LOG(INFO) << "Permission broker does not exist (yet); adding request for "
              << "port " << port << " to queue.  Count is "
              << requested_ports_[port] << ".";
    return;
  }

  bool allowed = false;
  // Pass the read end of the pipe to permission_broker, for it to monitor this
  // process.
  brillo::ErrorPtr error;
  if (!permission_broker_proxy_->RequestUdpPortAccess(
          port, interface_, base::ScopedFD(dup(lifeline_read_.get())), &allowed,
          &error)) {
    LOG(ERROR) << "Failed to request UDP port access: " << error->GetCode()
               << " " << error->GetMessage();
    return;
  }
  if (!allowed) {
    LOG(ERROR) << "Access request for UDP port " << port << " on interface "
               << interface_ << " is denied";
    return;
  }
  requested_ports_[port]++;
  LOG(INFO) << "Access granted for UDP port " << port << " on interface "
            << interface_ << ".  Count is " << requested_ports_[port];
}

std::unique_ptr<PortToken> FirewallManager::RequestPortAccessIfNeeded(
    const std::string& device_name) {
  if (BackendFromDeviceName(device_name) != kPixma) {
    return std::unique_ptr<PortToken>();
  }

  if (ConnectionTypeForScanner(device_name) != lorgnette::CONNECTION_NETWORK) {
    return std::unique_ptr<PortToken>();
  }

  return std::make_unique<PortToken>(RequestPixmaPortAccess());
}

base::WeakPtr<FirewallManager> FirewallManager::GetWeakPtrForTesting() {
  return weak_factory_.GetWeakPtr();
}

PortToken FirewallManager::RequestUdpPortAccess(uint16_t port) {
  SendPortAccessRequest(port);
  return PortToken(weak_factory_.GetWeakPtr(), port);
}

void FirewallManager::ReleaseUdpPortAccess(uint16_t port) {
  brillo::ErrorPtr error;
  bool success;
  if (requested_ports_.find(port) == requested_ports_.end()) {
    LOG(ERROR) << "UDP access has not been requested for port: " << port;
    return;
  }

  requested_ports_[port]--;
  size_t count = requested_ports_[port];
  if (count == 0) {
    requested_ports_.erase(port);
  }

  // This port will not actually get released until all clients have requested
  // it to be released.
  if (count > 0) {
    LOG(INFO) << "Requested to release port " << port << ".  Count is " << count
              << ".";
    return;
  }

  if (!permission_broker_proxy_) {
    return;
  }

  if (!permission_broker_proxy_->ReleaseUdpPort(port, interface_, &success,
                                                &error)) {
    LOG(ERROR) << "Failed to release UDP port access: " << error->GetCode()
               << " " << error->GetMessage();
    return;
  }
  if (!success) {
    LOG(ERROR) << "Release request for UDP port " << port << " on interface "
               << interface_ << " is denied";
    return;
  }
  LOG(INFO) << "Access released for UDP port " << port << " on interface "
            << interface_ << ".  Count is 0.";
}

}  // namespace lorgnette
