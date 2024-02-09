// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_OMAHA_UTILS_H_
#define UPDATE_ENGINE_CROS_OMAHA_UTILS_H_

#include <string>

namespace chromeos_update_engine {

using EolDate = int64_t;

// |EolDate| indicating an invalid end-of-life date.
extern const EolDate kEolDateInvalid;

// Returns the string representation of the |eol_date|.
std::string EolDateToString(EolDate eol_date);

// Converts the end-of-life date string to an EolDate numeric value. In case
// of an invalid string, the default |kEolDateInvalid| value will be used
// instead.
EolDate StringToEolDate(const std::string& eol_date);

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_OMAHA_UTILS_H_
