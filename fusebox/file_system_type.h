// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_FILE_SYSTEM_TYPE_H_
#define FUSEBOX_FILE_SYSTEM_TYPE_H_

// Fusebox device nodes are S_IFDIR directory children of the FUSE root node.
// chrome::storage backends provide their content. Backend types are:

namespace fusebox {

static constexpr char kMTPType[] = "mtp";  // Media Transport Protocol
static constexpr char kADPType[] = "adp";  // Android Documents Provider
static constexpr char kFSPType[] = "fsp";  // File System Provider

}  // namespace fusebox

#endif  // FUSEBOX_FILE_SYSTEM_TYPE_H_
