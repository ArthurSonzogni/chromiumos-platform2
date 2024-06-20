// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_DHCPCD_PROXY_H_
#define SHILL_NETWORK_DHCPCD_PROXY_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/net-base/process_manager.h>

#include "shill/network/dhcp_client_proxy.h"
#include "shill/technology.h"

namespace shill {

// The proxy for the latest dhcpcd.
class DHCPCDProxy : public DHCPClientProxy {
 public:
  DHCPCDProxy(net_base::ProcessManager* process_manager,
              std::string_view interface,
              DHCPClientProxy::EventHandler* handler,
              base::ScopedClosureRunner destroy_cb);
  ~DHCPCDProxy() override;

  // Implements DHCPClientProxy.
  bool IsReady() const override;
  bool Rebind() override;
  bool Release() override;

  // Gets the WeakPtr of this instance.
  base::WeakPtr<DHCPCDProxy> GetWeakPtr();

 private:
  // Runs the dhcpcd process with the arguments.
  bool RunDHCPCDWithArgs(const std::vector<std::string>& args);

  net_base::ProcessManager* process_manager_;

  // The callback that will be executed when the instance is destroyed.
  base::ScopedClosureRunner destroy_cb_;

  base::WeakPtrFactory<DHCPCDProxy> weak_ptr_factory_{this};
};

// The factory class to create DHCPCDProxy. The factory tracks all
// the alive proxy instances.
class DHCPCDProxyFactory : public DHCPClientProxyFactory {
 public:
  explicit DHCPCDProxyFactory(net_base::ProcessManager* process_manager =
                                  net_base::ProcessManager::GetInstance());
  ~DHCPCDProxyFactory() override;

  // Implements DHCPClientProxyFactory.
  // Starts the dhcpcd process and returns the DHCPCDProxy instance.
  std::unique_ptr<DHCPClientProxy> Create(
      std::string_view interface,
      Technology technology,
      const DHCPClientProxy::Options& options,
      DHCPClientProxy::EventHandler* handler) override;

  void set_root_for_testing(const base::FilePath& root) { root_ = root; }

 private:
  // Stores the alive proxy and the closure that cleans up the dhcpcd process
  // when the struct is destroyed.
  struct AliveProxy {
    base::WeakPtr<DHCPCDProxy> proxy;
    base::ScopedClosureRunner clean_up_closure;
  };

  // Stops the dhcpcd process with |pid|, and clears the pid and lease files.
  void CleanUpDhcpcd(const std::string& interface,
                     DHCPClientProxy::Options options,
                     int pid);

  // The callback from ProcessManager, called when the dhcpcd process is exited.
  void OnProcessExited(int pid, int exit_status);

  // The callback from DHCPCDProxy, called when the proxy instance is destroyed.
  void OnProxyDestroyed(int pid);

  // Gets the alive proxy by pid. Returns nullptr if the proxy is not found.
  DHCPCDProxy* GetAliveProxy(int pid) const;

  net_base::ProcessManager* process_manager_;
  base::FilePath root_{"/"};

  // The pids of the dhcpcd processes that needs to stop manually.
  std::set<int> pids_need_to_stop_;
  // The alive proxies. If |alive_proxies_| contains a pid, then there is a
  // running dhcpcd process with the pid.
  std::map<int /*pid*/, AliveProxy> alive_proxies_;

  base::WeakPtrFactory<DHCPCDProxyFactory> weak_ptr_factory_{this};
};

}  // namespace shill
#endif  // SHILL_NETWORK_DHCPCD_PROXY_H_
