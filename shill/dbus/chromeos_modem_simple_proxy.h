// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_MODEM_SIMPLE_PROXY_H_
#define SHILL_DBUS_CHROMEOS_MODEM_SIMPLE_PROXY_H_

#include <string>

#include <base/macros.h>

#include "cellular/dbus-proxies.h"
#include "shill/cellular/modem_simple_proxy_interface.h"

namespace shill {

// A proxy to (old) ModemManager.Modem.Simple.
class ChromeosModemSimpleProxy : public ModemSimpleProxyInterface {
 public:
  // Constructs a ModemManager.Modem.Simple DBus object proxy at
  // |path| owned by |service|.
  ChromeosModemSimpleProxy(const scoped_refptr<dbus::Bus>& bus,
                           const std::string& path,
                           const std::string& service);
  ~ChromeosModemSimpleProxy() override;

  // Inherited from ModemSimpleProxyInterface.
  void GetModemStatus(Error* error,
                      const KeyValueStoreCallback& callback,
                      int timeout) override;
  void Connect(const KeyValueStore& properties,
               Error* error,
               const ResultCallback& callback,
               int timeout) override;

 private:
  // Callbacks for GetStatus async call.
  void OnGetStatusSuccess(const KeyValueStoreCallback& callback,
                          const chromeos::VariantDictionary& props);
  void OnGetStatusFailure(const KeyValueStoreCallback& callback,
                          chromeos::Error* dbus_error);

  // Callbacks for Connect async call.
  void OnConnectSuccess(const ResultCallback& callback);
  void OnConnectFailure(const ResultCallback& callback,
                        chromeos::Error* dbus_error);

  std::unique_ptr<org::freedesktop::ModemManager::Modem::SimpleProxy> proxy_;

  base::WeakPtrFactory<ChromeosModemSimpleProxy> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ChromeosModemSimpleProxy);
};

}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_MODEM_SIMPLE_PROXY_H_
