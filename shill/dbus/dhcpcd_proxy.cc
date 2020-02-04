// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/dhcpcd_proxy.h"

#include <base/bind_helpers.h>

#include "shill/logging.h"

using std::string;
namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDHCP;
static string ObjectID(DHCPCDProxy* d) {
  return "(dhcpcd_proxy)";
}
}  // namespace Logging

DHCPCDProxy::DHCPCDProxy(const scoped_refptr<dbus::Bus>& bus,
                         const std::string& service_name)
    : dhcpcd_proxy_(new org::chromium::dhcpcdProxy(bus, service_name)) {
  SLOG(this, 2) << "DHCPCDProxy(service=" << service_name << ").";
  // Do not register signal handlers, signals are processed by
  // DHCPCDListener.
}

DHCPCDProxy::~DHCPCDProxy() {
  dhcpcd_proxy_->ReleaseObjectProxy(base::DoNothing());
}

void DHCPCDProxy::Rebind(const string& interface) {
  SLOG(DBus, nullptr, 2) << __func__;
  brillo::ErrorPtr error;
  if (!dhcpcd_proxy_->Rebind(interface, &error)) {
    LogDBusError(error, __func__, interface);
  }
}

void DHCPCDProxy::Release(const string& interface) {
  SLOG(DBus, nullptr, 2) << __func__;
  brillo::ErrorPtr error;
  if (!dhcpcd_proxy_->Release(interface, &error)) {
    LogDBusError(error, __func__, interface);
  }
}

void DHCPCDProxy::LogDBusError(const brillo::ErrorPtr& error,
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
