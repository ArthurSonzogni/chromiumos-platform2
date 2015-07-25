// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/chromeos_dhcpcd_proxy.h"

#include "shill/logging.h"

using std::string;
namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static string ObjectID(ChromeosDHCPCDProxy* d) { return "(dhcpcd_proxy)"; }
}

ChromeosDHCPCDProxy::ChromeosDHCPCDProxy(const scoped_refptr<dbus::Bus>& bus,
                                         const std::string& service_name)
    : dhcpcd_proxy_(
        new org::chromium::dhcpcdProxy(bus, service_name)) {
  SLOG(this, 2) << "DHCPCDProxy(service=" << service_name << ").";
  // Do not register signal handlers, signals are processed by
  // ChromeosDHCPCDListener.
}

ChromeosDHCPCDProxy::~ChromeosDHCPCDProxy() {}

void ChromeosDHCPCDProxy::Rebind(const string& interface) {
  SLOG(DBus, nullptr, 2) << __func__;
  chromeos::ErrorPtr error;
  if (!dhcpcd_proxy_->Rebind(interface, &error)) {
    LogDBusError(error, __func__, interface);
  }
}

void ChromeosDHCPCDProxy::Release(const string& interface) {
  SLOG(DBus, nullptr, 2) << __func__;
  chromeos::ErrorPtr error;
  if (!dhcpcd_proxy_->Release(interface, &error)) {
    LogDBusError(error, __func__, interface);
  }
}

void ChromeosDHCPCDProxy::LogDBusError(const chromeos::ErrorPtr& error,
                                       const string& method,
                                       const string& interface) {
  if (error->GetCode() == DBUS_ERROR_SERVICE_UNKNOWN ||
      error->GetCode() == DBUS_ERROR_NO_REPLY) {
    LOG(INFO) << method << ": dhcpcd daemon appears to have exited.";
  } else {
    LOG(FATAL) << "DBus error: " << method << " " << interface << ": "
               << error->GetCode() << ": " << error->GetMessage();
  }
}

}  // namespace shill
