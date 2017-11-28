// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_WEBSERVD_FIREWALL_INTERFACE_H_
#define WEBSERVER_WEBSERVD_FIREWALL_INTERFACE_H_

#include <inttypes.h>

#include <string>

#include <base/callback.h>
#include <base/macros.h>
#include <chromeos/dbus/dbus_object.h>

namespace webservd {

class FirewallInterface {
 public:
  virtual ~FirewallInterface() = default;

  // Wait for the firewall DBus service to be up.
  virtual void WaitForServiceAsync(dbus::Bus* bus,
                                   const base::Closure& callback) = 0;

  // Methods for managing firewall ports.
  virtual void PunchTcpHoleAsync(
      uint16_t port,
      const std::string& interface_name,
      const base::Callback<void(bool)>& success_cb,
      const base::Callback<void(chromeos::Error*)>& failure_cb) = 0;

 protected:
  FirewallInterface() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(FirewallInterface);
};

}  // namespace webservd

#endif  // WEBSERVER_WEBSERVD_FIREWALL_INTERFACE_H_
