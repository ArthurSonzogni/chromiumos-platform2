// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CLIENT_CLIENT_H_
#define SHILL_DBUS_CLIENT_CLIENT_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <brillo/brillo_export.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/ip_address.h>
#include <shill/dbus-proxies.h>

namespace shill {

constexpr base::TimeDelta kDefaultDBusTimeout =
    base::Milliseconds(dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);

// Shill D-Bus client for listening to common manager, service and device
// properties. This class is the result of an effort to consolidate a lot of
// duplicated boilerplate across multiple platform2 packages.
// TODO(garrick): Integrate into applicable platform2 packages.
class BRILLO_EXPORT Client {
 public:
  // This struct contains a subset of the net_base::NetworkConfig struct. Only
  // contains the fields which the users of this shill client may care about.
  struct NetworkConfig {
    NetworkConfig();
    ~NetworkConfig();

    NetworkConfig(const NetworkConfig& other);
    NetworkConfig& operator=(const NetworkConfig& other);

    bool operator==(const NetworkConfig& rhs) const;

    // IPv4 configurations.
    std::optional<net_base::IPv4CIDR> ipv4_address;
    std::optional<net_base::IPv4Address> ipv4_gateway;

    // IPv6 configurations.
    std::vector<net_base::IPv6CIDR> ipv6_addresses;
    std::optional<net_base::IPv6Address> ipv6_gateway;

    // DNS configurations.
    std::vector<net_base::IPAddress> dns_servers;
    std::vector<std::string> dns_search_domains;
  };

  // Represents a subset of properties from org.chromium.flimflam.Device.
  // TODO(jiejiang): add the following fields into this struct:
  // - the DBus path of the Service associated to this Device if any
  // - the connection state of the Service, if possible by translating back to
  //   the enum shill::Service::ConnectState
  struct Device {
    // A subset of shill::Technology.
    enum class Type {
      kUnknown,
      kCellular,
      kEthernet,
      kEthernetEap,
      kGuestInterface,
      kLoopback,
      kPPP,
      kTunnel,
      kVPN,
      kWifi,
    };

    // From shill::ConnectState.
    enum class ConnectionState {
      kUnknown,
      kIdle,
      kAssociation,
      kConfiguration,
      kReady,
      kNoConnectivity,
      kRedirectFound,
      kPortalSuspected,
      kOnline,
      kFailure,
      kDisconnecting,
    };

    // Returns the interface that is expected to be used for network operations.
    // For cell, this mean the primary multiplexed interface.
    std::string active_ifname() {
      return cellular_primary_ifname.empty() ? ifname : cellular_primary_ifname;
    }

    Type type;
    ConnectionState state;
    std::string ifname;
    std::string cellular_primary_ifname;  // empty if cell device has no primary
                                          // interface property.
    std::string cellular_country_code;
    NetworkConfig network_config;

    // The session_id of the associated Network. This should only be used in
    // logging.
    int session_id = 0;
  };

  template <class Proxy>
  class PropertyAccessor {
   public:
    PropertyAccessor(Proxy* proxy,
                     const base::TimeDelta& timeout = kDefaultDBusTimeout)
        : proxy_(proxy), timeout_(timeout.InMilliseconds()) {
      proxy_->RegisterPropertyChangedSignalHandler(
          base::BindRepeating(&PropertyAccessor::OnPropertyChange,
                              weak_factory_.GetWeakPtr()),
          base::BindRepeating(&PropertyAccessor::OnPropertyChangeRegistration,
                              weak_factory_.GetWeakPtr()));
    }

    virtual ~PropertyAccessor() = default;
    PropertyAccessor(const PropertyAccessor&) = delete;
    PropertyAccessor& operator=(const PropertyAccessor&) = delete;

    // Synchronous setter.
    virtual bool Set(const std::string& name,
                     const brillo::Any& value,
                     brillo::ErrorPtr* error) {
      return proxy_->SetProperty(name, value, error, timeout_);
    }

    // Asynchronous setter.
    virtual void Set(const std::string& name,
                     const brillo::Any& value,
                     base::OnceCallback<void()> success,
                     base::OnceCallback<void(brillo::Error*)> error) {
      proxy_->SetPropertyAsync(name, value, std::move(success),
                               std::move(error), timeout_);
    }

    // Get all properties.
    virtual bool Get(brillo::VariantDictionary* properties,
                     brillo::ErrorPtr* error) const {
      return proxy_->GetProperties(properties, error, timeout_);
    }

    // Get one property or its default empty value if not found.
    template <class T>
    bool Get(const std::string& name,
             T* property,
             brillo::ErrorPtr* error) const {
      brillo::VariantDictionary properties;
      if (!Get(&properties, error)) {
        return false;
      }

      *property = brillo::GetVariantValueOrDefault<T>(properties, name);
      return true;
    }

    // TODO(garrick): Async getters.
    // TODO(garrick): Clear.

    // Register a handler for changes to a property.
    void Watch(const std::string& name,
               base::RepeatingCallback<void(const brillo::Any&)> handler) {
      handlers_[name].push_back(std::move(handler));
    }

   private:
    void OnPropertyChangeRegistration(const std::string& interface,
                                      const std::string& name,
                                      bool success) {
      if (!success) {
        LOG(DFATAL) << "Failed to watch property [" << name << "] on ["
                    << interface << "]";
        return;
      }
    }

    void OnPropertyChange(const std::string& name, const brillo::Any& value) {
      const auto it = handlers_.find(name);
      if (it != handlers_.end()) {
        for (const auto& h : it->second) {
          h.Run(value);
        }
      }
    }

    Proxy* proxy_;
    const int timeout_;
    std::map<std::string,
             std::vector<base::RepeatingCallback<void(const brillo::Any&)>>>
        handlers_;

    base::WeakPtrFactory<PropertyAccessor> weak_factory_{this};
  };

  using ManagerPropertyAccessor =
      PropertyAccessor<org::chromium::flimflam::ManagerProxyInterface>;
  using DeviceChangedHandler =
      base::RepeatingCallback<void(const Device* const)>;

  explicit Client(scoped_refptr<dbus::Bus> bus);
  virtual ~Client() = default;
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  // |handler| will be invoked when shill's DBus service is available.
  // If called and the service is up, it will return true immediately,
  // if there is an internal error, it will return false immediately,
  // otherwise it will be called at a future point when the service owner
  // is updated.
  virtual void RegisterOnAvailableCallback(
      base::OnceCallback<void(bool)> handler);

  // |handler| will be invoked whenever shill exits. The boolean parameter
  // passed to the callback will be true if a new shill process was started and
  // now owns the dbus service; it will be false if shill is no longer running
  // (or at least, is no longer available on dbus).
  // Only one handler may be registered.
  virtual void RegisterProcessChangedHandler(
      const base::RepeatingCallback<void(bool)>& handler);

  // |handler| will be invoked whenever the device associated with the default
  // service changes. The following changes will triggers this handler:
  // * The default service itself changes,
  // * The default service device connection state changes,
  // * The device connected to the default service changes,
  // * The IP configuration of the default device changes.
  //
  // If the default service is empty, the device will be null.
  // Multiple handlers may be registered.
  virtual void RegisterDefaultDeviceChangedHandler(
      const DeviceChangedHandler& handler);

  // |handler| will be invoked whenever there is a change to tracked properties
  // which currently include:
  // * The device's network config,
  // * The state of the device's connected service.
  // Multiple handlers may be registered.
  virtual void RegisterDeviceChangedHandler(
      const DeviceChangedHandler& handler);

  // |handler| will be invoked whenever a device is added or removed from shill.
  // Note that if the default service switches to VPN, the corresponding device
  // will be added and tracked. This will not occur for any other type of
  // virtual device. Handlers can use |Device.type| to filter, if necessary.
  // Multiple handlers may be registered.
  virtual void RegisterDeviceAddedHandler(const DeviceChangedHandler& handler);
  virtual void RegisterDeviceRemovedHandler(
      const DeviceChangedHandler& handler);

  // Returns a manipulator interface for Manager properties.
  virtual std::unique_ptr<ManagerPropertyAccessor> ManagerProperties(
      const base::TimeDelta& timeout = kDefaultDBusTimeout) const;

  // Returns the default device.
  // If |exclude_vpn| is true, then the device returned will be associated with
  // the highest priority service that is not of type "vpn".
  // This method always queries the Manager for the latest properties. The
  // default device can be passively tracked by registering the appropriate
  // handler (assuming one is interested in the VPN device).
  virtual std::unique_ptr<Device> DefaultDevice(bool exclude_vpn);

  // Returns the manager proxy. This pointer must not be deleted by the caller.
  virtual org::chromium::flimflam::ManagerProxyInterface* GetManagerProxy()
      const;

  // Returns all available devices.
  virtual std::vector<std::unique_ptr<Device>> GetDevices() const;

 protected:
  // All of the methods and members with protected access scope are needed for
  // unit testing.

  // Invoked when the DBus service owner name changes, which occurs when the
  // service is stopped (new_owner is empty) or restarted (new_owner !=
  // old_owner)
  // This will trigger any existing proxies to the existing service to be reset,
  // and a new manager proxy will be established.
  void OnOwnerChange(const std::string& old_owner,
                     const std::string& new_owner);

  // This callback is invoked whenever a manager property change signal is
  // received; if the property is one we pay attention to the corresponding
  // Handle*Changed handler will be called.
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  // This callback is invoked whenever a device property change signal is
  // received; if the property is one we pay attention to the corresponding
  // handler will be invoked.
  void OnDevicePropertyChange(const std::string& device_path,
                              const std::string& property_name,
                              const brillo::Any& property_value);

  // This callback is invoked whenever a service property change signal is
  // received for a service that is connected to a particular device. In this
  // case |device_path| will be non-empty. Note that if the service in question
  // is also the default service, this handler will be called as well as the
  // default service change handler.
  void OnServicePropertyChange(const std::string& device_path,
                               const std::string& property_name,
                               const brillo::Any& property_value);

  virtual std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface>
  NewDeviceProxy(const dbus::ObjectPath& device_path);
  virtual std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>
  NewServiceProxy(const dbus::ObjectPath& service_path);

  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      manager_proxy_;

 private:
  // Wraps a device with its DBus proxy on which property change signals are
  // received.
  class DeviceWrapper {
   public:
    DeviceWrapper(
        scoped_refptr<dbus::Bus> bus,
        std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> proxy)
        : bus_(bus), proxy_(std::move(proxy)) {}
    ~DeviceWrapper() = default;
    DeviceWrapper(const DeviceWrapper&) = delete;
    DeviceWrapper& operator=(const DeviceWrapper&) = delete;

    Device* device() { return &device_; }
    org::chromium::flimflam::DeviceProxyInterface* proxy() {
      return proxy_.get();
    }

    // Object proxy needs to be released whenever a DeviceWrapper is deleted.
    // However, it is not possible to do it in its destructor as the method
    // `RemoveObjectProxy(...)` is asynchronous and might race with the D-Bus
    // destructor.
    void release_object_proxy() {
      bus_->RemoveObjectProxy(kFlimflamServiceName, proxy_->GetObjectPath(),
                              base::DoNothing());
      if (svc_proxy_) {
        bus_->RemoveObjectProxy(kFlimflamServiceName,
                                svc_proxy_->GetObjectPath(), base::DoNothing());
      }
    }
    void set_service_proxy(
        std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface> proxy) {
      // Note - expect this to be called once - if that ever changes, call
      // RemoveObjectProxy first.
      svc_proxy_ = std::move(proxy);
    }
    org::chromium::flimflam::ServiceProxyInterface* service_proxy() {
      return svc_proxy_.get();
    }

   private:
    scoped_refptr<dbus::Bus> bus_;
    Device device_;
    std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> proxy_;
    std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface> svc_proxy_;
  };

  // This callback is invoked whenever the default service changes, that is,
  // when it switches from one service to another.
  void HandleDefaultServiceChanged(const brillo::Any& property_value);

  // This callback is invoked whenever the (physical) device list provided by
  // shill changes.
  void HandleDevicesChanged(const brillo::Any& property_value);

  // Invoked whenever a device's selected service changes.
  void HandleSelectedServiceChanged(const std::string& device_path,
                                    const brillo::Any& property_value,
                                    DeviceWrapper* device_wrapper);

  // Invoked whenever a device's interface name changes. Invokes the DeviceAdded
  // callback if the interface name is added, and DeviceRemovedCallback if the
  // interface name is removed.
  void HandleDeviceInterfaceChanged(const std::string& device_path,
                                    const brillo::Any& property_value,
                                    Device* device);

  // This callback is invoked whenever a new manager proxy is created. It will
  // trigger the discovery of the default service.
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);

  // This callback is invoked whenever a new device proxy is created. It will
  // trigger the discovery of the device properties we care about including its
  // type, interface name and IP configuration.
  void OnDevicePropertyChangeRegistration(const std::string& device_path,
                                          const std::string& interface,
                                          const std::string& signal_name,
                                          bool success);

  // This callback is invoked whenever a new selected service proxy is created.
  // It will trigger the discovery of service properties we care about including
  // the connected state.
  void OnServicePropertyChangeRegistration(const std::string& device_path,
                                           const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);

  void SetupSelectedServiceProxy(const dbus::ObjectPath& service_path,
                                 const dbus::ObjectPath& device_path);
  void SetupDeviceProxy(const dbus::ObjectPath& device_path);

  void AddDevice(const dbus::ObjectPath& path);

  scoped_refptr<dbus::Bus> bus_;

  base::RepeatingCallback<void(bool)> process_handler_;
  std::vector<DeviceChangedHandler> default_device_handlers_;
  std::vector<DeviceChangedHandler> device_handlers_;
  std::vector<DeviceChangedHandler> device_added_handlers_;
  std::vector<DeviceChangedHandler> device_removed_handlers_;

  std::string default_device_path_;
  std::string default_service_path_;

  // Tracked devices keyed by path.
  std::map<std::string, std::unique_ptr<DeviceWrapper>> devices_;

  base::WeakPtrFactory<Client> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_DBUS_CLIENT_CLIENT_H_
