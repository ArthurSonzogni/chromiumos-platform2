// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CHROMEOS_SUPPLICANT_BSS_PROXY_H_
#define SHILL_DBUS_CHROMEOS_SUPPLICANT_BSS_PROXY_H_

#include <string>

#include <base/macros.h>

#include "shill/refptr_types.h"
#include "shill/supplicant/supplicant_bss_proxy_interface.h"
#include "supplicant/dbus-proxies.h"

namespace shill {

class WiFiEndpoint;

class ChromeosSupplicantBSSProxy
    : public SupplicantBSSProxyInterface {
 public:
  ChromeosSupplicantBSSProxy(const scoped_refptr<dbus::Bus>& bus,
                             const std::string& object_path,
                             WiFiEndpoint* wifi_endpoint);
  ~ChromeosSupplicantBSSProxy() override;

 private:
  // Signal handlers.
  void PropertiesChanged(const chromeos::VariantDictionary& properties);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  base::WeakPtrFactory<ChromeosSupplicantBSSProxy> weak_factory_{this};
  std::unique_ptr<fi::w1::wpa_supplicant1::BSSProxy> bss_proxy_;
  // We use a bare pointer, because each ChromeosSupplcantBSSProxy is
  // owned (using a unique_ptr) by a WiFiEndpoint. This means that if
  // |wifi_endpoint_| is invalid, then so is |this|.
  WiFiEndpoint* wifi_endpoint_;

  DISALLOW_COPY_AND_ASSIGN(ChromeosSupplicantBSSProxy);
};

}  // namespace shill

#endif  // SHILL_DBUS_CHROMEOS_SUPPLICANT_BSS_PROXY_H_
