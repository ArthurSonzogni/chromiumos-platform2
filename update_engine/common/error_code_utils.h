// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_ERROR_CODE_UTILS_H_
#define UPDATE_ENGINE_COMMON_ERROR_CODE_UTILS_H_

#include <initializer_list>
#include <string>

#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>

#include "update_engine/common/error_code.h"

namespace chromeos_update_engine {
namespace utils {

extern const char kCategoryPayload[];
extern const char kCategoryDownload[];
extern const char kCategoryVerity[];

extern const char kErrorMismatch[];
extern const char kErrorVerification[];
extern const char kErrorVersion[];
extern const char kErrorTimestamp[];
extern const char kErrorSignature[];
extern const char kErrorManifest[];

// Returns a string representation of the ErrorCodes (either the base
// error codes or the bit flags) for logging purposes.
std::string ErrorCodeToString(ErrorCode code);

// Logs an appropriate alert tag for a given error.
// Errors not deemed severe raise no alert logs.
void LogAlertTag(ErrorCode code);

// Create a tag that can be added to an Error log message to allow easier
// filtering from listnr logs. Expected to be used as the first field of a log
// message.
template <typename... Types>
std::string GenerateAlertTag(std::string category, Types... details) {
  return base::StringPrintf(
      "[UpdateEngineAlert<%s>] ",
      base::JoinString(std::initializer_list<std::string>{category, details...},
                       ":")
          .c_str());
}

}  // namespace utils
}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_ERROR_CODE_UTILS_H_
