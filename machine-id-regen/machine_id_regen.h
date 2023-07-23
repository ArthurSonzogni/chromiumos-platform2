// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MACHINE_ID_REGEN_MACHINE_ID_REGEN_H_
#define MACHINE_ID_REGEN_MACHINE_ID_REGEN_H_

#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/file_utils.h>
#include <metrics/metrics_library.h>

namespace machineidregen {

bool regen_machine_id(const base::FilePath& state_dir,
                      const base::FilePath& machine_id_file,
                      const std::string& reason,
                      std::shared_ptr<MetricsLibrary> metrics_lib,
                      base::TimeDelta minimum_age_seconds);

}  // namespace machineidregen

#endif  // MACHINE_ID_REGEN_MACHINE_ID_REGEN_H_
