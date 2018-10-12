// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SHILL_CLIENT_H_
#define VM_TOOLS_CONCIERGE_SHILL_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <shill/dbus-proxies.h>

namespace vm_tools {
namespace concierge {

// Listens for shill signals over D-Bus in order to identify when DNS
// nameservers or search domains change.
class ShillClient final {
 public:
  explicit ShillClient(scoped_refptr<dbus::Bus> bus);

  void RegisterResolvConfigChangedHandler(
      base::Callback<void(std::vector<std::string> nameservers,
                          std::vector<std::string> search_domains)> callback);

 private:
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  bool GetResolvConfig(std::vector<std::string>* nameservers,
                       std::vector<std::string>* search_domains);

  std::vector<std::string> nameservers_;
  std::vector<std::string> search_domains_;

  base::Callback<void(std::vector<std::string> nameservers,
                      std::vector<std::string> search_domains)>
      config_changed_callback_;

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxy> manager_proxy_;

  base::WeakPtrFactory<ShillClient> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(ShillClient);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SHILL_CLIENT_H_
