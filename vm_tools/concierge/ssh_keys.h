// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SSH_KEYS_H_
#define VM_TOOLS_CONCIERGE_SSH_KEYS_H_

#include <string>

namespace vm_tools::concierge {

// Erases all of the SSH keys generated for the specified |vm_name|. Should be
// called when a VM disk image is destroyed. Returns false if there were any
// failures deleting the keys, true otherwise.
bool EraseGuestSshKeys(const std::string& cryptohome_id,
                       const std::string& vm_name);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_SSH_KEYS_H_
