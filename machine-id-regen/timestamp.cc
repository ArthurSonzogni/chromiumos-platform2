// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "machine-id-regen/timestamp.h"

#include <memory>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time_delta_from_string.h>

namespace machineidregen {

std::optional<base::TimeDelta> Timestamp::get_last_update() {
  if (!base::PathExists(timestamp_path_)) {
    return std::nullopt;
  }

  std::string timestamp_raw;
  if (!base::ReadFileToStringWithMaxSize(timestamp_path_, &timestamp_raw,
                                         1024)) {
    return std::nullopt;
  }

  base::TrimString(timestamp_raw, "\n", &timestamp_raw);
  return base::TimeDeltaFromString(timestamp_raw + "s");
}

bool Timestamp::update(base::TimeDelta value) {
  std::string timestamp = base::NumberToString(value.InSeconds());
  return brillo::WriteToFileAtomic(timestamp_path_, timestamp.c_str(),
                                   timestamp.length(), 0644);
}

}  // namespace machineidregen
