// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DNS_PROXY_PROXY_H_
#define DNS_PROXY_PROXY_H_

#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/files/scoped_file.h>
#include <brillo/daemons/dbus_daemon.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <shill/dbus/client/client.h>

namespace dns_proxy {

// The process that runs the actual proxying code.
class Proxy : public brillo::DBusDaemon {
 public:
  enum class Type { kSystem, kDefault, kARC };

  struct Options {
    Type type;
    // Required for ARC proxies as it specifies which physical interface
    // should (always) be tracked. This field is ignored (but should be empty)
    // for the system and default network proxies.
    std::string ifname;
  };

  explicit Proxy(const Options& opts);
  Proxy(const Proxy&) = delete;
  Proxy& operator=(const Proxy&) = delete;
  ~Proxy() = default;

  static const char* TypeToString(Type t);
  static std::optional<Type> StringToType(const std::string& s);

 protected:
  int OnInit() override;
  void OnShutdown(int*) override;

 private:
  void Setup();
  void OnPatchpanelReady(bool success);

  // The system proxy needs to pay attention to the default service type
  // switching to VPN since it wants to always keep the DNS configuration for
  // the underlying physical network.
  void OnDefaultServiceChanged(const std::string& type);

  // Used to detect changes to the DNS configuration of interface(s) of interest
  // to the proxy.
  void OnDeviceChanged(bool is_default,
                       const shill::Client::Device* const device);

  // Helper func for setting the dns-proxy address in shill.
  // Only valid for the system proxy.
  bool SetShillProperty(const std::string& addr);

  const Options opts_;
  std::unique_ptr<patchpanel::Client> patchpanel_;
  std::unique_ptr<shill::Client> shill_;

  base::ScopedFD ns_fd_;

  base::WeakPtrFactory<Proxy> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream, Proxy::Type type);
std::ostream& operator<<(std::ostream& stream, Proxy::Options opt);

}  // namespace dns_proxy

#endif  // DNS_PROXY_PROXY_H_
