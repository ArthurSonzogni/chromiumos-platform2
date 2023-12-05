// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_LOCAL_DEVICE_H_
#define SHILL_WIFI_LOCAL_DEVICE_H_

#include <iostream>
#include <optional>
#include <string>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>

#include "shill/mockable.h"
#include "shill/refptr_types.h"
#include "shill/wifi/local_service.h"

namespace shill {

class ControlInterface;
class EventDispatcher;
class Manager;
class SupplicantProcessProxyInterface;

// LocalDevice superclass. This class is used as a base class for local
// connection interface. Individual local connection interface type will inherit
// from this class.
class LocalDevice : public base::RefCounted<LocalDevice> {
 public:
  enum class IfaceType {
    kAP,
    kP2PGO,
    kP2PClient,
    kUnknown,
  };

  enum class DeviceEvent {
    kInterfaceDisabled,  // Interface is disabled in kernel.
    kInterfaceEnabled,   // Interface is enabled and ready to use.
    kLinkUp,       // A link layer (L2 connection) is added and brought up.
    kLinkDown,     // A link layer (L2 connection) is brought down and removed.
    kLinkFailure,  // Failed to bring up a link layer (L2 connection).
    kNetworkUp,    // A network layer (L3 connection) is added and brought up.
    kNetworkDown,  // A network layer (L3 connection) is brought down and
                   // removed.
    kNetworkFailure,    // Failed to bring up a network layer (L3 connection).
    kPeerConnected,     // A peer is connected.
    kPeerDisconnected,  // A peer is disconnected.
  };

  // Callback function with an event code and a pointer to the base class
  // LocalDevice. Unlike Network::EventHandler listener, we only expect 1
  // listener at any given time as the technology manager will be the sole owner
  // who cares about the device and service event. This could be expanded to a
  // listener handler queue/set with handler register/deregister functions to
  // handle multiple listen cases in the future when needed.
  using EventCallback =
      base::RepeatingCallback<void(DeviceEvent, const LocalDevice*)>;

  // Constructor function
  LocalDevice(Manager* manager,
              IfaceType type,
              std::optional<std::string> link_name,
              uint32_t phy_index,
              const EventCallback& callback);
  LocalDevice(const LocalDevice&) = delete;
  LocalDevice& operator=(const LocalDevice&) = delete;

  virtual ~LocalDevice();

  // Enable or disable the device.
  bool SetEnabled(bool enable);

  // Post a task and use registered callback function |callback_| to handle
  // device event.
  void PostDeviceEvent(DeviceEvent event) const;

  // Return true if the device has a configured service and the service is up.
  mockable bool IsServiceUp() const;

  std::optional<std::string> link_name() const { return link_name_; }
  uint32_t phy_index() const { return phy_index_; }
  IfaceType iface_type() const { return iface_type_; }

 protected:
  FRIEND_TEST(LocalDeviceTest, PostDeviceEvent);
  FRIEND_TEST(LocalDeviceTest, SetEnabled);

  // LocalDevice start routine. Each device type should implement this method.
  virtual bool Start() = 0;

  // LocalDevice stop routine. Each device type should implement this method.
  virtual bool Stop() = 0;

  // Get configured local service. Each device type should implement this
  // method.
  virtual LocalService* GetService() const = 0;

  // Return the proxy to the wpa_supplicant process.
  SupplicantProcessProxyInterface* SupplicantProcessProxy() const;
  ControlInterface* ControlInterface() const;
  EventDispatcher* Dispatcher() const;
  Manager* manager() const { return manager_; }
  std::optional<std::string> link_name_;

 private:
  friend class LocalDeviceTest;
  friend class P2PManagerTest;

  void DeviceEventTask(DeviceEvent event) const;

  bool enabled_;
  Manager* manager_;
  IfaceType iface_type_;
  uint32_t phy_index_;
  EventCallback callback_;
  base::WeakPtrFactory<LocalDevice> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, LocalDevice::IfaceType type);
std::ostream& operator<<(std::ostream& stream, LocalDevice::DeviceEvent event);

}  // namespace shill

#endif  // SHILL_WIFI_LOCAL_DEVICE_H_
