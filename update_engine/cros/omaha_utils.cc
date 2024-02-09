// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/omaha_utils.h"

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

namespace chromeos_update_engine {

const EolDate kEolDateInvalid = -9999;

std::string EolDateToString(EolDate eol_date) {
  return base::NumberToString(eol_date);
}

EolDate StringToEolDate(const std::string& eol_date) {
  EolDate date = kEolDateInvalid;
  if (!base::StringToInt64(eol_date, &date))
    return kEolDateInvalid;
  return date;
}

}  // namespace chromeos_update_engine
