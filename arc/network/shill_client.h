// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_SHILL_CLIENT_H_
#define ARC_NETWORK_SHILL_CLIENT_H_

#include <memory>
#include <set>
#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <shill/dbus-proxies.h>

namespace arc_networkd {

// Listens for shill signals over dbus in order to figure out which
// network interface (if any) is being used as the default service.
class ShillClient {
 public:
  explicit ShillClient(const scoped_refptr<dbus::Bus>& bus);
  virtual ~ShillClient() = default;

  void RegisterDefaultInterfaceChangedHandler(
      const base::Callback<void(const std::string&)>& callback);
  void UnregisterDefaultInterfaceChangedHandler();

  void RegisterDevicesChangedHandler(
      const base::Callback<void(const std::set<std::string>&)>& callback);
  void UnregisterDevicesChangedHandler();

  void ScanDevices(
      const base::Callback<void(const std::set<std::string>&)>& callback);

 protected:
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  // Returns the name of the default interface for the system, or an empty
  // string when the system has no default interface.
  virtual std::string GetDefaultInterface();

 private:
  // Sets the internal variable tracking the system default interface and calls
  // the default interface handler if the default interface changed. When the
  // default interface is lost and a fallback exists, the fallback is used
  // instead.
  void SetDefaultInterface(std::string new_default);

  // Tracks the name of the system default interface chosen by shill.
  std::string default_interface_;
  // Another network interface on the system to use as a possible fallback if
  // no system default interface exists.
  std::string fallback_default_interface_;
  // Tracks all network interfaces managed by shill.
  std::set<std::string> devices_;
  // Called when the interface used as the default interface changes.
  base::Callback<void(const std::string&)> default_interface_callback_;
  // Called when the list of network interfaces managed by shill changes.
  base::Callback<void(const std::set<std::string>&)> devices_callback_;

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxy> manager_proxy_;

  base::WeakPtrFactory<ShillClient> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(ShillClient);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_SHILL_CLIENT_H_
