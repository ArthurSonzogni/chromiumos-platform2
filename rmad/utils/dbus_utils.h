// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_DBUS_UTILS_H_
#define RMAD_UTILS_DBUS_UTILS_H_

#include <string>

#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <google/protobuf/message_lite.h>

namespace rmad {

scoped_refptr<dbus::Bus> GetSystemBus();

class DBusUtils {
 public:
  static constexpr int DBUS_DEFAULT_TIMEOUT_MS = 10000;  // 10 seconds

  DBusUtils() = default;
  virtual ~DBusUtils() = default;

  virtual bool CallDBusMethod(
      const std::string& service_name,
      const std::string& service_path,
      const std::string& interface_name,
      const std::string& method_name,
      const google::protobuf::MessageLite& request,
      google::protobuf::MessageLite* reply,
      int timeout_ms = DBUS_DEFAULT_TIMEOUT_MS) const = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_DBUS_UTILS_H_
