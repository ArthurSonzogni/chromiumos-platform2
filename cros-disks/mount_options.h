// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_MOUNT_OPTIONS_H_
#define CROS_DISKS_MOUNT_OPTIONS_H_

#include <string>
#include <utility>
#include <vector>

namespace cros_disks {

// A class for holding and manipulating mount options.
class MountOptions {
 public:
  static const char kOptionBind[];
  static const char kOptionFlush[];
  static const char kOptionNoDev[];
  static const char kOptionNoExec[];
  static const char kOptionNoSuid[];
  static const char kOptionReadOnly[];
  static const char kOptionReadWrite[];
  static const char kOptionSynchronous[];
  static const char kOptionUtf8[];

  MountOptions() {}
  ~MountOptions() {}

  // Initializes the mount options with a list of option strings.
  //
  // If set_user_and_group_id is set to true, uid and gid options are set
  // if provided.
  //
  // If default_user_id is set to a non-empty value, it is added to the
  // mount options if no uid option is found in the option strings.
  // default_group_id is handled similarly.
  void Initialize(const std::vector<std::string>& options,
                  bool set_user_and_group_id,
                  const std::string& default_user_id,
                  const std::string& default_group_id);

  // Returns true if the read-only option is set.
  bool IsReadOnlyOptionSet() const;

  // Forces the read-only option to be set.
  void SetReadOnlyOption();

  // Converts the mount options into mount flags and data that are used by
  // the mount() library call.
  std::pair<unsigned long, std::string> ToMountFlagsAndData() const;

  // Converts the mount options into a comma-separated string.
  std::string ToString() const;

 private:
  // List of mount options.
  std::vector<std::string> options_;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_MOUNT_OPTIONS_H_
