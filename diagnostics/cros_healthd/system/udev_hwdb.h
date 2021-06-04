// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_HWDB_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_HWDB_H_

#include <map>
#include <string>

namespace diagnostics {

// Interface for accessing the udev hwdb library.
class UdevHwdb {
 public:
  virtual ~UdevHwdb() = default;

  // Returns the properties map according to |modalias|.
  using PropertieType = std::map<std::string, std::string>;
  virtual PropertieType GetProperties(const std::string& modalias) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_UDEV_HWDB_H_
