// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_PROXY_H_
#define DNS_PROXY_PROXY_H_

#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/files/scoped_file.h>
#include <brillo/daemons/dbus_daemon.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <shill/dbus/client/client.h>
#include <shill/net/rtnl_listener.h>

#include "dns-proxy/chrome_features_service_client.h"
#include "dns-proxy/metrics.h"
#include "dns-proxy/resolver.h"
#include "dns-proxy/session_monitor.h"

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
  ~Proxy() = default;

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

  // Helper for parsing and applying shill's DNSProxyDOHProviders property.
  class DoHConfig {
   public:
    DoHConfig() = default;
    DoHConfig(const DoHConfig&) = delete;
    DoHConfig& operator=(const DoHConfig&) = delete;
    ~DoHConfig() = default;

    // Get the name servers the network of the proxy is tracking.
    const std::vector<std::string>& ipv4_nameservers();
    const std::vector<std::string>& ipv6_nameservers();

    // Stores the resolver to configure whenever settings are updated.
    void set_resolver(Resolver* resolver);

    // |ipv4_nameservers| and |ipv6_nameservers| are the list of name servers
    // for the network the proxy is tracking.
    void set_nameservers(const std::vector<std::string>& ipv4_nameservers,
                         const std::vector<std::string>& ipv6_nameservers);

    // |settings| is the DoH providers property we get from shill. It keys, as
    // applicable, secure DNS provider endpoints to standard DNS name servers.
    void set_providers(const brillo::VariantDictionary& providers);

    void clear();

    void set_metrics(Metrics* metrics);

   private:
    void update();

    Resolver* resolver_{nullptr};
    std::vector<std::string> ipv4_nameservers_;
    std::vector<std::string> ipv6_nameservers_;
    // If non-empty, the secure providers to use for always-on DoH.
    std::set<std::string> secure_providers_;
    // If non-empty, maps name servers to secure DNS providers, for automatic
    // update.
    std::map<std::string, std::string> auto_providers_;

    Metrics* metrics_{nullptr};
  };

  void Setup();
  void OnPatchpanelReady(bool success);
  void OnPatchpanelReset(bool reset);

  void InitShill();
  void OnShillReady(bool success);
  void OnShillReset(bool reset);

  // Triggered by the session monitor whenever the user logs in or out.
  void OnSessionStateChanged(bool login);

  // Triggered by the Chrome features client in response to checking the status
  // of the DNSProxyEnabled feature value.
  void OnFeatureEnabled(base::Optional<bool> enabled);
  void Enable();
  void Disable();

  // Stops DNS proxy from proxying DNS queries. This is run whenever the device
  // is not yet online.
  void Stop();

  // Start and stop DNS redirection rules by querying patchpanel's API. This is
  // necessary to route corresponding DNS traffic to the DNS proxy.
  // |sa_family| values will be either AF_INET or AF_INET6, for IPv4 and IPv6
  // respectively.
  void StartDnsRedirection(
      const std::string& ifname,
      sa_family_t sa_family,
      const std::vector<std::string>& nameservers = std::vector<std::string>());
  void StopDnsRedirection(const std::string& ifname, sa_family_t sa_family);

  // Triggered whenever the device attached to the default network changes.
  // |device| can be null and indicates the default service is disconnected.
  void OnDefaultDeviceChanged(const shill::Client::Device* const device);
  void OnDeviceChanged(const shill::Client::Device* const device);

  void MaybeCreateResolver();
  void UpdateNameServers(const shill::Client::IPConfig& ipconfig);

  // Update DoH providers. If proxy is the default proxy and VPN is connected,
  // DoH is disabled. Force the provider to always be empty.
  void OnDoHProvidersChanged(const brillo::Any& value);

  // Notified by patchpanel whenever a change occurs in one of its virtual
  // network devices.
  void OnVirtualDeviceChanged(
      const patchpanel::NetworkDeviceChangedSignal& signal);

  // Start and stop DNS redirection rules upon virtual device changed.
  void StartGuestDnsRedirection(const patchpanel::NetworkDevice& device,
                                sa_family_t sa_family);
  void StopGuestDnsRedirection(const patchpanel::NetworkDevice& device,
                               sa_family_t sa_family);

  // Helper func for setting the dns-proxy address in shill.
  // Only valid for the system proxy.
  // Will retry on failure up to |num_retries| before possibly crashing the
  // proxy.
  void SetShillProperty(const std::string& addr,
                        bool die_on_failure = false,
                        uint8_t num_retries = kMaxShillPropertyRetries);

  // Callback from RTNetlink listener, invoked when the lan interface IPv6
  // address is changed.
  void RTNLMessageHandler(const shill::RTNLMessage& msg);

  // Return the property accessor, creating it if needed.
  shill::Client::ManagerPropertyAccessor* shill_props();

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
  FRIEND_TEST(ProxyTest, ArcProxy_NameServersUpdatedOnDeviceChangeEvent);
  FRIEND_TEST(ProxyTest, SystemProxy_NameServersUpdatedOnDeviceChangeEvent);
  FRIEND_TEST(ProxyTest, DeviceChangeEventIgnored);
  FRIEND_TEST(ProxyTest, BasicDoHDisable);
  FRIEND_TEST(ProxyTest, BasicDoHAlwaysOn);
  FRIEND_TEST(ProxyTest, BasicDoHAutomatic);
  FRIEND_TEST(ProxyTest, RemovesDNSQueryParameterTemplate_AlwaysOn);
  FRIEND_TEST(ProxyTest, RemovesDNSQueryParameterTemplate_Automatic);
  FRIEND_TEST(ProxyTest, NewResolverConfiguredWhenSet);
  FRIEND_TEST(ProxyTest, DoHModeChangingFixedNameServers);
  FRIEND_TEST(ProxyTest, MultipleDoHProvidersForAlwaysOnMode);
  FRIEND_TEST(ProxyTest, MultipleDoHProvidersForAutomaticMode);
  FRIEND_TEST(ProxyTest, DoHBadAlwaysOnConfigSetsAutomaticMode);
  FRIEND_TEST(ProxyTest, FeatureEnablementCheckedOnSetup);
  FRIEND_TEST(ProxyTest, LoginEventTriggersFeatureCheck);
  FRIEND_TEST(ProxyTest, LogoutEventTriggersDisable);
  FRIEND_TEST(ProxyTest, FeatureEnabled_LoginAfterLogout);
  FRIEND_TEST(ProxyTest, FeatureDisabled_LoginAfterLogout);
  FRIEND_TEST(ProxyTest, SystemProxy_ShillPropertyNotUpdatedIfFeatureDisabled);
  FRIEND_TEST(ProxyTest, DefaultProxy_DisableDoHProvidersOnVPN);
  FRIEND_TEST(ProxyTest, SystemProxy_NeverSetsDnsRedirectionRule);
  FRIEND_TEST(ProxyTest,
              DefaultProxy_SetDnsRedirectionRuleDeviceAlreadyStarted);
  FRIEND_TEST(ProxyTest, DefaultProxy_SetDnsRedirectionRuleNewDeviceStarted);
  FRIEND_TEST(ProxyTest, DefaultProxy_NeverSetsDnsRedirectionRuleOtherGuest);
  FRIEND_TEST(ProxyTest,
              DefaultProxy_NeverSetsDnsRedirectionRuleFeatureDisabled);
  FRIEND_TEST(ProxyTest, DefaultProxy_SetDnsRedirectionRuleWithoutIPv6);
  FRIEND_TEST(ProxyTest, DefaultProxy_SetDnsRedirectionRuleIPv6Added);
  FRIEND_TEST(ProxyTest, DefaultProxy_SetDnsRedirectionRuleIPv6Deleted);
  FRIEND_TEST(ProxyTest, DefaultProxy_SetDnsRedirectionRuleUnrelatedIPv6Added);
  FRIEND_TEST(ProxyTest, ArcProxy_SetDnsRedirectionRuleDeviceAlreadyStarted);
  FRIEND_TEST(ProxyTest, ArcProxy_SetDnsRedirectionRuleNewDeviceStarted);
  FRIEND_TEST(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleOtherIfname);
  FRIEND_TEST(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleOtherGuest);
  FRIEND_TEST(ProxyTest, ArcProxy_NeverSetsDnsRedirectionRuleFeatureDisabled);
  FRIEND_TEST(ProxyTest, ArcProxy_SetDnsRedirectionRuleIPv6Added);
  FRIEND_TEST(ProxyTest, ArcProxy_SetDnsRedirectionRuleIPv6Deleted);
  FRIEND_TEST(ProxyTest, ArcProxy_SetDnsRedirectionRuleUnrelatedIPv6Added);
  FRIEND_TEST(ProxyTest, UpdateNameServers);

  const Options opts_;
  std::unique_ptr<patchpanel::Client> patchpanel_;
  std::unique_ptr<shill::Client> shill_;
  std::unique_ptr<shill::Client::ManagerPropertyAccessor> shill_props_;
  std::unique_ptr<ChromeFeaturesServiceClient> features_;
  std::unique_ptr<SessionMonitor> session_;

  base::ScopedFD ns_fd_;
  patchpanel::ConnectNamespaceResponse ns_;
  std::string ns_peer_ipv6_address_;

  std::unique_ptr<Resolver> resolver_;
  DoHConfig doh_config_;
  std::unique_ptr<shill::Client::Device> device_;

  bool shill_ready_{false};
  bool feature_enabled_{false};

  // Mapping of interface name and socket family pair to a lifeline file
  // descriptor. These file descriptors control the lifetime of the DNS
  // redirection rules created through the patchpanel's DBus API.
  // For USER DnsRedirectionRequest, the interface name will be empty as it is
  // not needed.
  std::map<std::pair<std::string, sa_family_t>, base::ScopedFD> lifeline_fds_;

  Metrics metrics_;
  const Metrics::ProcessType metrics_proc_type_;

  // Listens for RTMGRP_IPV6_IFADDR messages and invokes RTNLMessageHandler.
  std::unique_ptr<shill::RTNLListener> addr_listener_;

  base::WeakPtrFactory<Proxy> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, Proxy::Type type);
std::ostream& operator<<(std::ostream& stream, Proxy::Options opt);

}  // namespace dns_proxy

#endif  // DNS_PROXY_PROXY_H_
