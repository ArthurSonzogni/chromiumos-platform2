// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_LOCAL_DEVICE_H_
#define SHILL_WIFI_LOCAL_DEVICE_H_

#include <iostream>
#include <string>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>

#include "shill/mac_address.h"
#include "shill/refptr_types.h"

namespace shill {

class EventDispatcher;
class Manager;
class Metrics;

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
    kServiceUp,          // A service is added and brought up.
    kServiceDown,        // A service is brought down and removed.
    kPeerConnected,      // A peer is connected.
    kPeerDisconnected,   // A peer is disconnected.
  };

  // Callback function with an event code and a pointer to the base class
  // LocalDevice. Unlike Network::EventHandler listener, we only expect 1
  // listener at any given time as the technology manager will be the sole owner
  // who cares about the device and service event. This could be expanded to a
  // listener handler queue/set with handler register/deregister functions to
  // handle multiple listen cases in the future when needed.
  using EventCallback =
      base::RepeatingCallback<void(DeviceEvent, LocalDevice*)>;

  // Constructor function
  LocalDevice(Manager* manager,
              IfaceType type,
              const std::string& link_name,
              const std::string& mac_address,
              uint32_t phy_index,
              const EventCallback& callback);
  LocalDevice(const LocalDevice&) = delete;
  LocalDevice& operator=(const LocalDevice&) = delete;

  virtual ~LocalDevice();

  // Enable or disable the device.
  bool SetEnabled(bool enable);

  const std::string& link_name() const { return link_name_; }
  uint32_t phy_index() const { return phy_index_; }
  IfaceType iface_type() const { return iface_type_; }

  EventDispatcher* Dispatcher() const;

 protected:
  FRIEND_TEST(LocalDeviceTest, PostDeviceEvent);
  FRIEND_TEST(LocalDeviceTest, SetEnabled);

  // LocalDevice start routine. Each device type should implement this method.
  virtual bool Start() = 0;

  // LocalDevice stop routine. Each device type should implement this method.
  virtual bool Stop() = 0;

  // Post a task and use registered callback function |callback_| to handle
  // device event.
  void PostDeviceEvent(DeviceEvent event);

 private:
  friend class LocalDeviceTest;

  void DeviceEventTask(DeviceEvent event);

  bool enabled_;
  Manager* manager_;
  IfaceType iface_type_;
  std::string link_name_;
  MACAddress mac_address_;
  uint32_t phy_index_;
  EventCallback callback_;
  base::WeakPtrFactory<LocalDevice> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, LocalDevice::IfaceType type);
std::ostream& operator<<(std::ostream& stream, LocalDevice::DeviceEvent event);

}  // namespace shill

#endif  // SHILL_WIFI_LOCAL_DEVICE_H_
