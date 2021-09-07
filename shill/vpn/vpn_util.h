// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_UTIL_H_
#define SHILL_VPN_VPN_UTIL_H_

#include <sys/types.h>

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>

namespace shill {

// An interface to wrap some constants and functions which are shared by
// multiple VPN classes.
class VPNUtil {
 public:
  // User and group we use to run external binaries. Defined in user/vpn and
  // group/vpn in chromiumos/overlays/eclass-overlay/profiles/base/accounts
  // folder.
  static constexpr char kVPNUser[] = "vpn";
  static constexpr char kVPNGroup[] = "vpn";
  // TODO(191627520): gid should ideally be looked up at runtime.
  static constexpr gid_t kVPNGid = 20174;

  VPNUtil(const VPNUtil&) = delete;
  VPNUtil& operator=(const VPNUtil&) = delete;
  virtual ~VPNUtil() = default;

  static std::unique_ptr<VPNUtil> New();

  // Writes |contents| into file with path |filename|, changes the group of this
  // file to "vpn", and makes this file group-readable. Note that although shill
  // does not have CAP_CHOWN, the owner of a file may change the group of the
  // file to any group of which that owner is a member, so we can change the
  // group to "vpn" here since "shill" is a member of "vpn".
  virtual bool WriteConfigFile(const base::FilePath& filename,
                               const std::string& contents) const = 0;

  // Creates a scoped temp directory under |parent_path|, changes its group to
  // "vpn", and give it group RWX permission. This directory can be used to
  // share the config files between shill and the vpn process, or as the run
  // directory for the vpn process. If failed, returns an invalid ScopedTmpDir.
  virtual base::ScopedTempDir CreateScopedTempDir(
      const base::FilePath& parent_path) const = 0;

 protected:
  VPNUtil() = default;
};

}  // namespace shill

#endif  // SHILL_VPN_VPN_UTIL_H_
