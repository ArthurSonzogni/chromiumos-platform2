// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_MAKE_STAT_H_
#define FUSEBOX_MAKE_STAT_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <string>

namespace fusebox {

// Returns true if |mode| type is allowed.
bool IsAllowedStatMode(mode_t mode, mode_t allowed = S_IFREG | S_IFDIR);

// Returns |mode| with synthesized permission bits.
mode_t MakeStatModeBits(mode_t mode, bool read_only = false);

// Returns an inode |ino| stat with synthesized permission bits.
struct stat MakeStat(ino_t ino, const struct stat& s, bool read_only = false);

// Returns mode string.
std::string StatModeToString(mode_t mode);

}  // namespace fusebox

#endif  // FUSEBOX_MAKE_STAT_H_
