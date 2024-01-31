// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_FIREWALL_MANAGER_H_
#define LORGNETTE_FIREWALL_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>

#include "permission_broker/dbus-proxies.h"

namespace lorgnette {

class FirewallManager;

// Class representing access to an open port. When it goes out of scope,
// it will release the port.
class PortToken {
 public:
  PortToken(base::WeakPtr<FirewallManager> firewall_manager, uint16_t port);
  PortToken(const PortToken&) = delete;
  PortToken& operator=(const PortToken&) = delete;
  PortToken(PortToken&&);
  ~PortToken();

 private:
  base::WeakPtr<FirewallManager> firewall_manager_;
  uint16_t port_;
};

// Class for managing required firewall rules for lorgnette.
class FirewallManager {
 public:
  explicit FirewallManager(const std::string& interface);
  FirewallManager(const FirewallManager&) = delete;
  FirewallManager& operator=(const FirewallManager&) = delete;
  virtual ~FirewallManager() = default;

  void Init(std::unique_ptr<org::chromium::PermissionBrokerProxyInterface>
                permission_broker_proxy);

  // Request port access for all ports needed during discovery.
  virtual std::vector<PortToken> RequestPortsForDiscovery();

  // Request port access for the well-known Canon scanner port.
  PortToken RequestPixmaPortAccess();

  // Request port access for the well-known Epson scanner port.
  PortToken RequestEpsonPortAccess();

  // Request UDP port access for the specified port.
  virtual PortToken RequestUdpPortAccess(uint16_t port);

  // Request port access if |device_name| corresponds to a SANE backend that
  // needs the access when connecting to a device. The caller should keep the
  // returned object alive as long as port access is needed.
  std::unique_ptr<PortToken> RequestPortAccessIfNeeded(
      const std::string& device_name);

  base::WeakPtr<FirewallManager> GetWeakPtrForTesting();

 private:
  // ReleaseUdpPortAccess() should be private so that users don't free ports
  // they didn't request, but PortToken's destructor needs access to it.
  friend PortToken::~PortToken();

  // Setup lifeline pipe to allow the remote firewall server
  // (permission_broker) to monitor this process, so it can remove the firewall
  // rules in case this process crashes.
  bool SetupLifelinePipe();

  void OnServiceAvailable(bool service_available);
  void OnServiceNameChanged(const std::string& old_owner,
                            const std::string& new_owner);

  void SendPortAccessRequest(uint16_t port);

  // This is called when a new instance of permission_broker is detected. Since
  // the new instance doesn't have any knowledge of previously port access
  // requests, re-issue those requests to permission_broker to get in sync.
  void RequestAllPortsAccess();

  virtual void ReleaseUdpPortAccess(uint16_t port);

  // DBus proxy for permission_broker.
  std::unique_ptr<org::chromium::PermissionBrokerProxyInterface>
      permission_broker_proxy_;
  // File descriptors for the two end of the pipe use for communicating with
  // remote firewall server (permission_broker), where the remote firewall
  // server will use the read end of the pipe to detect when this process exits.
  base::ScopedFD lifeline_read_;
  base::ScopedFD lifeline_write_;

  // The interface on which to request network access.
  std::string interface_;

  // A map from the requested port to the number of times that port has been
  // requested.  Used to reference count the port - the port will only get
  // closed once all requestors are done with the port.
  std::map<uint16_t, size_t> requested_ports_;

  // Keep as the last member variable.
  base::WeakPtrFactory<FirewallManager> weak_factory_{this};
};

}  // namespace lorgnette

#endif  // LORGNETTE_FIREWALL_MANAGER_H_
