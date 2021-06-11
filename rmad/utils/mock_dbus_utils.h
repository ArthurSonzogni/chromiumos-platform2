// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_MOCK_DBUS_UTILS_H_
#define RMAD_UTILS_MOCK_DBUS_UTILS_H_

#include "rmad/utils/dbus_utils.h"

#include <string>

#include <gmock/gmock.h>
#include <google/protobuf/message_lite.h>

namespace rmad {

class MockDBusUtils : public DBusUtils {
 public:
  MockDBusUtils() = default;
  ~MockDBusUtils() override = default;

  MOCK_METHOD(bool,
              CallDBusMethod,
              (const std::string&,
               const std::string&,
               const std::string&,
               const std::string&,
               const google::protobuf::MessageLite&,
               google::protobuf::MessageLite*,
               int),
              (const, override));
};

}  // namespace rmad

#endif  // RMAD_UTILS_MOCK_DBUS_UTILS_H_
