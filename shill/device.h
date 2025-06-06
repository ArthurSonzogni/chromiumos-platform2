// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DEVICE_H_
#define SHILL_DEVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <chromeos/net-base/ip_address.h>
#include <chromeos/net-base/mac_address.h>
#include <chromeos/net-base/rtnl_handler.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/adaptor_interfaces.h"
#include "shill/callbacks.h"
#include "shill/event_dispatcher.h"
#include "shill/geolocation_info.h"
#include "shill/metrics.h"
#include "shill/network/connection_diagnostics.h"
#include "shill/network/dhcp_provision_reasons.h"
#include "shill/network/network.h"
#include "shill/network/portal_detector.h"
#include "shill/refptr_types.h"
#include "shill/service.h"
#include "shill/store/property_store.h"
#include "shill/technology.h"

namespace shill {

class ControlInterface;
class DeviceAdaptorInterface;
class Error;
class EventDispatcher;
class Manager;

// Device superclass.  Individual network interfaces types will inherit from
// this class.
class Device : public base::RefCounted<Device>, public Network::EventHandler {
 public:
  // A constructor for the Device object
  Device(Manager* manager,
         std::string_view name,
         std::optional<net_base::MacAddress> mac_address,
         Technology technology);
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;

  // Initialize type-specific network interface properties.
  virtual void Initialize();

  // Enable or disable the device. This is a convenience method for
  // cases where we want to SetEnabledNonPersistent, but don't care
  // about the results.
  mockable void SetEnabled(bool enable);
  // Enable or disable the device. Unlike SetEnabledPersistent, it does not
  // save the setting in the profile.
  //
  // TODO(quiche): Replace both of the next two methods with calls to
  // SetEnabledChecked.
  void SetEnabledNonPersistent(bool enable, ResultCallback callback);
  // Enable or disable the device, and save the setting in the profile.
  // The setting is persisted before the enable or disable operation
  // starts, so that even if it fails, the user's intent is still recorded
  // for the next time shill restarts.
  void SetEnabledPersistent(bool enable, ResultCallback callback);
  // Enable or disable the Device, depending on |enable|.
  // Save the new setting to the profile, if |persist| is true.
  // Report synchronous errors using |error|, and asynchronous completion
  // with |callback|.
  mockable void SetEnabledChecked(bool enable,
                                  bool persist,
                                  ResultCallback callback);
  // Similar to SetEnabledChecked, but without coherence checking, and
  // without saving the new value of |enable| to the profile. If you
  // are rational (i.e. not Cellular), you should use
  // SetEnabledChecked instead.
  mockable void SetEnabledUnchecked(bool enable, ResultCallback callback);

  // Returns true if the underlying device reports that it is already enabled.
  // Used when the device is registered with the Manager, so that shill can
  // sync its state/ with the true state of the device. The default is to
  // report false.
  virtual bool IsUnderlyingDeviceEnabled() const;

  virtual void LinkEvent(unsigned flags, unsigned change);

  // The default implementation sets |error| to kNotSupported.
  virtual void Scan(Error* error, const std::string& reason, bool is_dbus_call);
  virtual void RegisterOnNetwork(const std::string& network_id,
                                 ResultCallback callback);
  virtual void RequirePin(const std::string& pin,
                          bool require,
                          ResultCallback callback);
  virtual void EnterPin(const std::string& pin, ResultCallback callback);
  virtual void UnblockPin(const std::string& unblock_code,
                          const std::string& pin,
                          ResultCallback callback);
  virtual void ChangePin(const std::string& old_pin,
                         const std::string& new_pin,
                         ResultCallback callback);
  virtual void Reset(ResultCallback callback);

  // Returns true if the selected service on the device (if any) is connected.
  // Returns false if there is no selected service, or if the selected service
  // is not connected.
  bool IsConnected() const;

  // Called by Device so that subclasses can run hooks on the selected service
  // getting an IP.  Subclasses should call up to the parent first.
  virtual void OnConnected();

  // Called by Device so that subclasses can run hooks on the selected service
  // changed. This function is called after the |selected_service_| changed so
  // the subclasses can call the getter to retrieve the new selected service.
  // Note that the base class does nothing here so the subclasses don't need to
  // call up to the parent.
  virtual void OnSelectedServiceChanged(const ServiceRefPtr& old_service);

  const RpcIdentifier& GetRpcIdentifier() const;
  std::string GetStorageIdentifier() const;

  // Update the Geolocation objects. Each object is multiple
  // key-value pairs representing one entity that can be used for
  // Geolocation.
  virtual void UpdateGeolocationObjects(
      std::vector<GeolocationInfo>* geolocation_infos) const;

  std::optional<net_base::MacAddress> mac_address() const {
    return mac_address_;
  }
  // Returns the interface name of the primary Network if it exits, otherwise
  // return the empty string. Device subclasses can override this method to
  // obtain
  // TODO(b/352665085): remove this getter and migrate client code to use the
  // Device's Network(s) directly.
  virtual std::string link_name() const;
  // Returns the interface index of the primary Network if it exits, otherwise
  // return -1. Device subclasses can override this method.
  // TODO(b/352665085): remove this getter and migrate client code to use the
  // Device's Network(s) directly.
  virtual int interface_index() const;
  bool enabled() const { return enabled_; }
  bool enabled_persistent() const { return enabled_persistent_; }
  mockable Technology technology() const { return technology_; }
  std::string GetTechnologyName() const;
  // Returns the raw hex string of |mac_address_| if it contains value.
  // Otherwise returns an empty string.
  std::string GetMacAddressHexString() const;

  // In WiFi, Ethernet and all other device types except for Cellular, this
  // method is guaranteed to return always a valid Network, so it is safe to
  // dereference the returned value.
  //
  // In Cellular devices, where ephemeral multiplexed network interfaces are
  // supported, this method is not guaranteed to always return a valid Network.
  // The Network lifecycle will be bound to the connection state of the device,
  // and therefore this method will return nullptr when disconnected.
  virtual Network* GetPrimaryNetwork() const;

  // Returns a string that is guaranteed to uniquely identify this Device
  // instance.
  const std::string& UniqueName() const;

  // Returns a WeakPtr of the Device.
  base::WeakPtr<Device> AsWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

  PropertyStore* mutable_store() { return &store_; }
  const PropertyStore& store() const { return store_; }
  net_base::RTNLHandler* rtnl_handler() { return rtnl_handler_; }

  EventDispatcher* dispatcher() const;

  // Load configuration for the device from |storage|.  This may include
  // instantiating non-visible services for which configuration has been
  // stored.
  virtual bool Load(const StoreInterface* storage);

  // Save configuration for the device to |storage|.
  virtual bool Save(StoreInterface* storage);

  DeviceAdaptorInterface* adaptor() const { return adaptor_.get(); }

  // Suspend event handler. Called by Manager before the system
  // suspends. This handler, along with any other suspend handlers,
  // will have Manager::kTerminationActionsTimeoutMilliseconds to
  // execute before the system enters the suspend state. |callback|
  // must be invoked after all synchronous and/or asynchronous actions
  // this function performs complete. Code that needs to run on exit should use
  // Manager::AddTerminationAction, rather than OnBeforeSuspend.
  //
  // The default implementation invokes the |callback| immediately, since
  // there is nothing to be done in the general case.
  virtual void OnBeforeSuspend(ResultCallback callback);

  // Resume event handler. Called by Manager as the system resumes.
  // The base class implementation takes care of renewing a DHCP lease
  // (if necessary). Derived classes may implement any technology
  // specific requirements by overriding, but should include a call to
  // the base class implementation.
  virtual void OnAfterResume();

  // This method is invoked when the system resumes from suspend temporarily in
  // the "dark resume" state. The system will reenter suspend in
  // Manager::kTerminationActionsTimeoutMilliseconds. |callback| must be invoked
  // after all synchronous and/or asynchronous actions this function performs
  // and/or posts complete.
  //
  // The default implementation invokes the |callback| immediately, since
  // there is nothing to be done in the general case.
  virtual void OnDarkResume(ResultCallback callback);

  // Sets MAC address source for USB Ethernet device.
  virtual void SetUsbEthernetMacAddressSource(const std::string& source,
                                              ResultCallback callback);

  // Renews DHCPv4 lease with the given reason and invalidates the IPv6 config
  // kept in shill. So far only DHCPProvisionReason::kSuspendResume is used in
  // this function.
  void ForceIPConfigUpdate(DHCPProvisionReason reason);

  // Request the WiFi device to roam to AP with |addr|.
  // This call will send Roam command to wpa_supplicant.
  virtual bool RequestRoam(const std::string& addr, Error* error);

  const ServiceRefPtr& selected_service() const { return selected_service_; }

  // Returns a string formatted as "$ifname $service_log_name", or
  // "$ifname no_service" if |selected_service_| is currently not defined.
  std::string LoggingTag() const;

  // Called when the device is claimed via the "ClaimInterface" API command. Can
  // be reimplemented by classes that have special steps for when they are
  // claimed.
  virtual void OnDeviceClaimed();

  // Overrides for Network::EventHandler. See the comments for
  // Network::EventHandler for more details.

  // Updates the state of the current selected service and request network
  // validation if the Service's current configuration does not disable network
  // validation. If network validation is currently disabled, the Service's
  // connection state is set immediately to 'online'.
  void OnConnectionUpdated(int interface_index) override;
  void OnNetworkStopped(int interface_index, bool is_failure) override;
  // Emit a property change signal for the "IPConfigs" property of this device.
  void OnIPConfigsPropertyUpdated(int interface_index) override;

  void set_selected_service_for_testing(ServiceRefPtr service) {
    selected_service_ = service;
  }

  void set_network_for_testing(std::unique_ptr<Network> network) {
    implicit_network_ = std::move(network);
  }

 protected:
  friend class base::RefCounted<Device>;
  FRIEND_TEST(CellularServiceTest, IsAutoConnectable);
  FRIEND_TEST(CellularTest, DefaultLinkDeleted);
  FRIEND_TEST(DeviceTest, AvailableIPConfigs);
  FRIEND_TEST(DeviceTest, GetProperties);
  FRIEND_TEST(DeviceTest, Load);
  FRIEND_TEST(DeviceTest, Save);
  FRIEND_TEST(DeviceTest, SelectedService);
  FRIEND_TEST(DeviceTest, SetEnabledNonPersistent);
  FRIEND_TEST(DeviceTest, SetEnabledPersistent);
  FRIEND_TEST(DeviceTest, Start);
  FRIEND_TEST(DeviceTest, StartFailure);
  FRIEND_TEST(DeviceTest, StartProhibited);
  FRIEND_TEST(DeviceTest, Stop);
  FRIEND_TEST(DeviceTest, StopWithFixedIpParams);
  FRIEND_TEST(DeviceTest, StopWithNetworkInterfaceDisabledAfterward);
  FRIEND_TEST(ManagerTest, ConnectedTechnologies);
  FRIEND_TEST(ManagerTest, DefaultTechnology);
  FRIEND_TEST(ManagerTest, DeviceRegistrationAndStart);
  FRIEND_TEST(ManagerTest, GetEnabledDeviceWithTechnology);
  FRIEND_TEST(ManagerTest, ConnectToMostSecureWiFi);
  FRIEND_TEST(ManagerTest, SetEnabledStateForTechnology);
  FRIEND_TEST(ManagerTest, TechnologyEnabledCheck);
  FRIEND_TEST(VirtualDeviceTest, ResetConnection);
  // For logging.
  friend std::ostream& operator<<(std::ostream& stream, const Device& device);

  ~Device() override;

  // Each device must implement this method to do the work needed to
  // enable the device to operate for establishing network connections.
  virtual void Start(EnabledStateChangedCallback callback) = 0;

  // Each device must implement this method to do the work needed to
  // disable the device, i.e., clear any running state, and make the
  // device no longer capable of establishing network connections.
  virtual void Stop(EnabledStateChangedCallback callback) = 0;

  // Returns true if the associated network interface should be brought down
  // after the device is disabled, or false if that should be done before the
  // device is disabled.
  virtual bool ShouldBringNetworkInterfaceDownAfterDisabled() const;

  // The EnabledStateChangedCallback that gets passed to the device's
  // Start() and Stop() methods is bound to this method. |callback|
  // is the callback that was passed to SetEnabled().
  void OnEnabledStateChanged(ResultCallback callback, const Error& error);

  // Update the device state to the pending state.
  void UpdateEnabledState();

  // Create the implicit Network object. Device subclasses that use a single
  // network interface and a single Network should call CreateImplicitNetwork in
  // their constructor.
  void CreateImplicitNetwork(int interface_index,
                             std::string_view interface_name,
                             bool fixed_ip_params);

  // Drops the currently selected service along with its IP configuration and
  // implicit Network connection, if any. Must be reimplemented by classes (e.g.
  // Cellular) that don't require the implicit network.
  virtual void DropConnection();

  // Brings the network interface associated to the implicit Network down. Must
  /// be reimplemented by classes (e.g. Cellular) that don't require the
  // implicit network.
  virtual void BringNetworkInterfaceDown();

  // Called by Device so that subclasses can run hooks on the selected service
  // failing to get an IP.  The default implementation disconnects the selected
  // service with Service::kFailureDHCP.
  virtual void OnIPConfigFailure();

  // Check if the interface index provided corresponds to the index of the
  // network interface associated to the primary network. Network events
  // reported in other interfaces will be ignored by the Device class.
  bool IsEventOnPrimaryNetwork(int interface_index);

  // Selects a service to be "current" -- i.e. link-state or configuration
  // events that happen to the device are attributed to this service. Also reset
  // old service state to Idle if its current state is not Failure and
  // |reset_old_service_state| is true.
  void SelectService(const ServiceRefPtr& service,
                     bool reset_old_service_state = true);

  // Reset the Network currently used in the selected service by reloading the
  // one considered primary. This will typically be run during SelectService()
  // but may also happen if technologies silently change the Network used
  // without performing service selection.
  void ResetServiceAttachedNetwork();

  // Set the state of the |selected_service_|.
  virtual void SetServiceState(Service::ConnectState state);

  // Set the failure of the selected service (implicitly sets the state to
  // "failure").
  virtual void SetServiceFailure(Service::ConnectFailure failure_state);

  // Records the failure mode and time of the selected service, and
  // sets the Service state of the selected service to "Idle".
  // Avoids showing a failure mole in the UI.
  virtual void SetServiceFailureSilent(Service::ConnectFailure failure_state);

  void HelpRegisterConstDerivedString(std::string_view name,
                                      std::string (Device::*get)(Error*));

  void HelpRegisterConstDerivedRpcIdentifier(
      std::string_view name, RpcIdentifier (Device::*get)(Error*));

  void HelpRegisterConstDerivedRpcIdentifiers(
      std::string_view name, RpcIdentifiers (Device::*get)(Error*));

  void HelpRegisterConstDerivedUint64(std::string_view name,
                                      uint64_t (Device::*get)(Error*));

  // By default StorageId is equal to: "device_" + DeviceStorageSuffix()
  // where the latter returns the raw hex string of the MAC address.
  // This can be overridden in subclasses.
  virtual std::string DeviceStorageSuffix() const {
    return GetMacAddressHexString();
  }

  // Property getters reserved for subclasses
  ControlInterface* control_interface() const;
  bool enabled_pending() const { return enabled_pending_; }
  Metrics* metrics() const;
  Manager* manager() const { return manager_; }

  virtual void set_mac_address(net_base::MacAddress mac_address);

  // Emit a given MAC Address via dbus. If std::nullopt is provided, emit the
  // hardware MAC address of the device.
  void EmitMACAddress(
      std::optional<net_base::MacAddress> mac_address = std::nullopt);

 private:
  friend class CellularTest;
  friend class DeviceAdaptorInterface;
  friend class DeviceByteCountTest;
  friend class DevicePortalDetectionTest;
  friend class DeviceTest;
  friend class DevicePortalDetectorTest;
  friend class EthernetTest;
  friend class OpenVPNDriverTest;
  friend class TestDevice;
  friend class VirtualDeviceTest;
  friend class WiFiObjectTest;

  static const char kStoragePowered[];

  RpcIdentifier GetSelectedServiceRpcIdentifier(Error* error);
  RpcIdentifiers AvailableIPConfigs(Error* error);

  // Necessary getter signature for kTypeProperty. Cannot be const.
  std::string GetTechnologyString(Error* error);
  // Necessary getter signature for kAddressProperty. Cannot be const.
  std::string GetMacAddressString(Error* error);
  // Necessary getter signature for kInterfaceProperty. Cannot be const.
  std::string GetInterface(Error* error);

  std::string GetServiceLogName() const;
  std::string GetNetworkSessionID() const;

  // |enabled_persistent_| is the value of the Powered property, as
  // read from the profile. If it is not found in the profile, it
  // defaults to true. |enabled_| reflects the real-time state of
  // the device, i.e., enabled or disabled. |enabled_pending_| reflects
  // the target state of the device while an enable or disable operation
  // is occurring.
  //
  // Some typical sequences for these state variables are shown below.
  //
  // Shill starts up, profile has been read:
  //  |enabled_persistent_|=true   |enabled_|=false   |enabled_pending_|=false
  //
  // Shill acts on the value of |enabled_persistent_|, calls SetEnabled(true):
  //  |enabled_persistent_|=true   |enabled_|=false   |enabled_pending_|=true
  //
  // SetEnabled completes successfully, device is enabled:
  //  |enabled_persistent_|=true   |enabled_|=true    |enabled_pending_|=true
  //
  // User presses "Disable" button, SetEnabled(false) is called:
  //  |enabled_persistent_|=false   |enabled_|=true    |enabled_pending_|=false
  //
  // SetEnabled completes successfully, device is disabled:
  //  |enabled_persistent_|=false   |enabled_|=false    |enabled_pending_|=false
  bool enabled_;
  bool enabled_persistent_;
  bool enabled_pending_;

  std::optional<net_base::MacAddress> mac_address_;

  PropertyStore store_;

  // Name representing this Device. This may be different than link_name() but
  // must be unique across all existing Device instances.
  const std::string name_;
  Manager* manager_;
  std::unique_ptr<Network> implicit_network_;
  std::unique_ptr<DeviceAdaptorInterface> adaptor_;
  Technology technology_;

  // Maintain a reference to the connected / connecting service
  ServiceRefPtr selected_service_;

  // Cache singleton pointers for performance and test purposes.
  net_base::RTNLHandler* rtnl_handler_;

  base::WeakPtrFactory<Device> weak_ptr_factory_;
};

std::ostream& operator<<(std::ostream& stream, const Device& device);

}  // namespace shill

#endif  // SHILL_DEVICE_H_
