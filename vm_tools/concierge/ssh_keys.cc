// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/ssh_keys.h"

#include <utility>
#include <vector>

#include <base/command_line.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_util.h>

#include "vm_tools/common/naming.h"

namespace vm_tools::concierge {

namespace {
// Daemon store base path.
constexpr char kCryptohomeRoot[] = "/run/daemon-store/crosvm";

// Dir name that all ssh keys are stored under.
constexpr char kSshKeysDir[] = "sshkeys";

// Separator between the encoded vm and container name in the filename. This
// also prevents a well-chosen vm/container name from colliding with 'host_key'.
constexpr char kVmContainerSeparator[] = "-";

}  // namespace

bool EraseGuestSshKeys(const std::string& cryptohome_id,
                       const std::string& vm_name) {
  // Look in the generated key directory for all keys that have the prefix
  // associated with this |vm_name| and erase them.
  bool rv = true;
  std::string encoded_vm = GetEncodedName(vm_name);
  std::string target_prefix = encoded_vm + kVmContainerSeparator;
  base::FilePath search_path =
      base::FilePath(kCryptohomeRoot).Append(cryptohome_id).Append(kSshKeysDir);
  base::FileEnumerator file_enum(search_path, false,
                                 base::FileEnumerator::FILES);
  for (base::FilePath enum_path = file_enum.Next(); !enum_path.empty();
       enum_path = file_enum.Next()) {
    if (base::StartsWith(enum_path.BaseName().value(), target_prefix,
                         base::CompareCase::SENSITIVE)) {
      // Found an ssh key for this VM, delete it.
      if (!base::DeleteFile(enum_path)) {
        PLOG(ERROR) << "Failed deleting generated SSH key for VM: "
                    << enum_path.value();
        rv = false;
      }
    }
  }
  return rv;
}

}  // namespace vm_tools::concierge
