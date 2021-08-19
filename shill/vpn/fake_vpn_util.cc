// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/fake_vpn_util.h"

#include <base/files/file_util.h>

namespace shill {

bool FakeVPNUtil::WriteConfigFile(const base::FilePath& filename,
                                  const std::string& contents) const {
  return base::WriteFile(filename, contents);
}

}  // namespace shill
