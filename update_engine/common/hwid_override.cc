// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/hwid_override.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/key_value_store.h>

using std::string;

namespace chromeos_update_engine {

const char HwidOverride::kHwidOverrideKey[] = "HWID_OVERRIDE";

HwidOverride::HwidOverride() {}

HwidOverride::~HwidOverride() {}

string HwidOverride::Read(const base::FilePath& root) {
  brillo::KeyValueStore lsb_release;
  lsb_release.Load(base::FilePath(root.value() + "/etc/lsb-release"));
  string result;
  if (lsb_release.GetString(kHwidOverrideKey, &result))
    return result;
  return "";
}

}  // namespace chromeos_update_engine
