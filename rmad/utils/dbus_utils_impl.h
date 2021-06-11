// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_DBUS_UTILS_IMPL_H_
#define RMAD_UTILS_DBUS_UTILS_IMPL_H_

#include "rmad/utils/dbus_utils.h"

#include <string>

#include <google/protobuf/message_lite.h>

namespace rmad {

class DBusUtilsImpl : public DBusUtils {
 public:
  DBusUtilsImpl() = default;
  ~DBusUtilsImpl() override = default;

  bool CallDBusMethod(const std::string& service_name,
                      const std::string& service_path,
                      const std::string& interface_name,
                      const std::string& method_name,
                      const google::protobuf::MessageLite& request,
                      google::protobuf::MessageLite* reply,
                      int timeout_ms) const override;
};

}  // namespace rmad

#endif  // RMAD_UTILS_DBUS_UTILS_IMPL_H_
