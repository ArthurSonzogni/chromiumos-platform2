// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/machine_quirks.h"

#include <string_view>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/pattern.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"

namespace power_manager::system {

namespace {
// Default DMI ID directory
const base::FilePath kDefaultDmiIdDir("/sys/class/dmi/id/");

// Name of product name file for special suspend workarounds.
constexpr std::string_view kDefaultProductNameFile = "product_name";

// File containing the product names that require suspend-to-idle.
const base::FilePath kPowerManagerSuspendToIdleFile("suspend_to_idle_models");

// File containing the product names that require suspend blocking.
const base::FilePath kPowerManagerSuspendPreventionFile(
    "suspend_prevention_models");
}  // namespace

MachineQuirks::MachineQuirks() : dmi_id_dir_(kDefaultDmiIdDir) {}

void MachineQuirks::Init(PrefsInterface* prefs) {
  DCHECK(prefs);
  prefs_ = prefs;
}

void MachineQuirks::ApplyQuirksToPrefs() {
  DCHECK(prefs_) << "MachineQuirks::Init() wasn't called";
  bool machine_quirks_enabled = false;
  prefs_->GetBool(kHasMachineQuirksPref, &machine_quirks_enabled);
  if (!machine_quirks_enabled) {
    return;
  }

  if (IsSuspendBlocked()) {
    prefs_->SetInt64(kDisableIdleSuspendPref, 1);
    LOG(INFO) << "Disable Idle Suspend Pref set to enabled";
  }

  if (IsSuspendToIdle()) {
    prefs_->SetInt64(kSuspendToIdlePref, 1);
    LOG(INFO) << "Suspend to Idle Pref set to enabled";
  }

  if (IsExternalDisplayOnly()) {
    prefs_->SetInt64(kExternalDisplayOnlyPref, 1);
    LOG(INFO) << "ExternalDisplayOnly Pref set to enabled";
  }
}

bool MachineQuirks::IsSuspendBlocked() {
  DCHECK(prefs_) << "MachineQuirks::Init() wasn't called";

  std::string suspend_prevention_ids_pref;
  // Read suspend prevention ids pref.
  if (!prefs_->GetString(kSuspendPreventionListPref,
                         &suspend_prevention_ids_pref))
    return false;

  if (ContainsDMIMatch(suspend_prevention_ids_pref)) {
    return true;
  }

  // Normal case, no quirk is required.
  return false;
}

bool MachineQuirks::IsSuspendToIdle() {
  CHECK(prefs_) << "MachineQuirks::Init() wasn't called";

  std::string suspend_to_idle_ids_pref;
  // Read suspend prevention ids pref.
  if (!prefs_->GetString(kSuspendToIdleListPref, &suspend_to_idle_ids_pref))
    return false;

  if (ContainsDMIMatch(suspend_to_idle_ids_pref)) {
    return true;
  }

  // Normal case, no quirk is required.
  return false;
}

bool MachineQuirks::IsExternalDisplayOnly() {
  CHECK(prefs_) << "MachineQuirks::Init() wasn't called";

  std::string external_display_only_ids_pref;
  // Read external_display_only ids pref.
  if (!prefs_->GetString(kExternalDisplayOnlyListPref,
                         &external_display_only_ids_pref))
    return false;

  if (ContainsDMIMatch(external_display_only_ids_pref))
    return true;

  // Normal case, no quirk is required.
  return false;
}

bool MachineQuirks::ReadDMIValFromFile(std::string_view dmi_file_name,
                                       std::string* value_out) {
  const base::FilePath dmi_file(dmi_file_name);
  base::FilePath dmi_file_path = base::FilePath(dmi_id_dir_.Append(dmi_file));
  if (!util::ReadStringFile(dmi_file_path, value_out)) {
    return false;
  }
  base::TrimWhitespaceASCII(*value_out, base::TRIM_ALL, value_out);
  return true;
}

bool MachineQuirks::IsProductNameMatch(std::string_view product_name_pref) {
  std::string product_name_dut;
  if (!ReadDMIValFromFile(kDefaultProductNameFile, &product_name_dut))
    return false;
  if (base::MatchPattern(product_name_dut, product_name_pref)) {
    LOG(INFO) << "Quirk match found for product_name:" << product_name_dut;
    return true;
  }
  return false;
}

bool MachineQuirks::IsDMIMatch(std::string_view dmi_pref_entry) {
  // If the DMI entry doesn't follow the key:val format, that means that it just
  // contains the product_name, so do just a product_name match.
  if (dmi_pref_entry.find(':') == std::string::npos) {
    return IsProductNameMatch(dmi_pref_entry);
  }

  // If the DMI entry is in the key:val format, then we parse and match each
  // pair. Example: "board_name:A, product_family:B"

  base::StringPairs dmi_pairs;
  if (!base::SplitStringIntoKeyValuePairs(dmi_pref_entry, ':', ',',
                                          &dmi_pairs)) {
    LOG(INFO) << dmi_pref_entry
              << " in the DMI models list is incorrectly formatted.";
    return false;
  }

  // Return false if any DMI keyval fails to match with the DUT's DMI info
  for (const auto& dmi_keyval : dmi_pairs) {
    std::string dmi_val;
    if (!ReadDMIValFromFile(dmi_keyval.first, &dmi_val)) {
      LOG(INFO) << "Unable to read a DMI val for this model in the list: "
                << dmi_pref_entry
                << ". Please note that DMI values ending in _serial or _uuid "
                   "cannot be read by power_manager.";
      return false;
    }
    if (dmi_keyval.second != dmi_val)
      return false;
  }

  // If all the listed DMI values match, then we know it's a match!
  LOG(INFO) << "Quirk match found for DMI vals " << dmi_pref_entry;
  return true;
}

bool MachineQuirks::ContainsDMIMatch(std::string_view dmi_ids_pref) {
  // The DMI IDs pref is read from models.yaml as a pref and comes originally as
  // a single string before it is processed into a vector of strings.

  for (const auto& dmi_entry :
       base::SplitString(dmi_ids_pref, "\n", base::TRIM_WHITESPACE,
                         base::SPLIT_WANT_NONEMPTY)) {
    if (IsDMIMatch(dmi_entry)) {
      return true;
    }
  }
  return false;
}

}  // namespace power_manager::system
