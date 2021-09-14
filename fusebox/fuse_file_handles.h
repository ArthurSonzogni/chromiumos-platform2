// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_FUSE_FILE_HANDLES_H_
#define FUSEBOX_FUSE_FILE_HANDLES_H_

#include <stdint.h>

#include <base/files/scoped_file.h>

namespace fusebox {

// Returns a new open file handle, with optional backing fd.
uint64_t OpenFile(base::ScopedFD fd = {});

// Returns file |handle| if it is open, or 0 if not.
uint64_t GetFile(uint64_t handle);

// Returns backing fd if file |handle| is open, or -1 if not.
int GetFileDescriptor(uint64_t handle);

// Close the file |handle|. Returns its backing fd.
base::ScopedFD CloseFile(uint64_t handle);

}  // namespace fusebox

#endif  // FUSEBOX_FUSE_FILE_HANDLES_H_
