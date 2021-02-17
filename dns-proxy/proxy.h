// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_PROXY_H_
#define DNS_PROXY_PROXY_H_

#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/files/scoped_file.h>
#include <brillo/daemons/dbus_daemon.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <shill/dbus/client/client.h>

#include "dns-proxy/resolver.h"

namespace dns_proxy {

// The process that runs the actual proxying code.
class Proxy : public brillo::DBusDaemon {
 public:
  enum class Type { kSystem, kDefault, kARC };

  struct Options {
    Type type;
    // Required for ARC proxies as it specifies which physical interface
    // should (always) be tracked. This field is ignored (but should be empty)
    // for the system and default network proxies.
    std::string ifname;
  };

  explicit Proxy(const Options& opts);
  // For testing.
  Proxy(const Options& opts,
        std::unique_ptr<patchpanel::Client> patchpanel,
        std::unique_ptr<shill::Client> shill);
  Proxy(const Proxy&) = delete;
  Proxy& operator=(const Proxy&) = delete;
  ~Proxy();

  static const char* TypeToString(Type t);
  static std::optional<Type> StringToType(const std::string& s);

 protected:
  int OnInit() override;
  void OnShutdown(int*) override;

  // Added for testing.
  virtual std::unique_ptr<Resolver> NewResolver(base::TimeDelta timeout,
                                                base::TimeDelta retry_delay,
                                                int max_num_retries);

 private:
  static const uint8_t kMaxShillPropertyRetries = 10;

  void Setup();
  void OnPatchpanelReady(bool success);
  void OnPatchpanelReset(bool reset);
  void OnShillReady(bool success);
  void OnShillReset(bool reset);

  // Triggered whenever the device attached to the default network changes.
  // |device| can be null and indicates the default service is disconnected.
  void OnDefaultDeviceChanged(const shill::Client::Device* const device);
  void OnDeviceChanged(const shill::Client::Device* const device);

  void UpdateNameServers(const shill::Client::IPConfig& ipconfig);

  // Helper func for setting the dns-proxy address in shill.
  // Only valid for the system proxy.
  // Will retry on failure up to |num_retries| before possibly crashing the
  // proxy.
  void SetShillProperty(const std::string& addr,
                        bool die_on_failure = false,
                        uint8_t num_retries = kMaxShillPropertyRetries);

  FRIEND_TEST(ProxyTest, SystemProxy_OnShutdownClearsAddressPropertyOnShill);
  FRIEND_TEST(ProxyTest, NonSystemProxy_OnShutdownDoesNotCallShill);
  FRIEND_TEST(ProxyTest, SystemProxy_SetShillPropertyWithNoRetriesCrashes);
  FRIEND_TEST(ProxyTest, SystemProxy_SetShillPropertyDoesntCrashIfDieFalse);
  FRIEND_TEST(ProxyTest, ShillInitializedWhenReady);
  FRIEND_TEST(ProxyTest, SystemProxy_ConnectedNamedspace);
  FRIEND_TEST(ProxyTest, DefaultProxy_ConnectedNamedspace);
  FRIEND_TEST(ProxyTest, ArcProxy_ConnectedNamedspace);
  FRIEND_TEST(ProxyTest, CrashOnConnectNamespaceFailure);
  FRIEND_TEST(ProxyTest, CrashOnPatchpanelNotReady);
  FRIEND_TEST(ProxyTest, ShillResetRestoresAddressProperty);
  FRIEND_TEST(ProxyTest, StateClearedIfDefaultServiceDrops);
  FRIEND_TEST(ProxyTest, ArcProxy_IgnoredIfDefaultServiceDrops);
  FRIEND_TEST(ProxyTest, StateClearedIfDefaultServiceIsNotOnline);
  FRIEND_TEST(ProxyTest, NewResolverStartsListeningOnDefaultServiceComesOnline);
  FRIEND_TEST(ProxyTest, CrashOnListenFailure);
  FRIEND_TEST(ProxyTest, NameServersUpdatedOnDefaultServiceComesOnline);
  FRIEND_TEST(ProxyTest,
              SystemProxy_ShillPropertyUpdatedOnDefaultServiceComesOnline);
  FRIEND_TEST(ProxyTest, SystemProxy_IgnoresVPN);
  FRIEND_TEST(ProxyTest, SystemProxy_GetsPhysicalDeviceOnInitialVPN);
  FRIEND_TEST(ProxyTest, DefaultProxy_UsesVPN);
  FRIEND_TEST(ProxyTest, NameServersUpdatedOnDeviceChangeEvent);
  FRIEND_TEST(ProxyTest, DeviceChangeEventIgnored);

  const Options opts_;
  std::unique_ptr<patchpanel::Client> patchpanel_;
  std::unique_ptr<shill::Client> shill_;

  base::ScopedFD ns_fd_;
  patchpanel::ConnectNamespaceResponse ns_;
  std::unique_ptr<Resolver> resolver_;
  std::unique_ptr<shill::Client::Device> device_;

  base::WeakPtrFactory<Proxy> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, Proxy::Type type);
std::ostream& operator<<(std::ostream& stream, Proxy::Options opt);

}  // namespace dns_proxy

#endif  // DNS_PROXY_PROXY_H_
