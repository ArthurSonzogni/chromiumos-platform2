// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/omaha_utils.h"

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

namespace chromeos_update_engine {

const DateType kInvalidDate = -9999;

std::string DateToString(DateType date) {
  return base::NumberToString(date);
}

DateType StringToDate(const std::string& date_str) {
  DateType date;
  if (!base::StringToInt64(date_str, &date))
    return kInvalidDate;
  return date;
}

}  // namespace chromeos_update_engine
