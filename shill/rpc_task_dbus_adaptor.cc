// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/rpc_task_dbus_adaptor.h"

#include "shill/error.h"
#include "shill/logging.h"
#include "shill/rpc_task.h"

using std::map;
using std::string;

namespace shill {

// static
const char RPCTaskDBusAdaptor::kPath[] = "/task/";

RPCTaskDBusAdaptor::RPCTaskDBusAdaptor(DBus::Connection *conn, RPCTask *task)
    : DBusAdaptor(conn, kPath + task->UniqueName()),
      task_(task),
      interface_name_(SHILL_INTERFACE ".Task"),
      connection_name_(conn->unique_name()) {}

RPCTaskDBusAdaptor::~RPCTaskDBusAdaptor() {
  task_ = nullptr;
}

const string &RPCTaskDBusAdaptor::GetRpcIdentifier() {
  return DBus::Object::path();
}

const string &RPCTaskDBusAdaptor::GetRpcInterfaceIdentifier() {
  // TODO(petkov): We should be able to return DBus::Interface::name() or simply
  // name() and avoid the need for the |interface_name_| data member. However,
  // that's non-trivial due to multiple inheritance (crbug.com/209869).
  return interface_name_;
}

const string &RPCTaskDBusAdaptor::GetRpcConnectionIdentifier() {
  return connection_name_;
}

void RPCTaskDBusAdaptor::getsec(
    string &user, string &password, DBus::Error &error) {  // NOLINT
  SLOG(DBus, 2) << __func__ << ": " << user;
  task_->GetLogin(&user, &password);
}

void RPCTaskDBusAdaptor::notify(const string &reason,
                                const map<string, string> &dict,
                                DBus::Error &/*error*/) {  // NOLINT
  SLOG(DBus, 2) << __func__ << ": " << reason;
  task_->Notify(reason, dict);
}

}  // namespace shill
