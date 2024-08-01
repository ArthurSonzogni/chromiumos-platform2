// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_H_
#define SHILL_NETWORK_NETWORK_H_

#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/observer_list.h>
#include <base/time/time.h>
#include <chromeos/net-base/http_url.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/ipv6_address.h>
#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/network_priority.h>
#include <chromeos/net-base/proc_fs_stub.h>
#include <chromeos/net-base/rtnl_handler.h>
#include <chromeos/patchpanel/dbus/client.h>

#include "shill/ipconfig.h"
#include "shill/metrics.h"
#include "shill/mockable.h"
#include "shill/network/compound_network_config.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcpv4_config.h"
#include "shill/network/network_monitor.h"
#include "shill/network/portal_detector.h"
#include "shill/network/slaac_controller.h"
#include "shill/resolver.h"
#include "shill/technology.h"

namespace shill {

class EventDispatcher;
class Service;

// TODO(b/289971126): dedup with patchpanel::NetworkApplier::Area.
enum class NetworkConfigArea : uint32_t {
  kNone = 0,
  kIPv4Address = 1u << 0,
  kIPv4Route = 1u << 1,
  kIPv4DefaultRoute = 1u << 2,
  kIPv6Address = 1u << 8,
  kIPv6Route = 1u << 9,
  kIPv6DefaultRoute = 1u << 10,
  kRoutingPolicy = 1u << 16,
  kDNS = 1u << 17,
  kMTU = 1u << 18,
  kClear = 1u << 31,
};

inline uint32_t operator&(NetworkConfigArea a, NetworkConfigArea b) {
  return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

inline NetworkConfigArea operator|(NetworkConfigArea a, NetworkConfigArea b) {
  return static_cast<NetworkConfigArea>(static_cast<uint32_t>(a) |
                                        static_cast<uint32_t>(b));
}

inline NetworkConfigArea& operator|=(NetworkConfigArea& a,
                                     NetworkConfigArea b) {
  return a = a | b;
}

// An object of Network class represents a network interface in the kernel, and
// maintains the layer 3 configuration on this interface.
class Network : public NetworkMonitor::ClientNetwork {
 public:
  // Handler of the events of the Network class, can be added to (or removed
  // from) a Network object by `RegisterEventHandler()` (or
  // `UnregisterEventHandler()`). The object implements this interface must have
  // a longer life time that the Network object, e.g., that object can be the
  // owner of this Network object. All the callbacks provide the listener with
  // the interface index where the event happened, to allow listening for events
  // in multiple Network objects at the same time.
  // TODO(b/333466169): Use |network_id| instead of |interface_index| as the
  // identifier of the network in each callback.
  class EventHandler : public base::CheckedObserver {
   public:
    // Called every time when the network config on the connection is updated.
    // When this callback is called, the Network must be in a connected state,
    // but this signal does not always indicate a change from a non-connected
    // state to a connected state.
    // TODO(b/232177767): Currently this function will not be called if there is
    // an IPv6 update when IPv4 is working.
    virtual void OnConnectionUpdated(int interface_index) {}

    // Called when the Network becomes idle from a non-idle state (configuring
    // or connected), no matter if this state change is caused by a failure
    // (e.g., DHCP failure) or a user-initiate disconnect. |is_failure|
    // indicates this failure is triggered by a DHCP failure. Note that
    // currently this is the only failure type generated inside the Network
    // class.
    virtual void OnNetworkStopped(int interface_index, bool is_failure) {}

    // The IPConfig object lists held by this Network has changed.
    virtual void OnIPConfigsPropertyUpdated(int interface_index) {}

    // Called when a new DHCPv4 lease is obtained for this device. This is
    // called before OnConnectionUpdated() is called as a result of the lease
    // acquisition.
    virtual void OnGetDHCPLease(int interface_index) {}
    // Called when DHCPv4 fails to acquire a lease.
    virtual void OnGetDHCPFailure(int interface_index) {}
    // Called on when an IPv6 address is obtained from SLAAC. SLAAC is initiated
    // by the kernel when the link is connected and is currently not monitored
    // by shill. Derived class should implement this function to listen to this
    // event. Base class does nothing. This is called before
    // OnConnectionUpdated() is called and before captive portal detection is
    // started if IPv4 is not configured.
    virtual void OnGetSLAACAddress(int interface_index) {}

    // Called after IPv4 has been configured as a result of acquiring a new DHCP
    // lease. This is called after OnGetDHCPLease, OnIPConfigsPropertyUpdated,
    // and OnConnectionUpdated.
    virtual void OnIPv4ConfiguredWithDHCPLease(int interface_index) {}
    // Called after IPv6 has been configured as a result of acquiring an IPv6
    // address from the kernel when SLAAC completes. This is called after
    // OnGetSLAACAddress, OnIPConfigsPropertyUpdated, and OnConnectionUpdated
    // (if IPv4 is not yet configured).
    virtual void OnIPv6ConfiguredWithSLAACAddress(int interface_index) {}
    // Called after shill receives a NeighborReachabilityEventSignal from
    // patchpanel's link monitor for the network interface of this Network.
    virtual void OnNeighborReachabilityEvent(
        int interface_index,
        const net_base::IPAddress& ip_address,
        patchpanel::Client::NeighborRole role,
        patchpanel::Client::NeighborStatus status) {}

    // Called with |is_failure| set to false when the NetworkMonitor has been
    // started a network validation attempt successfully. Called with
    // |is_failure| set to true if NetworkMonitor failed to start network
    // validation and there is currently no Internet connectivity information.
    // NetworkMonitor can fail to start if the Network has an incorrect
    // configuration state (no DNS, ...) and should be considered as having no
    // Internet connectivity. If network validation is used for this Service,
    // NetworkMonitor starts the first attempt when OnConnected() is called.
    // NetworkMonitor may run multiple times for the same network.
    virtual void OnNetworkValidationStart(int interface_index,
                                          bool is_failure) {}
    // Called when NetworkMonitor is stopped. If |is_failure| is false,
    // NetworkMonitor was stopped normally either by an external trigger or
    // because Internet connectivity was verified. If |is_failure| is true, the
    // Network is not able to run network validation because of an incorrect
    // configuration state (no DNS, ...) and should be considered as having no
    // Internet connectivity.
    virtual void OnNetworkValidationStop(int interface_index, bool is_failure) {
    }
    // Called when a NetworkMonitor attempt finishes and Internet
    // connectivity has been evaluated.
    virtual void OnNetworkValidationResult(
        int interface_index, const NetworkMonitor::Result& result) {}

    // Called when the Network object is about to be destroyed and become
    // invalid. Any EventHandler still registered should stop any reference
    // they hold for that Network object.
    virtual void OnNetworkDestroyed(int network_id, int interface_index) {}
  };

  // Options for starting a network.
  struct StartOptions {
    // Start DHCP client on this interface if |dhcp| is not empty.
    std::optional<DHCPController::Options> dhcp;
    // Accept router advertisements for IPv6.
    bool accept_ra = false;
    // The link local address for the device that would be an override of the
    // default EUI-64 link local address assigned by the kernel. Used in
    // cellular where the link local address is generated from the network ID
    // specified by the carrier through bearer.
    std::optional<net_base::IPv6Address> link_local_address;
    // When set to true, neighbor events from link monitoring are ignored.
    bool ignore_link_monitoring = false;
    // PortalDetector probe configuration for network validation.
    PortalDetector::ProbingConfiguration probing_configuration;
    // NetworkMonitor validation mode when the Network connection starts. The
    // owner of the Network can configure this mode later during the connection.
    NetworkMonitor::ValidationMode validation_mode =
        NetworkMonitor::ValidationMode::kFullValidation;
  };

  // State for tracking the L3 connectivity (e.g., portal state is not
  // included).
  enum class State {
    // The Network is not started.
    kIdle,
    // The Network has been started. Waiting for IP configuration provisioned.
    kConfiguring,
    // The layer 3 connectivity has been established. At least one of IPv4 and
    // IPv6 configuration has been provisioned, and the other one can still be
    // in the configuring state.
    kConnected,
  };

  // Creates a Network instance, only for testing.
  static std::unique_ptr<Network> CreateForTesting(
      int interface_index,
      std::string_view interface_name,
      Technology technology,
      bool fixed_ip_params,
      ControlInterface* control_interface,
      EventDispatcher* dispatcher,
      Metrics* metrics,
      patchpanel::Client* patchpanel_client);

  Network(const Network&) = delete;
  Network& operator=(const Network&) = delete;
  virtual ~Network();

  // Starts the network with the given |options|.
  mockable void Start(const StartOptions& options);
  // Stops the network connection. OnNetworkStopped() will be called when
  // cleaning up the network state is finished.
  mockable void Stop();

  State state() const { return state_; }

  mockable bool IsConnected() const { return state_ == State::kConnected; }

  // Return true if either 1) network is connected and network validation is
  // disabled, or 2) network validation result is present and state is
  // PortalDetector::ValidationState::kInternetConnectivity, otherwise return
  // false.
  mockable bool HasInternetConnectivity() const;

  void RegisterEventHandler(EventHandler* handler);
  void UnregisterEventHandler(EventHandler* handler);

  // Sets network config specific to technology. Currently this is used by
  // cellular and VPN.
  mockable void set_link_protocol_network_config(
      std::unique_ptr<net_base::NetworkConfig> config) {
    config_.SetFromLinkProtocol(std::move(config));
  }

  mockable int network_id() const { return network_id_; }
  int interface_index() const { return interface_index_; }
  std::string interface_name() const { return interface_name_; }
  Technology technology() const { return technology_; }

  // Interfaces between Service and Network.
  // Callback invoked when the static IP properties configured on the selected
  // service changed.
  mockable void OnStaticIPConfigChanged(const net_base::NetworkConfig& config);
  // Register a callback that gets called when the |current_ipconfig_| changed.
  // This should only be used by Service.
  void RegisterCurrentIPConfigChangeHandler(base::RepeatingClosure handler);
  // Returns the IPConfig object which is used to setup the Connection of this
  // Network. Returns nullptr if there is no such IPConfig. This is only used by
  // Service to expose its IPConfig dbus API. Other user who would like to get
  // the configuration of the Network should use GetNetworkConfig() instead.
  mockable IPConfig* GetCurrentIPConfig() const;
  // The net_base::NetworkConfig before applying the static one. Only needed by
  // Service to be exposed as a Savednet_base::NetworkConfig Service property
  // via D-Bus.
  // TODO(b/227715787): This D-Bus API should be deprecated.
  const net_base::NetworkConfig* GetSavedIPConfig() const;

  // Functions for DHCP.
  // Initiates renewal of existing DHCP lease. Return false if the renewal
  // failed immediately, or we don't have active lease now.
  mockable bool RenewDHCPLease();
  // Calculates the duration till a DHCP lease is due for renewal, and stores
  // this value in |result|. Returns std::nullopt if there is no upcoming DHCP
  // lease renewal, base::TimeDelta wrapped in std::optional otherwise.
  mockable std::optional<base::TimeDelta> TimeToNextDHCPLeaseRenewal();

  // Invalidate the IPv6 config kept in shill and wait for the new config from
  // the kernel.
  mockable void InvalidateIPv6Config();

  // Returns a WeakPtr of the Network.
  base::WeakPtr<Network> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // Routing policy rules have priorities, which establishes the order in which
  // policy rules will be matched against the current traffic. The higher the
  // priority value, the lower the priority of the rule. 0 is the highest rule
  // priority and is generally reserved for the kernel.
  //
  // Updates the kernel's routing policy rule database such that policy rules
  // corresponding to this Connection will use |priority| as the "base
  // priority". This call also updates the systemwide DNS configuration if
  // necessary, and triggers captive portal detection if the connection has
  // transitioned from non-default to default.
  //
  // This function should only be called when the Network is connected,
  // otherwise the call is a no-op.
  mockable void SetPriority(net_base::NetworkPriority network_priority);

  // Returns the current priority of the Network.
  net_base::NetworkPriority GetPriority();

  // Returns the current active configuration of the Network. That could be from
  // DHCPv4, static IPv4 configuration, SLAAC, data-link layer control
  // protocols, or merged from multiple of these sources.
  const net_base::NetworkConfig& GetNetworkConfig() const;

  // Returns all known (global) addresses of the Network. That includes IPv4
  // address from link protocol, or from DHCPv4, or from static IPv4
  // configuration; and IPv6 address from SLAAC and/or from link protocol.
  // TODO(b/269401899): deprecate this and use GetNetworkConfig() instead.
  mockable std::vector<net_base::IPCIDR> GetAddresses() const;

  // Return all (both IPv4 and IPv6) DNS servers configured for the Network.
  // TODO(b/269401899): deprecate this and use GetNetworkConfig() instead.
  mockable std::vector<net_base::IPAddress> GetDNSServers() const;

  // Responds to a neighbor reachability event from patchpanel.
  mockable void OnNeighborReachabilityEvent(
      const patchpanel::Client::NeighborReachabilityEvent& event);

  // Changes the network validation mode for the current network connection,
  // and starts or stops network validation accordingly:
  //   - if validation was disabled and is now enabled, network validation is
  //   restarted.
  //   - if validation was currently active and is now disabled, network
  //   validation is stopped.
  // Any ValidationMode set for a given network connection does not carry over
  // to the next network connection.
  mockable void UpdateNetworkValidationMode(
      NetworkMonitor::ValidationMode mode);

  // Setter/getter for enabling the CAPPORT functionality.
  void SetCapportEnabled(bool enabled);
  bool GetCapportEnabled() const { return capport_enabled_; }

  // Starts a new network validation attempt if network validation is enabled.
  // See the detail of NetworkMonitor::Start().
  mockable void RequestNetworkValidation(
      NetworkMonitor::ValidationReason reason);

  // Stops the current network validation cycle if it is still running.
  mockable void StopPortalDetection(bool is_failure = false);

  // Returns the PortalDetector::Result from the last network validation
  // attempt that completed, or nothing if no network validation attempt
  // has completed for this network connection yet.
  const std::optional<NetworkMonitor::Result>& network_validation_result()
      const {
    return network_validation_result_;
  }

  // Start a separate PortalDetector instance for the purpose of connectivity
  // test.
  void StartConnectivityTest(PortalDetector::ProbingConfiguration probe_config);

  // Returns the RPC identifiers of the available IPConfig objects (at most two,
  // one for IPv4 and one for IPv6) owned by the Network object.
  // TODO(b/340974631): Remove this after we fully retire the IPConfig D-Bus
  // interface of shill.
  RpcIdentifiers AvailableIPConfigIdentifiers() const;

  bool fixed_ip_params() const { return fixed_ip_params_; }
  const std::string& logging_tag() const { return logging_tag_; }
  void set_logging_tag(std::string_view logging_tag) {
    logging_tag_ = logging_tag;
  }

  // Returns true if the IPv4 or IPv6 gateway respectively has been observed as
  // a reachable neighbor for the current active connection. Reachability can
  // only be obsrved on WiFi and Ethernet networks.
  mockable bool ipv4_gateway_found() const { return ipv4_gateway_found_; }
  bool ipv6_gateway_found() const { return ipv6_gateway_found_; }

  // Returns true if the DHCP parameters provided indicate that the Chromebook
  // is tetherd to an Android mobile device or another Chromebook over a WiFi
  // hotspot or a USB ethernet connection ("ANDROID_METERED" vendor option 43).
  mockable bool IsConnectedViaTether() const;

  // Helper functions to prepare data and call corresponding NetworkApplier
  // function. Protected for manual-triggering in test. Calls |callback| when
  // finished.
  mockable void ApplyNetworkConfig(
      NetworkConfigArea area,
      base::OnceCallback<void(bool)> callback = base::DoNothing());

  // Called by a Device to signal a terms and conditions page is available on
  // the network.
  mockable void OnTermsAndConditions(const net_base::HttpUrl& url);

  const IPConfig* get_ipconfig_for_testing() const { return ipconfig_.get(); }
  const IPConfig* get_ip6config_for_testing() const { return ip6config_.get(); }
  void set_fixed_ip_params_for_testing(bool val) { fixed_ip_params_ = val; }
  void set_dhcp_controller_factory_for_testing(
      std::unique_ptr<DHCPControllerFactory> dhcp_controller_factory) {
    legacy_dhcp_controller_factory_ = std::move(dhcp_controller_factory);
  }
  void set_state_for_testing(State state) { state_ = state; }
  void set_primary_family_for_testing(
      std::optional<net_base::IPFamily> family) {
    primary_family_ = family;
  }
  void set_dhcp_network_config_for_testing(
      const net_base::NetworkConfig& network_config) {
    config_.SetFromDHCP(
        std::make_unique<net_base::NetworkConfig>(network_config));
  }
  void set_dhcp_data_for_testing(const DHCPv4Config::Data data) {
    dhcp_data_ = data;
  }
  // Take ownership of an external created net_base::ProcFsStub and return the
  // point to internal proc_fs_ after move.
  net_base::ProcFsStub* set_proc_fs_for_testing(
      std::unique_ptr<net_base::ProcFsStub> proc_fs) {
    proc_fs_ = std::move(proc_fs);
    return proc_fs_.get();
  }
  void set_ignore_link_monitoring_for_testing(bool ignore_link_monitoring) {
    ignore_link_monitoring_ = ignore_link_monitoring;
  }
  void set_network_monitor_for_testing(
      std::unique_ptr<NetworkMonitor> network_monitor) {
    network_monitor_ = std::move(network_monitor);
  }
  void set_network_monitor_result_for_testing(
      const NetworkMonitor::Result& result) {
    network_validation_result_ = result;
  }

  // Implements the NetworkMonitor::ClientNetwork class.
  const net_base::NetworkConfig& GetCurrentConfig() const override {
    return GetNetworkConfig();
  }
  void OnNetworkMonitorResult(const NetworkMonitor::Result& result) override;
  void OnValidationStarted(bool is_success) override;

 protected:
  // Only NetworkManager could construct Network instances.
  friend class NetworkManager;

  // The constructor is hidden to ensure all the Network instances are tracked
  // by NetworkManager. Please use NetworkManager::Get()->CreateNetwork() to
  // create a instance.
  Network(int interface_index,
          std::string_view interface_name,
          Technology technology,
          bool fixed_ip_params,
          ControlInterface* control_interface,
          EventDispatcher* dispatcher,
          Metrics* metrics,
          patchpanel::Client* patchpanel_client,
          std::unique_ptr<DHCPControllerFactory> legacy_dhcp_controller_factory,
          std::unique_ptr<DHCPControllerFactory> dhcp_controller_factory,
          Resolver* resolver = Resolver::GetInstance(),
          std::unique_ptr<NetworkMonitorFactory> network_monitor_factory =
              std::make_unique<NetworkMonitorFactory>());

 private:
  // TODO(b/232177767): Refactor DeviceTest to remove this dependency.
  friend class DeviceTest;
  // TODO(b/232177767): Refactor DeviceTest to remove this dependency.
  friend class DevicePortalDetectorTest;
  // TODO(b/232177767): Refactor StaticIPParametersTest to remove this
  // dependency
  friend class StaticIPParametersTest;

  // Configures (or reconfigures) the Network for |family|. If |is_slaac|, the
  // address and default route configuration is skipped.
  void SetupConnection(net_base::IPFamily family, bool is_slaac);

  // The second part of SetupConnection, called after network configuration
  // actually get applied to the netdev.
  void OnSetupConnectionFinished(bool success);

  // Creates a SLAACController object. Isolated for unit test mock injection.
  mockable std::unique_ptr<SLAACController> CreateSLAACController();

  // Returns the preferred IPFamily for performing network validation with
  // PortalDetector. This defaults to IPv4 if both IPv4 and IPv6 are available.
  std::optional<net_base::IPFamily> GetNetworkValidationIPFamily() const;
  // Returns the list of name servers for performing network validation with
  // PortalDetector.
  std::vector<net_base::IPAddress> GetNetworkValidationDNSServers(
      net_base::IPFamily family) const;

  // Shuts down and clears all the running state of this network. If
  // |trigger_callback| is true and the Network is started, OnNetworkStopped()
  // will be invoked with |is_failure|.
  void StopInternal(bool is_failure, bool trigger_callback);

  void ConnectivityTestCallback(const std::string& device_logging_tag,
                                const PortalDetector::Result& result);

  // Functions for IPv4.
  // Triggers a reconfiguration on connection for an IPv4 config change.
  void OnIPv4ConfigUpdated();
  // Callback registered with DHCPController. Also see the comment for
  // DHCPController::UpdateCallback.
  void OnIPConfigUpdatedFromDHCP(const net_base::NetworkConfig& network_config,
                                 const DHCPv4Config::Data& dhcp_data,
                                 bool new_lease_acquired);
  // Callback invoked on DHCP failures and RFC 8925 voluntary stops.
  void OnDHCPDrop(bool is_voluntary);

  // Functions for IPv6.
  // Called when IPv6 configuration changes.
  void OnIPv6ConfigUpdated();

  // Callback registered with SLAACController. |update_type| indicates the
  // update type (see comment in SLAACController declaration for detail).
  void OnUpdateFromSLAAC(SLAACController::UpdateType update_type);

  void UpdateIPConfigDBusObject();

  void CallPatchpanelConfigureNetwork(
      int interface_index,
      const std::string& interface_name,
      NetworkConfigArea area,
      const net_base::NetworkConfig& network_config,
      net_base::NetworkPriority priority,
      Technology technology,
      base::OnceCallback<void(bool)> callback,
      bool is_service_ready);

  void CallPatchpanelDestroyNetwork();

  // Enable ARP filtering on the interface. Incoming ARP requests are responded
  // to only by the interface(s) owning the address. Outgoing ARP requests will
  // contain the best local address for the target.
  void EnableARPFiltering();

  // Report the current IP type metrics (v4, v6 or dual-stack) to UMA.
  void ReportIPType();

  // Report to UMA a failure in patchpanel::NeighborLinkMonitor for a WiFi or
  // Ethernet network connection.
  void ReportNeighborLinkMonitorFailure(Technology tech,
                                        net_base::IPFamily family,
                                        patchpanel::Client::NeighborRole role);

  // The network_id of the next constructed Network instance.
  // TODO(b/273743901): Centralize the the network id allocation at patchpanel.
  static int next_network_id_;
  // The identifier of the Network instance, which is unique during the lifetime
  // of the shill process.
  const int network_id_;

  const int interface_index_;
  const std::string interface_name_;
  const Technology technology_;
  // A header tag to use in LOG statement for identifying the Device and Service
  // associated with a Network connection.
  std::string logging_tag_;

  // If true, IP parameters should not be modified. This should not be changed
  // after a Network object is created. Make it modifiable just for unit tests.
  bool fixed_ip_params_;

  State state_ = State::kIdle;

  // A temporary helper flag simulating the legacy SetupConnection() state. Also
  // indicates which IPConfig will be seen by legacy Service->IPConfig dbus API.
  //  std::nullopt - network configuration has not been applied.
  //  kIPv6 - IPv6 configuration has been applied, but not IPv4.
  //  kIPv4 - IPv4 configuration has been applied (IPv6 can be yes or no).
  std::optional<net_base::IPFamily> primary_family_ = std::nullopt;

  std::unique_ptr<net_base::ProcFsStub> proc_fs_;

  // TODO(b/344500617): Migrate DHCPv4 to use `dhcp_controller_factory_` too and
  // remove `legacy_dhcp_controller_factory_`.
  std::unique_ptr<DHCPControllerFactory> legacy_dhcp_controller_factory_;
  std::unique_ptr<DHCPControllerFactory> dhcp_controller_factory_;
  // The instance exists when the state is not at kIdle.
  std::unique_ptr<DHCPController> dhcp_controller_;
  // The instance exists when the state is not at kIdle.
  std::unique_ptr<SLAACController> slaac_controller_;

  std::unique_ptr<IPConfig> ipconfig_;
  std::unique_ptr<IPConfig> ip6config_;
  net_base::NetworkPriority priority_;

  base::RepeatingClosure current_ipconfig_change_handler_;

  CompoundNetworkConfig config_;
  std::optional<DHCPv4Config::Data> dhcp_data_;

  // Track the current same-net multi-home state.
  bool is_multi_homed_ = false;

  // Remember which flag files were previously successfully written. Only used
  // in SetIPFlag().
  std::set<std::string> written_flags_;

  // When set to true, neighbor events from link monitoring are ignored. This
  // boolean is reevaluated for every new Network connection.
  bool ignore_link_monitoring_ = false;

  // If the gateway has ever been reachable for the current connection. Reset in
  // Start().
  bool ipv4_gateway_found_ = false;
  bool ipv6_gateway_found_ = false;

  std::unique_ptr<NetworkMonitorFactory> network_monitor_factory_;
  PortalDetector::ProbingConfiguration probing_configuration_;
  bool capport_enabled_ = true;
  // Validates the network connectivity and detect the captive portal.
  // The instance exists when the state is not at kIdle.
  std::unique_ptr<NetworkMonitor> network_monitor_;
  // Records whether |network_monitor_| was running or not when starting it.
  bool network_monitor_was_running_ = false;
  // Only defined if NetworkMonitor completed at least one attempt for the
  // current network connection.
  std::optional<NetworkMonitor::Result> network_validation_result_;
  // Another instance of PortalDetector used for CreateConnectivityReport.
  std::unique_ptr<PortalDetector> connectivity_test_portal_detector_;

  base::ObserverList<EventHandler> event_handlers_;

  // Other dependencies.
  ControlInterface* control_interface_;
  EventDispatcher* dispatcher_;
  Metrics* metrics_;
  patchpanel::Client* patchpanel_client_;

  // Cache singleton pointers for performance and test purposes.
  net_base::RTNLHandler* rtnl_handler_;
  // TODO(b/240871320): /etc/resolv.conf is now managed by dnsproxy. The
  // resolver class is to be deprecated.
  Resolver* resolver_;

  // All the weak pointers created by this factory will be invalidated when the
  // Network state becomes kIdle. Can be useful when the concept of a connected
  // Network is needed. Note that the "connection" in the name is not the same
  // thing with the Connection class in shill.
  base::WeakPtrFactory<Network> weak_factory_for_connection_{this};

  base::WeakPtrFactory<Network> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, const Network& network);

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_H_
