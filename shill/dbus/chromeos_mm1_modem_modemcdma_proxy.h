// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_MM1_MODEM_MODEMCDMA_PROXY_H_
#define SHILL_DBUS_CHROMEOS_MM1_MODEM_MODEMCDMA_PROXY_H_

#include <string>

#include "cellular/dbus-proxies.h"
#include "shill/cellular/mm1_modem_modemcdma_proxy_interface.h"

namespace shill {
namespace mm1 {

// A proxy to org.freedesktop.ModemManager1.Modem.ModemCdma.
class ChromeosModemModemCdmaProxy : public ModemModemCdmaProxyInterface {
 public:
  // Constructs a org.freedesktop.ModemManager1.Modem.ModemCdma DBus
  // object proxy at |path| owned by |service|.
  ChromeosModemModemCdmaProxy(const scoped_refptr<dbus::Bus>& bus,
                              const std::string& path,
                              const std::string& service);
  ~ChromeosModemModemCdmaProxy() override;

  // Inherited methods from ModemModemCdmaProxyInterface.
  void Activate(const std::string& carrier,
                Error* error,
                const ResultCallback& callback,
                int timeout) override;
  void ActivateManual(
      const KeyValueStore& properties,
      Error* error,
      const ResultCallback& callback,
      int timeout) override;

  void set_activation_state_callback(
      const ActivationStateSignalCallback& callback) override {
    activation_state_callback_ = callback;
  }

 private:
  // Signal handler.
  void ActivationStateChanged(
        uint32_t activation_state,
        uint32_t activation_error,
        const chromeos::VariantDictionary& status_changes);

  // Callbacks for async calls that uses ResultCallback.
  void OnOperationSuccess(const ResultCallback& callback,
                          const std::string& operation);
  void OnOperationFailure(const ResultCallback& callback,
                          const std::string& operation,
                          chromeos::Error* dbus_error);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  ActivationStateSignalCallback activation_state_callback_;

  std::unique_ptr<org::freedesktop::ModemManager1::Modem::ModemCdmaProxy>
      proxy_;

  base::WeakPtrFactory<ChromeosModemModemCdmaProxy> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ChromeosModemModemCdmaProxy);
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_MM1_MODEM_MODEMCDMA_PROXY_H_
