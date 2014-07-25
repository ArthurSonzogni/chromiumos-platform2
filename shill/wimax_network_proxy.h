// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIMAX_NETWORK_PROXY_H_
#define SHILL_WIMAX_NETWORK_PROXY_H_

#include <string>

#include <base/callback.h>

#include "shill/wimax_network_proxy_interface.h"
#include "wimax_manager/dbus_proxies/org.chromium.WiMaxManager.Network.h"

namespace shill {

class WiMaxNetworkProxy : public WiMaxNetworkProxyInterface {
 public:
  // Constructs a WiMaxManager.Network DBus object proxy at |path|.
  WiMaxNetworkProxy(DBus::Connection *connection,
                   const DBus::Path &path);
  virtual ~WiMaxNetworkProxy();

  // Inherited from WiMaxNetwokProxyInterface.
  virtual RpcIdentifier path() const;
  virtual void set_signal_strength_changed_callback(
      const SignalStrengthChangedCallback &callback);
  virtual uint32 Identifier(Error *error);
  virtual std::string Name(Error *error);
  virtual int Type(Error *error);
  virtual int CINR(Error *error);
  virtual int RSSI(Error *error);
  virtual int SignalStrength(Error *error);

 private:
  class Proxy : public org::chromium::WiMaxManager::Network_proxy,
                public DBus::ObjectProxy {
   public:
    Proxy(DBus::Connection *connection, const DBus::Path &path);
    virtual ~Proxy();

    void set_signal_strength_changed_callback(
        const SignalStrengthChangedCallback &callback);

   private:
    // Signal callbacks inherited from WiMaxManager::Network_proxy.
    virtual void SignalStrengthChanged(const int32 &signal_strength);

    // Method callbacks inherited from WiMaxManager::Network_proxy.
    // [None]

    SignalStrengthChangedCallback signal_strength_changed_callback_;

    DISALLOW_COPY_AND_ASSIGN(Proxy);
  };

  static void FromDBusError(const DBus::Error &dbus_error, Error *error);

  Proxy proxy_;

  DISALLOW_COPY_AND_ASSIGN(WiMaxNetworkProxy);
};

}  // namespace shill

#endif  // SHILL_WIMAX_NETWORK_PROXY_H_
