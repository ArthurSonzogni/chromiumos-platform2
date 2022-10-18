// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_LOGS_LOGS_CONSTANTS_H_
#define RMAD_LOGS_LOGS_CONSTANTS_H_

#include <base/values.h>

namespace rmad {

// JsonStore keys.
inline constexpr char kLogs[] = "logs";
inline constexpr char kEvents[] = "events";

// Event keys.
inline constexpr char kTimestamp[] = "timestamp";
inline constexpr char kType[] = "type";
inline constexpr char kDetails[] = "details";

// State transition keys.
inline constexpr char kFromStateId[] = "from_state_id";
inline constexpr char kToStateId[] = "to_state_id";

enum class LogEventType {
  kTransition = 0,
  kData = 1,
  kError = 2,
  kMaxValue = kError,
};

}  // namespace rmad

#endif  // RMAD_LOGS_LOGS_CONSTANTS_H_
