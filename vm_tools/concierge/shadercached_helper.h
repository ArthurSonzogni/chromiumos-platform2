// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SHADERCACHED_HELPER_H_
#define VM_TOOLS_CONCIERGE_SHADERCACHED_HELPER_H_

#include <string>

#include "base/files/file_path.h"

namespace vm_tools::concierge {

// Creates the shader-cache-specific shared data parameter for crosvm.
std::string CreateShaderSharedDataParam(base::FilePath data_dir);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_SHADERCACHED_HELPER_H_
