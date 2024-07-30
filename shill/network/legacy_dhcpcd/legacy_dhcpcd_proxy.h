// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_PROXY_H_
#define SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_PROXY_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/net-base/process_manager.h>

#include "dhcpcd/dbus-proxies.h"
#include "shill/network/dhcp_client_proxy.h"
#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_listener.h"
#include "shill/technology.h"

namespace shill {

// The proxy for the legacy dhcpcd 7.2.5 with ChromeOS patches.
// It communiates with the dhcpcd process through the dhcpcd D-Bus API.
class LegacyDHCPCDProxy : public DHCPClientProxy {
 public:
  LegacyDHCPCDProxy(std::string_view interface,
                    DHCPClientProxy::EventHandler* handler,
                    base::ScopedClosureRunner destroy_cb);
  ~LegacyDHCPCDProxy() override;

  // Implements DHCPClientProxy.
  bool IsReady() const override;
  bool Rebind() override;
  bool Release() override;

  // Sets the |dhcp_proxy_|, called by LegacyDHCPCDProxyFactory.
  void set_dhcpcd_proxy(
      std::unique_ptr<org::chromium::dhcpcdProxy> dhcpcd_proxy) {
    dhcpcd_proxy_ = std::move(dhcpcd_proxy);
  }

  // Called by LegacyDHCPCDProxyFactory. Delegates the signals to
  // |handler_|.
  void OnDHCPEvent(EventReason reason, const KeyValueStore& configuration);

  // Gets the WeakPtr of this instance.
  base::WeakPtr<LegacyDHCPCDProxy> GetWeakPtr();

 private:
  // The dhcpcd D-Bus proxy.
  std::unique_ptr<org::chromium::dhcpcdProxy> dhcpcd_proxy_;

  // The callback that will be executed when the instance is destroyed.
  base::ScopedClosureRunner destroy_cb_;

  base::WeakPtrFactory<LegacyDHCPCDProxy> weak_ptr_factory_{this};
};

// The factory class to create LegacyDHCPCDProxy. The factory tracks all
// the alive proxy instances, and holds a LegacyDHCPCDListener that listens
// the D-Bus signal from the dhcpcd process. The listener delegates the received
// signal to the factory instance, then the factory delegates the signal to the
// corresponding proxy.
class LegacyDHCPCDProxyFactory : public DHCPClientProxyFactory {
 public:
  LegacyDHCPCDProxyFactory(
      EventDispatcher* dispatcher,
      scoped_refptr<dbus::Bus> bus,
      net_base::ProcessManager* process_manager =
          net_base::ProcessManager::GetInstance(),
      std::unique_ptr<LegacyDHCPCDListenerFactory> listener_factory =
          std::make_unique<LegacyDHCPCDListenerFactory>());
  ~LegacyDHCPCDProxyFactory() override;

  // Implements DHCPClientProxyFactory.
  // Starts the dhcpcd process and returns the LegacyDHCPCDProxy instance.
  // Set the dhcpcd D-Bus proxy to the LegacyDHCPCDProxy when the listener
  // receives the first signal from the dhcpcd process.
  std::unique_ptr<DHCPClientProxy> Create(
      std::string_view interface,
      Technology technology,
      const DHCPClientProxy::Options& options,
      DHCPClientProxy::EventHandler* handler,
      net_base::IPFamily family = net_base::IPFamily::kIPv4) override;

  void set_root_for_testing(const base::FilePath& root) { root_ = root; }

 private:
  // Stores the alive proxy and the closure that cleans up the dhcpcd process
  // when the struct is destroyed.
  struct AliveProxy {
    base::WeakPtr<LegacyDHCPCDProxy> proxy;
    base::ScopedClosureRunner clean_up_closure;
  };

  // Stops the dhcpcd process with |pid|, and clears the pid and lease files.
  void CleanUpDhcpcd(const std::string& interface,
                     DHCPClientProxy::Options options,
                     int pid);

  // The callback from ProcessManager, called when the dhcpcd process is exited.
  void OnProcessExited(int pid, int exit_status);

  // The callback from LegacyDHCPCDListener.
  void OnDHCPEvent(std::string_view service_name,
                   uint32_t pid,
                   DHCPClientProxy::EventReason reason_str,
                   const KeyValueStore& configuration);
  void OnStatusChanged(std::string_view service_name,
                       uint32_t pid,
                       LegacyDHCPCDListener::Status status);

  // The callback from LegacyDHCPCDProxy, called when the proxy instance is
  // destroyed.
  void OnProxyDestroyed(int pid);

  // Sets the D-Bus proxy to the LegacyDHCPCDProxy if the LegacyDHCPCDProxy
  // hasn't set the proxy.
  void SetDBusProxyIfPending(LegacyDHCPCDProxy* proxy,
                             std::string_view service_name,
                             int pid);

  // Gets the alive proxy by pid. Returns nullptr if the proxy is not found.
  LegacyDHCPCDProxy* GetAliveProxy(int pid) const;

  net_base::ProcessManager* process_manager_;
  scoped_refptr<dbus::Bus> bus_;
  base::FilePath root_{"/"};

  // The listener that listens the D-Bus signal from the dhcpcd process.
  std::unique_ptr<LegacyDHCPCDListener> listener_;

  // The pids of the dhcpcd processes that needs to stop manually.
  std::set<int> pids_need_to_stop_;
  // The alive proxies. If |alive_proxies_| contains a pid, then there
  // is a running dhcpcd process with the pid.
  std::map<int /*pid*/, AliveProxy> alive_proxies_;

  base::WeakPtrFactory<LegacyDHCPCDProxyFactory> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_PROXY_H_
