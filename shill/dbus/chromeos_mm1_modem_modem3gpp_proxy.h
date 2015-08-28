// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_MM1_MODEM_MODEM3GPP_PROXY_H_
#define SHILL_DBUS_CHROMEOS_MM1_MODEM_MODEM3GPP_PROXY_H_

#include <string>
#include <vector>

#include "cellular/dbus-proxies.h"
#include "shill/cellular/mm1_modem_modem3gpp_proxy_interface.h"

namespace shill {
namespace mm1 {

// A proxy to org.freedesktop.ModemManager1.Modem.Modem3gpp.
class ChromeosModemModem3gppProxy : public ModemModem3gppProxyInterface {
 public:
  // Constructs an org.freedesktop.ModemManager1.Modem.Modem3gpp DBus
  // object proxy at |path| owned by |service|.
  ChromeosModemModem3gppProxy(const scoped_refptr<dbus::Bus>& bus,
                              const std::string& path,
                              const std::string& service);
  ~ChromeosModemModem3gppProxy() override;
  // Inherited methods from ModemModem3gppProxyInterface.
  void Register(const std::string& operator_id,
                Error* error,
                const ResultCallback& callback,
                int timeout) override;
  void Scan(Error* error,
            const KeyValueStoresCallback& callback,
            int timeout) override;

 private:
  // Callbacks for Register async call.
  void OnRegisterSuccess(const ResultCallback& callback);
  void OnRegisterFailure(const ResultCallback& callback,
                         chromeos::Error* dbus_error);

  // Callbacks for Scan async call.
  void OnScanSuccess(const KeyValueStoresCallback& callback,
                     const std::vector<chromeos::VariantDictionary>& results);
  void OnScanFailure(const KeyValueStoresCallback& callback,
                     chromeos::Error* dbus_error);

  std::unique_ptr<org::freedesktop::ModemManager1::Modem::Modem3gppProxy>
      proxy_;

  base::WeakPtrFactory<ChromeosModemModem3gppProxy> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ChromeosModemModem3gppProxy);
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_MM1_MODEM_MODEM3GPP_PROXY_H_
