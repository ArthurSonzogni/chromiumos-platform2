// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_PERFETTO_PRODUCER_DBUS_REQUEST_H_
#define DBUS_PERFETTO_PRODUCER_DBUS_REQUEST_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <dbus/dbus.h>

struct ProcessInfo;

// Map from D-Bus well-known name to D-Bus unique name
using DBusNameMap = std::unordered_map<std::string, std::string>;

// Map from D-Bus unique name to ProcessInfo
using ProcessMap = std::unordered_map<std::string, ProcessInfo>;

// Map from message serial to process name of destination of the message
using MethodMap = std::unordered_map<uint64_t, std::string>;

struct ProcessInfo {
  uint32_t id;
  std::string name;
  std::unique_ptr<MethodMap> methods;
};

struct Maps {
  DBusNameMap names;
  ProcessMap processes;
};

bool StoreProcessesNames(DBusConnection*, DBusError*, Maps&);

#endif  // DBUS_PERFETTO_PRODUCER_DBUS_REQUEST_H_
