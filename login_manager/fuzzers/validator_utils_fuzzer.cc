// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/logging.h>

#include "login_manager/validator_utils.h"

// Disable logging.
struct Environment {
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  std::string input(reinterpret_cast<const char*>(data), size);

  login_manager::ValidateEmail(input);
  login_manager::ValidateAccountIdKey(input);
  login_manager::ValidateExtensionId(input);
  login_manager::ValidateAccountId(input, nullptr);

  return 0;
}
