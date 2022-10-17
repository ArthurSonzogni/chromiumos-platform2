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

std::string MatchResultToString(int result) {
  switch (result) {
    case EC_MKBP_FP_ERR_MATCH_NO:
      return "No match";
    case EC_MKBP_FP_ERR_MATCH_NO_INTERNAL:
      return "Internal error";
    case EC_MKBP_FP_ERR_MATCH_NO_TEMPLATES:
      return "No templates";
    case EC_MKBP_FP_ERR_MATCH_NO_LOW_QUALITY:
      return "Low quality";
    case EC_MKBP_FP_ERR_MATCH_NO_LOW_COVERAGE:
      return "Low coverage";
    case EC_MKBP_FP_ERR_MATCH_YES:
      return "Finger matched";
    case EC_MKBP_FP_ERR_MATCH_YES_UPDATED:
      return "Finger matched, template updated";
    case EC_MKBP_FP_ERR_MATCH_YES_UPDATE_FAILED:
      return "Finger matched, template updated failed";
    default:
      return "Unknown matcher result";
  }
}

}  // namespace biod
