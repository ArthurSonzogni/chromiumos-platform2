// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_FAKE_VPN_UTIL_H_
#define SHILL_VPN_FAKE_VPN_UTIL_H_

#include "shill/vpn/vpn_util.h"

#include <sys/types.h>

#include <string>
#include <memory>

#include <base/files/file_path.h>

namespace shill {

class FakeVPNUtil : public VPNUtil {
 public:
  FakeVPNUtil() = default;
  FakeVPNUtil(const FakeVPNUtil&) = delete;
  FakeVPNUtil& operator=(const FakeVPNUtil&) = delete;
  ~FakeVPNUtil() = default;

  // Writes |contents| into file with path |filename| without changing
  // permissions.
  bool WriteConfigFile(const base::FilePath& filename,
                       const std::string& contents) const override;

  // Creates a ScopedTempDir under |parent_path| without changing permissions.
  base::ScopedTempDir CreateScopedTempDir(
      const base::FilePath& parent_path) const override;
};

}  // namespace shill

#endif  // SHILL_VPN_FAKE_VPN_UTIL_H_
