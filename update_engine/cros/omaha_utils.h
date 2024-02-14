// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_OMAHA_UTILS_H_
#define UPDATE_ENGINE_CROS_OMAHA_UTILS_H_

#include <string>

namespace chromeos_update_engine {

using DateType = int64_t;

// Indicate invalid date.
extern const DateType kInvalidDate;

// Returns the string representation of `DateType`.
std::string DateToString(DateType date);

// Converts the Unix Epoch date string to `DateType` value.
// In case of an invalid string, the default `kInvalidDate` value will be used.
DateType StringToDate(const std::string& date);

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_OMAHA_UTILS_H_
