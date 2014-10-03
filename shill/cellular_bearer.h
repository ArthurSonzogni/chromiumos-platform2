// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_BEARER_H_
#define SHILL_CELLULAR_BEARER_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <gtest/gtest_prod.h>

#include "shill/dbus_properties.h"
#include "shill/ipconfig.h"

namespace shill {

class DBusPropertiesProxyInterface;
class ProxyFactory;

// A class for observing property changes of a bearer object exposed by
// ModemManager.
class CellularBearer {
 public:
  // Constructs a cellular bearer for observing property changes of a
  // corresponding bearer object, at the DBus path |dbus_path| of DBus service
  // |dbus_service|,  exposed by ModemManager. The ownership of |proxy_factory|
  // is not transferred, and should outlive this object.
  //
  // TODO(benchan): Use a context object approach to pass objects like
  // ProxyFactory through constructor.
  CellularBearer(ProxyFactory *proxy_factory,
                 const std::string &dbus_path,
                 const std::string &dbus_service);
  ~CellularBearer();

  // Initializes this object by creating a DBus properties proxy to observe
  // property changes of the corresponding bearer object exposed by ModemManager
  // and also fetching the current properties of the bearer.  Returns true on
  // success or false if it fails to the DBus properties proxy.
  bool Init();

  // Callback upon DBus property changes of the bearer.
  void OnDBusPropertiesChanged(
      const std::string &interface,
      const DBusPropertiesMap &changed_properties,
      const std::vector<std::string> &invalidated_properties);

  const std::string &dbus_path() const { return dbus_path_; }
  const std::string &dbus_service() const { return dbus_service_; }

  bool connected() const { return connected_; }
  const std::string &data_interface() const { return data_interface_; }
  IPConfig::Method ipv4_config_method() const { return ipv4_config_method_; }
  const IPConfig::Properties *ipv4_config_properties() const {
    return ipv4_config_properties_.get();
  }
  IPConfig::Method ipv6_config_method() const { return ipv6_config_method_; }
  const IPConfig::Properties *ipv6_config_properties() const {
    return ipv6_config_properties_.get();
  }

 private:
  friend class CellularBearerTest;
  FRIEND_TEST(CellularTest, EstablishLinkDHCP);
  FRIEND_TEST(CellularTest, EstablishLinkPPP);
  FRIEND_TEST(CellularTest, EstablishLinkStatic);

  // Gets the IP configuration method and properties from |properties|.
  // |address_family| specifies the IP address family of the configuration.
  // |ipconfig_method| and |ipconfig_properties| are used to return the IP
  // configuration method and properties and should be non-NULL.
  void GetIPConfigMethodAndProperties(
      const DBusPropertiesMap &properties,
      IPAddress::Family address_family,
      IPConfig::Method *ipconfig_method,
      scoped_ptr<IPConfig::Properties> *ipconfig_properties) const;

  // Resets bearer properties.
  void ResetProperties();

  // Updates bearer properties by fetching the current properties of the
  // corresponding bearer object exposed by ModemManager over DBus.
  void UpdateProperties();

  // Setters for unit tests.
  void set_connected(bool connected) { connected_ = connected; }
  void set_data_interface(const std::string &data_interface) {
    data_interface_ = data_interface;
  }
  void set_ipv4_config_method(IPConfig::Method ipv4_config_method) {
    ipv4_config_method_ = ipv4_config_method;
  }
  void set_ipv4_config_properties(
      scoped_ptr<IPConfig::Properties> ipv4_config_properties) {
    ipv4_config_properties_ = ipv4_config_properties.Pass();
  }
  void set_ipv6_config_method(IPConfig::Method ipv6_config_method) {
    ipv6_config_method_ = ipv6_config_method;
  }
  void set_ipv6_config_properties(
      scoped_ptr<IPConfig::Properties> ipv6_config_properties) {
    ipv6_config_properties_ = ipv6_config_properties.Pass();
  }

  ProxyFactory *proxy_factory_;
  std::string dbus_path_;
  std::string dbus_service_;
  scoped_ptr<DBusPropertiesProxyInterface> dbus_properties_proxy_;
  bool connected_;
  std::string data_interface_;

  // If |ipv4_config_method_| is set to |IPConfig::kMethodStatic|,
  // |ipv4_config_properties_| is guaranteed to contain valid IP configuration
  // properties. Otherwise, |ipv4_config_properties_| is set to nullptr.
  // |ipv6_config_properties_| is handled similarly.
  IPConfig::Method ipv4_config_method_;
  scoped_ptr<IPConfig::Properties> ipv4_config_properties_;
  IPConfig::Method ipv6_config_method_;
  scoped_ptr<IPConfig::Properties> ipv6_config_properties_;

  DISALLOW_COPY_AND_ASSIGN(CellularBearer);
};

}  // namespace shill

#endif  // SHILL_CELLULAR_BEARER_H_
