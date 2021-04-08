// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UTILS_H_
#define BIOD_UTILS_H_

#include <string>
#include <type_traits>

namespace biod {

// This function can be used to fetch the value of an enum
// typecasted to it's base type (int if none specified).
//
// enum class FlockSize : uint8_t {
//   kOne = 1,
//   kTwo,
//   ...
// };
//
// uint8_t total_animals = to_utype(FlockSize::kTwo);
template <typename E>
constexpr auto to_utype(E enumerator) noexcept {
  return static_cast<std::underlying_type_t<E>>(enumerator);
}

/**
 * @brief Convert id to a privacy preserving identifier string.
 *
 * Log files are uploaded via crash reports and feedback reports.
 * This function helps ensure that the IDs logged are only unique within
 * a single crash/feedback report and not across many different reports.
 * Only use this string for logging purposes.
 *
 * @param id A plain text string id.
 * @return std::string The mutated loggable id string.
 */
std::string LogSafeID(const std::string& id);

}  // namespace biod

#endif  // BIOD_UTILS_H_
