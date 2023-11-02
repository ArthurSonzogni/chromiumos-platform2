//
// Copyright (C) 2012 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

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
