// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include <base/logging.h>

#include "chromeos-dbus-bindings/dbus_signature.h"

namespace chromeos_dbus_bindings {

namespace {

struct Environment {
  Environment() { logging::SetMinLogLevel(logging::LOG_FATAL); }
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  const std::string data_string(reinterpret_cast<const char*>(data), size);

  DBusSignature signature;
  signature.Parse(data_string);

  return 0;
}

}  // namespace chromeos_dbus_bindings
