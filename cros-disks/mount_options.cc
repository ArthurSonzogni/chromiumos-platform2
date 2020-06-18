// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_options.h"

#include <sys/mount.h>

#include <algorithm>

#include <base/strings/string_util.h>
#include <base/stl_util.h>

#include "cros-disks/quote.h"

#ifndef MS_NOSYMFOLLOW
// Added locally in kernel 5.4, upstream TBD.
#define MS_NOSYMFOLLOW 256
#endif

namespace cros_disks {

const char MountOptions::kOptionBind[] = "bind";
const char MountOptions::kOptionDirSync[] = "dirsync";
const char MountOptions::kOptionFlush[] = "flush";
const char MountOptions::kOptionNoDev[] = "nodev";
const char MountOptions::kOptionNoExec[] = "noexec";
const char MountOptions::kOptionNoSuid[] = "nosuid";
const char MountOptions::kOptionNoSymFollow[] = "nosymfollow";
const char MountOptions::kOptionReadOnly[] = "ro";
const char MountOptions::kOptionReadWrite[] = "rw";
const char MountOptions::kOptionRemount[] = "remount";
const char MountOptions::kOptionSynchronous[] = "sync";
const char MountOptions::kOptionUtf8[] = "utf8";

namespace {
const char kOptionUidPrefix[] = "uid=";
const char kOptionGidPrefix[] = "gid=";
const char kOptionShortNamePrefix[] = "shortname=";
const char kOptionTimeOffsetPrefix[] = "time_offset=";
}  // namespace

MountOptions::MountOptions()
    : whitelist_exact_({kOptionBind, kOptionDirSync, kOptionFlush,
                        kOptionSynchronous, kOptionUtf8}),
      whitelist_prefix_({kOptionShortNamePrefix, kOptionTimeOffsetPrefix}),
      enforced_options_({kOptionNoDev, kOptionNoExec, kOptionNoSuid}) {}

MountOptions::~MountOptions() = default;

void MountOptions::Initialize(const std::vector<std::string>& options,
                              bool set_user_and_group_id,
                              const std::string& default_user_id,
                              const std::string& default_group_id) {
  options_.clear();
  options_.reserve(options.size());

  bool option_read_only = false, option_read_write = false;
  bool option_remount = false;
  std::string option_user_id, option_group_id;

  for (const auto& option : options) {
    // Skip early if |option| contains a comma.
    if (option.find(",") != std::string::npos) {
      LOG(WARNING) << "Ignoring invalid mount option " << quote(option);
      continue;
    }

    if (option == kOptionReadOnly) {
      option_read_only = true;
    } else if (option == kOptionReadWrite) {
      option_read_write = true;
    } else if (option == kOptionRemount) {
      option_remount = true;
    } else if (base::StartsWith(option, kOptionUidPrefix,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      option_user_id = option;
    } else if (base::StartsWith(option, kOptionGidPrefix,
                                base::CompareCase::INSENSITIVE_ASCII)) {
      option_group_id = option;
    } else if (base::Contains(enforced_options_, option)) {
      // We'll add these options unconditionally below.
      continue;
    } else if (base::Contains(whitelist_exact_, option)) {
      // Only add options in the whitelist.
      options_.push_back(option);
    } else if (std::find_if(whitelist_prefix_.begin(), whitelist_prefix_.end(),
                            [option](const auto& s) {
                              return base::StartsWith(
                                  option, s,
                                  base::CompareCase::INSENSITIVE_ASCII);
                            }) != whitelist_prefix_.end()) {
      // Only add options in the whitelist.
      options_.push_back(option);
    } else {
      // Never add unknown/non-whitelisted options.
      LOG(WARNING) << "Ignoring unsupported mount option " << quote(option);
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
      options_.push_back(kOptionUidPrefix + default_user_id);
    }

    if (!option_group_id.empty()) {
      options_.push_back(option_group_id);
    } else if (!default_group_id.empty()) {
      options_.push_back(kOptionGidPrefix + default_group_id);
    }
  }

  std::copy(enforced_options_.begin(), enforced_options_.end(),
            std::back_inserter(options_));
}

bool MountOptions::IsReadOnlyOptionSet() const {
  for (std::vector<std::string>::const_reverse_iterator option_iterator =
           options_.rbegin();
       option_iterator != options_.rend(); ++option_iterator) {
    const std::string& option = *option_iterator;
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

std::pair<MountOptions::Flags, std::string> MountOptions::ToMountFlagsAndData()
    const {
  Flags flags = MS_RDONLY;
  std::vector<std::string> data;
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
    } else if (option == kOptionNoSymFollow) {
      flags |= MS_NOSYMFOLLOW;
      // Pass the nosymfollow option as both a flag and a string option for
      // compatibility across kernels.  The mount syscall ignores unknown flags,
      // so kernels that don't have MS_NOSYMFOLLOW will pick up nosymfollow from
      // the data parameter through the chromiumos LSM.  Kernels that do have
      // MS_NOSYMFOLLOW will pick up the same behavior directly from the flag;
      // our LSM ignores the string option in that case.
      //
      // TODO(b/152074038): Remove the string option once all devices have been
      // upreved to a kernel that supports MS_NOSYMFOLLOW (currently 5.4+).
      data.push_back(option);
    } else {
      data.push_back(option);
    }
  }
  return std::make_pair(flags, base::JoinString(data, ","));
}

std::string MountOptions::ToString() const {
  return options_.empty() ? kOptionReadOnly : base::JoinString(options_, ",");
}

void MountOptions::WhitelistOption(const std::string& option) {
  whitelist_exact_.push_back(option);
}

void MountOptions::WhitelistOptionPrefix(const std::string& prefix) {
  whitelist_prefix_.push_back(prefix);
}

void MountOptions::EnforceOption(const std::string& option) {
  enforced_options_.push_back(option);
}

bool MountOptions::HasOption(const std::string& option) const {
  return base::Contains(options_, option);
}

}  // namespace cros_disks
