// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_MOUNTS_H_
#define SECANOMALYD_MOUNTS_H_

#include <string>
#include <vector>

#include <base/optional.h>

#include "secanomalyd/mount_entry.h"

namespace secanomalyd {

using MountEntries = std::vector<MountEntry>;
using MaybeMountEntries = base::Optional<MountEntries>;

enum class MountFilter { kAll = 0, kUploadableOnly };

MaybeMountEntries ReadMounts(MountFilter filter);
// Used mostly for testing.
MaybeMountEntries ReadMountsFromString(const std::string& mounts,
                                       MountFilter filter);

}  // namespace secanomalyd

#endif  // SECANOMALYD_MOUNTS_H_
