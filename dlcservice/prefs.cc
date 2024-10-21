// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/prefs.h"

#include <optional>

#include <base/logging.h>
#include <brillo/files/file_util.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"

using base::FilePath;
using std::string;

namespace dlcservice {

const char kDlcPrefVerified[] = "verified";
const char kDlcPrefVerifiedValueFile[] = "/etc/lsb-release";
const char kDlcRootMount[] = "root_mount";
const char kUserPrefsDir[] = "prefs";

Prefs::Prefs(const base::FilePath& prefs_root) : prefs_root_(prefs_root) {}

Prefs::Prefs(const DlcBase& dlc, BootSlot::Slot slot)
    : Prefs(JoinPaths(SystemState::Get()->dlc_prefs_dir(),
                      dlc.GetId(),
                      BootSlot::ToString(slot))) {}

// static
std::optional<Prefs> Prefs::CreatePrefs(const DlcInterface* dlc,
                                        BootSlot::Slot slot) {
  if (!dlc) {
    return std::nullopt;
  }

  auto prefs_dir = SystemState::Get()->dlc_prefs_dir();
  if (dlc->IsUserTied()) {
    const auto& daemon_store = GetDaemonStorePath();
    if (daemon_store.empty()) {
      return std::nullopt;
    }

    prefs_dir = JoinPaths(daemon_store, kUserPrefsDir);
  }
  return std::make_optional<Prefs>(
      JoinPaths(prefs_dir, dlc->GetId(), BootSlot::ToString(slot)));
}

bool Prefs::SetKey(const string& key, const string& value) {
  if (!base::DirectoryExists(prefs_root_)) {
    if (!CreateDir(prefs_root_)) {
      PLOG(ERROR) << "Failed to create prefs root=" << prefs_root_.value();
      return false;
    }
  }
  auto key_path = JoinPaths(prefs_root_, key);
  if (!WriteToFile(key_path, value)) {
    PLOG(ERROR) << "Failed to write to prefs file=" << key_path.value();
    return false;
  }
  return true;
}

bool Prefs::GetKey(const string& key, string* value) {
  auto key_path = JoinPaths(prefs_root_, key);
  if (!base::ReadFileToString(key_path, value)) {
    PLOG(ERROR) << "Failed to read from prefs file=" << key_path.value();
    return false;
  }
  return true;
}

bool Prefs::Create(const string& key) {
  return SetKey(key, "");
}

bool Prefs::Exists(const string& key) {
  return base::PathExists(JoinPaths(prefs_root_, key));
}

bool Prefs::Delete(const string& key) {
  return brillo::DeletePathRecursively(JoinPaths(prefs_root_, key));
}

}  // namespace dlcservice
