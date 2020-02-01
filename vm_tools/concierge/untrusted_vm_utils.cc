// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/untrusted_vm_utils.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>
#include <base/files/file_util.h>
#include <base/strings/string_piece.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/scoped_dbus_error.h>

namespace vm_tools {
namespace concierge {

namespace {

// Scheduler configuration to be passed to the debugd API to disable SMT on the
// device.
const char kSchedulerConfigurationConservative[] = "conservative";

// Error returned by debugd::SetSchedulerConfigurationV2 API if SMT is not
// supported by the host.
const char kInvalidArchitectureErrorMsg[] = "Invalid architecture";

// Returns the L1TF mitigation status of the host it's run on.
UntrustedVMUtils::MitigationStatus GetL1TFMitigationStatus(
    const base::FilePath& l1tf_status_path) {
  std::string l1tf_status;
  if (!base::ReadFileToString(l1tf_status_path, &l1tf_status)) {
    LOG(ERROR) << "Failed to read L1TF status";
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  }

  std::vector<base::StringPiece> l1tf_statuses = base::SplitStringPiece(
      l1tf_status, ",;", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  // The sysfs file should always return up to 3 statuses and no more.
  DCHECK_LE(l1tf_statuses.size(), 3);

  const base::StringPiece& processor_mitigation_status = l1tf_statuses[0];
  if (processor_mitigation_status == "Not affected")
    return UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE;
  if (processor_mitigation_status != "Mitigation: PTE Inversion")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;

  const base::StringPiece& vmx_mitigation_status = l1tf_statuses[1];
  if (vmx_mitigation_status == "VMX: vulnerable")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  if (vmx_mitigation_status == "VMX: conditional cache flushes")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  if (vmx_mitigation_status != "VMX: cache flushes")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;

  const base::StringPiece& smt_mitigation_status = l1tf_statuses[2];
  if (smt_mitigation_status == "SMT vulnerable")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED;
  if (smt_mitigation_status != "SMT disabled")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;

  return UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE;
}

// Returns the MDS mitigation status of the host it's run on.
UntrustedVMUtils::MitigationStatus GetMDSMitigationStatus(
    const base::FilePath& mds_status_path) {
  std::string mds_status;
  if (!base::ReadFileToString(mds_status_path, &mds_status)) {
    LOG(ERROR) << "Failed to read L1TF status";
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  }

  std::vector<base::StringPiece> mds_statuses = base::SplitStringPiece(
      mds_status, ",;", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  // The sysfs file should always return up to 2 statuses and no more.
  DCHECK_LE(mds_statuses.size(), 2);

  const base::StringPiece& processor_mitigation_status = mds_statuses[0];
  if (processor_mitigation_status == "Not affected")
    return UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE;
  if (processor_mitigation_status.find("Vulnerable") != base::StringPiece::npos)
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  if (processor_mitigation_status != "Mitigation: Clear CPU buffers")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;

  const base::StringPiece& smt_mitigation_status = mds_statuses[1];
  if (smt_mitigation_status == "SMT vulnerable")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED;
  if (smt_mitigation_status == "SMT mitigated")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED;
  if (smt_mitigation_status == "SMT Host state unknown")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED;
  if (smt_mitigation_status != "SMT disabled")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;

  return UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE;
}

}  // namespace

UntrustedVMUtils::UntrustedVMUtils(
    dbus::ObjectProxy* debugd_proxy,
    KernelVersionAndMajorRevision host_kernel_version,
    KernelVersionAndMajorRevision min_needed_version,
    const base::FilePath& l1tf_status_path,
    const base::FilePath& mds_status_path)
    : debugd_proxy_(debugd_proxy),
      host_kernel_version_(host_kernel_version),
      min_needed_version_(min_needed_version),
      l1tf_status_path_(l1tf_status_path),
      mds_status_path_(mds_status_path) {
  DCHECK(!l1tf_status_path.empty());
  DCHECK(!mds_status_path.empty());
}

UntrustedVMUtils::MitigationStatus
UntrustedVMUtils::CheckUntrustedVMMitigationStatus() {
  if (host_kernel_version_ < min_needed_version_)
    return MitigationStatus::VULNERABLE;

  MitigationStatus status = GetL1TFMitigationStatus(l1tf_status_path_);
  if (status != MitigationStatus::NOT_VULNERABLE)
    return status;

  return GetMDSMitigationStatus(mds_status_path_);
}

bool UntrustedVMUtils::DisableSMT() {
  dbus::MethodCall method_call(debugd::kDebugdInterface,
                               debugd::kSetSchedulerConfigurationV2);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(kSchedulerConfigurationConservative);
  writer.AppendBool(true /* lock_policy */);

  dbus::ScopedDBusError dbus_error;
  std::unique_ptr<dbus::Response> response =
      debugd_proxy_->CallMethodAndBlockWithErrorDetails(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT, &dbus_error);
  if (!response) {
    // Non x86_64 devices don't have SMT. Pretend this operation succeeded.
    if (dbus_error.is_set() &&
        !strcmp(dbus_error.message(), kInvalidArchitectureErrorMsg)) {
      return true;
    }
    return false;
  }

  bool result;
  dbus::MessageReader reader(response.get());
  if (!reader.PopBool(&result)) {
    LOG(ERROR) << "Failed to read SetAndLockConservativeSchedulerConfiguration "
                  "response ";
    return false;
  }
  return result;
}

void UntrustedVMUtils::SetKernelVersionForTesting(
    KernelVersionAndMajorRevision host_kernel_version) {
  host_kernel_version_ = host_kernel_version;
}

}  // namespace concierge
}  // namespace vm_tools
