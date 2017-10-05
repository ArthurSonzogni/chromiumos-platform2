// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_options.h"

#include <sys/mount.h>

#include <algorithm>

#include <base/strings/string_util.h>

using std::pair;
using std::string;
using std::vector;

namespace cros_disks {

const char MountOptions::kOptionBind[] = "bind";
const char MountOptions::kOptionDirSync[] = "dirsync";
const char MountOptions::kOptionFlush[] = "flush";
const char MountOptions::kOptionNoDev[] = "nodev";
const char MountOptions::kOptionNoExec[] = "noexec";
const char MountOptions::kOptionNoSuid[] = "nosuid";
const char MountOptions::kOptionReadOnly[] = "ro";
const char MountOptions::kOptionReadWrite[] = "rw";
const char MountOptions::kOptionRemount[] = "remount";
const char MountOptions::kOptionSynchronous[] = "sync";
const char MountOptions::kOptionUtf8[] = "utf8";

void MountOptions::Initialize(const vector<string>& options,
                              bool set_user_and_group_id,
                              const string& default_user_id,
                              const string& default_group_id) {
  options_.clear();
  options_.reserve(options.size());

  bool option_read_only = false, option_read_write = false;
  bool option_remount = false;
  string option_user_id, option_group_id;

  for (const auto& option : options) {
    // Skip early if |option| contains a comma.
    if (option.find(",") != string::npos) {
      LOG(WARNING) << "Ignoring invalid mount option '" << option << "'.";
      continue;
    }

    if (option == kOptionReadOnly) {
      option_read_only = true;
    } else if (option == kOptionReadWrite) {
      option_read_write = true;
    } else if (option == kOptionRemount) {
      option_remount = true;
    } else if (base::StartsWith(option,
                                "uid=", base::CompareCase::INSENSITIVE_ASCII)) {
      option_user_id = option;
    } else if (base::StartsWith(option,
                                "gid=", base::CompareCase::INSENSITIVE_ASCII)) {
      option_group_id = option;
    } else if (option == kOptionNoDev || option == kOptionNoExec ||
               option == kOptionNoSuid) {
      // We'll add these options unconditionally below.
      continue;
    } else if (option == kOptionBind || option == kOptionDirSync ||
               option == kOptionFlush || option == kOptionSynchronous ||
               option == kOptionUtf8 ||
               base::StartsWith(option, "shortname=",
                                base::CompareCase::INSENSITIVE_ASCII)) {
      // Only add options in the whitelist.
      options_.push_back(option);
    } else {
      // Never add unknown/non-whitelisted options.
      LOG(WARNING) << "Ignoring unsupported mount option '" << option << "'.";
    }
  }

  if (option_read_only || !option_read_write) {
    options_.push_back(kOptionReadOnly);
  } else {
    options_.push_back(kOptionReadWrite);
  }

  if (option_remount) {
    options_.push_back(kOptionRemount);
  }

  if (set_user_and_group_id) {
    if (!option_user_id.empty()) {
      options_.push_back(option_user_id);
    } else if (!default_user_id.empty()) {
      options_.push_back("uid=" + default_user_id);
    }

    if (!option_group_id.empty()) {
      options_.push_back(option_group_id);
    } else if (!default_group_id.empty()) {
      options_.push_back("gid=" + default_group_id);
    }
  }

  // Always set 'nodev', 'noexec', and 'nosuid'.
  options_.push_back(kOptionNoDev);
  options_.push_back(kOptionNoExec);
  options_.push_back(kOptionNoSuid);
}

bool MountOptions::IsReadOnlyOptionSet() const {
  for (vector<string>::const_reverse_iterator option_iterator =
           options_.rbegin();
       option_iterator != options_.rend(); ++option_iterator) {
    const string& option = *option_iterator;
    if (option == kOptionReadOnly)
      return true;

    if (option == kOptionReadWrite)
      return false;
  }
  return true;
}

void MountOptions::SetReadOnlyOption() {
  std::replace(options_.begin(), options_.end(), kOptionReadWrite,
               kOptionReadOnly);
}

pair<MountOptions::Flags, string> MountOptions::ToMountFlagsAndData() const {
  Flags flags = MS_RDONLY;
  vector<string> data;
  data.reserve(options_.size());

  for (const auto& option : options_) {
    if (option == kOptionReadOnly) {
      flags |= MS_RDONLY;
    } else if (option == kOptionReadWrite) {
      flags &= ~static_cast<Flags>(MS_RDONLY);
    } else if (option == kOptionRemount) {
      flags |= MS_REMOUNT;
    } else if (option == kOptionBind) {
      flags |= MS_BIND;
    } else if (option == kOptionDirSync) {
      flags |= MS_DIRSYNC;
    } else if (option == kOptionNoDev) {
      flags |= MS_NODEV;
    } else if (option == kOptionNoExec) {
      flags |= MS_NOEXEC;
    } else if (option == kOptionNoSuid) {
      flags |= MS_NOSUID;
    } else if (option == kOptionSynchronous) {
      flags |= MS_SYNCHRONOUS;
    } else {
      data.push_back(option);
    }
  }
  return std::make_pair(flags, base::JoinString(data, ","));
}

string MountOptions::ToString() const {
  return options_.empty() ? kOptionReadOnly : base::JoinString(options_, ",");
}

}  // namespace cros_disks
