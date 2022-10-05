// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/logging.h>
#include <libec/ec_command.h>

#include "biod/utils.h"

namespace biod {

std::string LogSafeID(const std::string& id) {
  // Truncate the string to the first 2 chars without extending to 2 chars.
  if (id.length() > 2) {
    return id.substr(0, 2) + "*";
  }
  return id;
}

void LogOnSignalConnected(const std::string& interface_name,
                          const std::string& signal_name,
                          bool success) {
  if (!success)
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
}

std::string EnrollResultToString(int result) {
  switch (result) {
    case EC_MKBP_FP_ERR_ENROLL_OK:
      return "Success";
    case EC_MKBP_FP_ERR_ENROLL_LOW_QUALITY:
      return "Low quality";
    case EC_MKBP_FP_ERR_ENROLL_IMMOBILE:
      return "Same area";
    case EC_MKBP_FP_ERR_ENROLL_LOW_COVERAGE:
      return "Low coverage";
    case EC_MKBP_FP_ERR_ENROLL_INTERNAL:
      return "Internal error";
    default:
      return "Unknown enrollment result";
  }
}

}  // namespace biod
