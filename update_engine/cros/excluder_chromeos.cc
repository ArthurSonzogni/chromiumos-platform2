// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/excluder_chromeos.h"

#include <memory>
#include <string_view>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/system_state.h"

using std::string;
using std::vector;

namespace chromeos_update_engine {

std::unique_ptr<ExcluderInterface> CreateExcluder() {
  return std::make_unique<ExcluderChromeOS>();
}

bool ExcluderChromeOS::Exclude(const string& name) {
  auto* prefs = SystemState::Get()->prefs();
  auto key = prefs->CreateSubKey({kExclusionPrefsSubDir, name});
  return prefs->SetString(key, "");
}

bool ExcluderChromeOS::IsExcluded(const string& name) {
  auto* prefs = SystemState::Get()->prefs();
  auto key = prefs->CreateSubKey({kExclusionPrefsSubDir, name});
  return prefs->Exists(key);
}

bool ExcluderChromeOS::Reset() {
  auto* prefs = SystemState::Get()->prefs();
  bool ret = true;
  vector<string> keys;
  if (!prefs->GetSubKeys(kExclusionPrefsSubDir, &keys))
    return false;
  for (const auto& key : keys)
    if (!(ret &= prefs->Delete(key)))
      LOG(ERROR) << "Failed to delete exclusion pref for " << key;
  return ret;
}

}  // namespace chromeos_update_engine
