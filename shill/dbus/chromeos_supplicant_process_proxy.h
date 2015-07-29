// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_SUPPLICANT_PROCESS_PROXY_H_
#define SHILL_DBUS_CHROMEOS_SUPPLICANT_PROCESS_PROXY_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>

#include "shill/event_dispatcher.h"
#include "shill/supplicant/supplicant_process_proxy_interface.h"
#include "supplicant/dbus-proxies.h"

namespace shill {

class EventDispatcher;

class ChromeosSupplicantProcessProxy : public SupplicantProcessProxyInterface {
 public:
  ChromeosSupplicantProcessProxy(
      EventDispatcher* dispatcher,
      const scoped_refptr<dbus::Bus>& bus,
      const base::Closure& service_appeared_callback,
      const base::Closure& service_vanished_callback);
  ~ChromeosSupplicantProcessProxy() override;

  // Implementation of SupplicantProcessProxyInterface.
  bool CreateInterface(const KeyValueStore& args,
                       std::string* rpc_identifier) override;
  bool RemoveInterface(const std::string& rpc_identifier) override;
  bool GetInterface(const std::string& ifname,
                    std::string* rpc_identifier) override;
  // This function will always return true since PropertySet::Set is an async
  // method. Any failures will be logged in the callback.
  bool SetDebugLevel(const std::string& level) override;
  bool GetDebugLevel(std::string* level) override;

 private:
  class PropertySet : public dbus::PropertySet {
   public:
    PropertySet(dbus::ObjectProxy* object_proxy,
                const std::string& interface_name,
                const PropertyChangedCallback& callback);
    dbus::Property<std::string> debug_level;
    dbus::Property<bool> debug_timestamp;
    dbus::Property<bool> debug_show_keys;
    dbus::Property<std::vector<dbus::ObjectPath>> interfaces;
    dbus::Property<std::vector<std::string>> eap_methods;

   private:
    DISALLOW_COPY_AND_ASSIGN(PropertySet);
  };

  static const char kInterfaceName[];
  static const char kPropertyDebugLevel[];
  static const char kPropertyDebugTimestamp[];
  static const char kPropertyDebugShowKeys[];
  static const char kPropertyInterfaces[];
  static const char kPropertyEapMethods[];

  // Signal handlers.
  void InterfaceAdded(const dbus::ObjectPath& path,
                      const chromeos::VariantDictionary& properties);
  void InterfaceRemoved(const dbus::ObjectPath& path);
  void PropertiesChanged(const chromeos::VariantDictionary& properties);

  // Called when service appeared or vanished.
  void OnServiceAvailable(bool available);

  // Service name owner changed handler.
  void OnServiceOwnerChanged(const std::string& old_owner,
                             const std::string& new_owner);

  // Callback invoked when the value of property |property_name| is changed.
  void OnPropertyChanged(const std::string& property_name);

  // Callback invoked when DebugLevel property set completed.
  void OnDebugLevelSet(bool success);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  std::unique_ptr<fi::w1::wpa_supplicant1Proxy> supplicant_proxy_;
  std::unique_ptr<PropertySet> properties_;
  EventDispatcher* dispatcher_;
  base::Closure service_appeared_callback_;
  base::Closure service_vanished_callback_;
  bool service_available_;

  base::WeakPtrFactory<ChromeosSupplicantProcessProxy> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ChromeosSupplicantProcessProxy);
};

}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_SUPPLICANT_PROCESS_PROXY_H_
