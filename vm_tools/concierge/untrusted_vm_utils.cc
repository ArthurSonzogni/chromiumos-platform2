// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/untrusted_vm_utils.h"

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "base/files/file_path.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools::concierge {

namespace {

// File path that reports the L1TF vulnerability status.
constexpr const char kL1TFFilePath[] =
    "/sys/devices/system/cpu/vulnerabilities/l1tf";

// File path that reports the MDS vulnerability status.
constexpr const char kMDSFilePath[] =
    "/sys/devices/system/cpu/vulnerabilities/mds";

// Returns the L1TF mitigation status of the host it's run on.
UntrustedVMUtils::MitigationStatus GetL1TFMitigationStatus(
    const base::FilePath& l1tf_status_path) {
  std::string l1tf_status;
  if (!base::ReadFileToString(l1tf_status_path, &l1tf_status)) {
    LOG(ERROR) << "Failed to read L1TF status";
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  }

  base::TrimString(l1tf_status, "\n", &l1tf_status);
  LOG(INFO) << "l1tf status: " << l1tf_status;

  std::vector<std::string_view> l1tf_statuses = base::SplitStringPiece(
      l1tf_status, ",;", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  const size_t num_statuses = l1tf_statuses.size();
  // The sysfs file should always return up to 3 statuses and no more.
  if (num_statuses > 3) {
    LOG(ERROR) << "Bad l1tf state";
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  }

  std::string_view processor_mitigation_status = l1tf_statuses[0];
  if (processor_mitigation_status == "Not affected")
    return UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE;
  if (processor_mitigation_status != "Mitigation: PTE Inversion")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;

  if (num_statuses >= 2) {
    std::string_view vmx_mitigation_status = l1tf_statuses[1];
    if ((vmx_mitigation_status != "VMX: cache flushes") &&
        (vmx_mitigation_status != "VMX: flush not necessary")) {
      return UntrustedVMUtils::MitigationStatus::VULNERABLE;
    }
  }

  // Only a maximum of 3 statuses are expected.
  if (num_statuses == 3) {
    std::string_view smt_mitigation_status = l1tf_statuses[2];
    if (smt_mitigation_status == "SMT vulnerable")
      return UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED;
    if (smt_mitigation_status != "SMT disabled")
      return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  }

  return UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE;
}

// Returns the MDS mitigation status of the host it's run on.
UntrustedVMUtils::MitigationStatus GetMDSMitigationStatus(
    const base::FilePath& mds_status_path) {
  std::string mds_status;
  if (!base::ReadFileToString(mds_status_path, &mds_status)) {
    LOG(ERROR) << "Failed to read MDS status";
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  }

  base::TrimString(mds_status, "\n", &mds_status);
  LOG(INFO) << "mds status: " << mds_status;

  std::vector<std::string_view> mds_statuses = base::SplitStringPiece(
      mds_status, ";", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  const size_t num_statuses = mds_statuses.size();
  // The sysfs file should always return up to 2 statuses and no more.
  if (num_statuses > 2) {
    LOG(ERROR) << "Bad mds state";
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  }

  std::string_view processor_mitigation_status = mds_statuses[0];
  if (processor_mitigation_status == "Not affected")
    return UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE;
  if (processor_mitigation_status.find("Vulnerable") != std::string_view::npos)
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  if (processor_mitigation_status != "Mitigation: Clear CPU buffers")
    return UntrustedVMUtils::MitigationStatus::VULNERABLE;

  // Only a maximum of 2 statuses are expected.
  if (num_statuses == 2) {
    std::string_view smt_mitigation_status = mds_statuses[1];
    if (smt_mitigation_status == "SMT vulnerable")
      return UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED;
    if (smt_mitigation_status == "SMT mitigated")
      return UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED;
    if (smt_mitigation_status == "SMT Host state unknown")
      return UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED;
    if (smt_mitigation_status != "SMT disabled")
      return UntrustedVMUtils::MitigationStatus::VULNERABLE;
  }

  return UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE;
}

}  // namespace

// static
UntrustedVMUtils::KernelVersionAndMajorRevision
UntrustedVMUtils::GetKernelVersion() {
  struct utsname buf;
  if (uname(&buf))
    return std::make_pair(INT_MIN, INT_MIN);

  // Parse uname result in the form of x.yy.zzz. The parsed data should be in
  // the expected format.
  std::vector<std::string_view> versions = base::SplitStringPiece(
      buf.release, ".", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);
  DCHECK_EQ(versions.size(), 3);
  DCHECK(!versions[0].empty());
  DCHECK(!versions[1].empty());
  int version;
  bool result = base::StringToInt(versions[0], &version);
  DCHECK(result);
  int major_revision;
  result = base::StringToInt(versions[1], &major_revision);
  DCHECK(result);
  return std::make_pair(version, major_revision);
}

UntrustedVMUtils::UntrustedVMUtils()
    : UntrustedVMUtils(base::FilePath(kL1TFFilePath),
                       base::FilePath(kMDSFilePath)) {}

UntrustedVMUtils::UntrustedVMUtils(base::FilePath l1tf_status_path,
                                   base::FilePath mds_status_path)
    : l1tf_status_path_(std::move(l1tf_status_path)),
      mds_status_path_(std::move(mds_status_path)) {
  DCHECK(!l1tf_status_path_.empty());
  DCHECK(!mds_status_path_.empty());
}

UntrustedVMUtils::MitigationStatus
UntrustedVMUtils::CheckUntrustedVMMitigationStatus() const {
  MitigationStatus status = GetL1TFMitigationStatus(l1tf_status_path_);
  if (status != MitigationStatus::NOT_VULNERABLE)
    return status;

  return GetMDSMitigationStatus(mds_status_path_);
}

bool UntrustedVMUtils::IsUntrustedVMAllowed(std::string* reason) const {
  DCHECK(reason);

  // Check if l1tf and mds mitigations are present on the host.
  switch (CheckUntrustedVMMitigationStatus()) {
    // If the host kernel version isn't supported or the host doesn't have
    // l1tf and mds mitigations then fail to start an untrusted VM.
    case UntrustedVMUtils::MitigationStatus::VULNERABLE:
      *reason = "Host vulnerable against untrusted VM";
      return false;

    // At this point SMT should not be a security issue, since Linux 4.19+ has
    // security patches to make nested VMs co-exist securely with SMT.
    case UntrustedVMUtils::MitigationStatus::VULNERABLE_DUE_TO_SMT_ENABLED:
    case UntrustedVMUtils::MitigationStatus::NOT_VULNERABLE:
      return true;
  }
}

}  // namespace vm_tools::concierge
