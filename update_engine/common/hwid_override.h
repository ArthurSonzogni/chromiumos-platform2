// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_HWID_OVERRIDE_H_
#define UPDATE_ENGINE_COMMON_HWID_OVERRIDE_H_

#include <map>
#include <string>

#include <base/files/file_path.h>

namespace chromeos_update_engine {

// Class that allows HWID to be read from <root>/etc/lsb-release.
class HwidOverride {
 public:
  HwidOverride();
  HwidOverride(const HwidOverride&) = delete;
  HwidOverride& operator=(const HwidOverride&) = delete;

  ~HwidOverride();

  // Read HWID from an /etc/lsb-release file under given root.
  // An empty string is returned if there is any error.
  static std::string Read(const base::FilePath& root);

  static const char kHwidOverrideKey[];
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_HWID_OVERRIDE_H_
