// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_LOGGER_INTERFACE_H_
#define MODEMLOGGERD_LOGGER_INTERFACE_H_

#include <string>

#include <brillo/errors/error.h>

namespace modemloggerd {

class LoggerInterface {
 public:
  virtual brillo::ErrorPtr Start() = 0;
  virtual brillo::ErrorPtr Stop() = 0;
  virtual brillo::ErrorPtr SetOutputDir(const std::string& output_dir) = 0;

  virtual dbus::ObjectPath GetDBusPath() = 0;

  virtual ~LoggerInterface() = default;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_LOGGER_INTERFACE_H_
