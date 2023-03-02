// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/shadercached_helper.h"

#include "base/strings/strcat.h"

namespace vm_tools::concierge {

namespace {

// Map root to nobody(65534), map chronos(1000) (user inside Borealis) to
// shadercached(333). VM having full CRUD access to the shared directory is fine
// because the shared path is wrapped inside a directory with correct
// permissions that is only editable by host shadercached. Mapping VM user to
// shadercached ensures shadercached has full access to all files and
// directories created by the VM.
constexpr char kShadercachedUidMap[] = "0 65534 1,1000 333 1";
constexpr char kShadercachedGidMap[] = "0 65534 1,1000 333 1";
constexpr char kShaderSharedDirTag[] = "precompiled_gpu_cache";

}  // namespace

std::string CreateShaderSharedDataParam(base::FilePath data_dir) {
  // Write performance is not a concern, we only need to make sure if a write
  // happens from the guest side, it is guaranteed to be persisted in the host.
  return base::StrCat({data_dir.value(), ":", kShaderSharedDirTag, ":uidmap=",
                       kShadercachedUidMap, ":gidmap=", kShadercachedGidMap,
                       ":type=fs", ":cache=never", ":timeout=1",
                       ":rewrite-security-xattrs=false", ":writeback=false",
                       ":ascii_casefold=false"});
}

}  // namespace vm_tools::concierge
