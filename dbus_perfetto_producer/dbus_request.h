// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_PERFETTO_PRODUCER_DBUS_REQUEST_H_
#define DBUS_PERFETTO_PRODUCER_DBUS_REQUEST_H_

#include <string>
#include <unordered_map>

#include <dbus/dbus.h>

struct ProcessInfo {
  uint32_t uuid;
  std::string name;
};

using ProcessMap = std::unordered_map<std::string, ProcessInfo>;

bool StoreProcessesNames(DBusConnection*, DBusError*, ProcessMap*);

#endif  // DBUS_PERFETTO_PRODUCER_DBUS_REQUEST_H_
