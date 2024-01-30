// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_UNTRUSTED_VM_UTILS_H_
#define VM_TOOLS_CONCIERGE_UNTRUSTED_VM_UTILS_H_

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>

#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge {

// Used to check for, and if needed enable, the conditions required for
// untrusted VMs.
class UntrustedVMUtils {
 public:
  // Used to represent kernel version.
  using KernelVersionAndMajorRevision = std::pair<int, int>;

  // Returns the current kernel version. If there is a failure to retrieve the
  // version it returns <INT_MIN, INT_MIN>.
  static KernelVersionAndMajorRevision GetKernelVersion();

  UntrustedVMUtils();
  UntrustedVMUtils(const UntrustedVMUtils&) = delete;
  UntrustedVMUtils& operator=(const UntrustedVMUtils&) = delete;
  virtual ~UntrustedVMUtils() = default;

  // Mitigation status for L1TF and MDS vulnerabilities.
  enum class MitigationStatus {
    // The host is not vulnerable.
    NOT_VULNERABLE,

    // The host is vulnerable.
    VULNERABLE,

    // The host is vulnerable but can be secure if SMT is disabled on the host.
    VULNERABLE_DUE_TO_SMT_ENABLED,
  };

  // Returns the mitigation status for untrusted VMs based on the following
  // checks
  // - Check if L1TF is mitigated.
  // - Check if MDS is mitigated.
  //
  // virtual for testing.
  virtual MitigationStatus CheckUntrustedVMMitigationStatus() const;

  // Returns whether an untrusted VM is allowed on the host depending on the
  // security patches are applied.
  bool IsUntrustedVMAllowed(std::string* reason) const;

 protected:
  // Protected constructor for testing.
  UntrustedVMUtils(base::FilePath l1tf_status_path,
                   base::FilePath mds_status_path);

 private:
  // Path to read L1TF vulnerability status from.
  base::FilePath l1tf_status_path_;

  // Path to read MDS vulnerability status from.
  base::FilePath mds_status_path_;
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_UNTRUSTED_VM_UTILS_H_
