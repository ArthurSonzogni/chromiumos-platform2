// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_EDID_H_
#define RUNTIME_PROBE_FUNCTIONS_EDID_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Parse EDID files from DRM devices in sysfs.
//
// @param edid_patterns a list of paths to be evaluated. (Default:
// {"/sys/class/drm/*/edid"})
class EdidFunction final : public PrivilegedProbeFunction {
 public:
  NAME_PROBE_FUNCTION("edid");

  template <typename T>
  static auto FromKwargsValue(const base::Value& dict_value) {
    constexpr auto kSysfsEdidPath = "/sys/class/drm/*/edid";
    PARSE_BEGIN();
    PARSE_ARGUMENT(edid_patterns, {kSysfsEdidPath});
    PARSE_END();
  }

 private:
  DataType EvalImpl() const override;

  // The path of target edid files, can contain wildcard.
  std::vector<std::string> edid_patterns_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_EDID_H_
