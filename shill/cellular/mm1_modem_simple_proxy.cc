// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/mm1_modem_simple_proxy.h"

#include <memory>

#include "shill/cellular/cellular_error.h"
#include "shill/dbus_async_call_helper.h"
#include "shill/logging.h"

using std::string;
using std::unique_ptr;

namespace shill {

namespace mm1 {

ModemSimpleProxy::ModemSimpleProxy(DBus::Connection *connection,
                                   const string &path,
                                   const string &service)
    : proxy_(connection, path, service) {}

ModemSimpleProxy::~ModemSimpleProxy() {}

void ModemSimpleProxy::Connect(
    const DBusPropertiesMap &properties,
    Error *error,
    const DBusPathCallback &callback,
    int timeout) {
  BeginAsyncDBusCall(__func__, proxy_, &Proxy::ConnectAsync, callback,
                     error, &CellularError::FromMM1DBusError, timeout,
                     properties);
}

void ModemSimpleProxy::Disconnect(const ::DBus::Path &bearer,
                                  Error *error,
                                  const ResultCallback &callback,
                                  int timeout) {
  BeginAsyncDBusCall(__func__, proxy_, &Proxy::DisconnectAsync, callback,
                     error, &CellularError::FromMM1DBusError, timeout,
                     bearer);
}

void ModemSimpleProxy::GetStatus(Error *error,
                                 const DBusPropertyMapCallback &callback,
                                 int timeout) {
  BeginAsyncDBusCall(__func__, proxy_, &Proxy::GetStatusAsync, callback,
                     error, &CellularError::FromMM1DBusError, timeout);
}


// ModemSimpleProxy::Proxy
ModemSimpleProxy::Proxy::Proxy(DBus::Connection *connection,
                               const std::string &path,
                               const std::string &service)
    : DBus::ObjectProxy(*connection, path, service.c_str()) {}

ModemSimpleProxy::Proxy::~Proxy() {}

// Method callbacks inherited from
// org::freedesktop::ModemManager1::Modem::ModemSimpleProxy
void ModemSimpleProxy::Proxy::ConnectCallback(const ::DBus::Path &bearer,
                                              const ::DBus::Error &dberror,
                                              void *data) {
  SLOG(&bearer, 2) << __func__;
  unique_ptr<DBusPathCallback> callback(
      reinterpret_cast<DBusPathCallback *>(data));
  Error error;
  CellularError::FromMM1DBusError(dberror, &error);
  callback->Run(bearer, error);
}

void ModemSimpleProxy::Proxy::DisconnectCallback(const ::DBus::Error &dberror,
                                                 void *data) {
  SLOG(&path(), 2) << __func__;
  unique_ptr<ResultCallback> callback(reinterpret_cast<ResultCallback *>(data));
  Error error;
  CellularError::FromMM1DBusError(dberror, &error);
  callback->Run(error);
}

void ModemSimpleProxy::Proxy::GetStatusCallback(
    const DBusPropertiesMap &properties,
    const ::DBus::Error &dberror,
    void *data) {
  SLOG(&path(), 2) << __func__;
  unique_ptr<DBusPropertyMapCallback> callback(
      reinterpret_cast<DBusPropertyMapCallback *>(data));
  Error error;
  CellularError::FromMM1DBusError(dberror, &error);
  callback->Run(properties, error);
}

}  // namespace mm1
}  // namespace shill
