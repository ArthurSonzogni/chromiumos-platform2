// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <linux/capability.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <cstddef>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace {
constexpr uid_t kChronosUid = 1000;

const base::FilePath kProcSelfMountinfoPath =
    base::FilePath("/proc/self/mountinfo");

bool HasCap(size_t cap_index) {
  return prctl(PR_CAPBSET_READ, cap_index) == 1;
}

bool VerifyNonRootIds() {
  // Check that real and effective user and group IDs match 'chronos'.
  uid_t effective_uid, real_uid;
  gid_t effective_gid, real_gid;

  // get(e){u,g}id() functions never fail.
  effective_uid = geteuid();
  real_uid = getuid();
  effective_gid = getegid();
  real_gid = getgid();

  bool res = true;

  if (effective_uid != kChronosUid) {
    LOG(ERROR) << "Effective user ID is " << effective_uid << ", expected "
               << kChronosUid;
    res = false;
  }

  if (real_uid != kChronosUid) {
    LOG(ERROR) << "Real user ID is " << real_uid << ", expected "
               << kChronosUid;
    res = false;
  }

  if (effective_gid != kChronosUid) {
    LOG(ERROR) << "Effective group ID is " << effective_gid << ", expected "
               << kChronosUid;
    res = false;
  }

  if (real_gid != kChronosUid) {
    LOG(ERROR) << "Real group ID is " << real_gid << ", expected "
               << kChronosUid;
    res = false;
  }

  return res;
}

bool VerifyCapNetBindServiceOnly() {
  // Check that the process has dropped all capabilities except for
  // CAP_NET_BIND_SERVICE.
  bool res = true;
  for (size_t cap_idx = 0; cap_idx <= CAP_LAST_CAP; cap_idx++) {
    if (HasCap(cap_idx) && cap_idx != CAP_NET_BIND_SERVICE) {
      LOG(ERROR) << "Process has capability " << cap_idx
                 << " in the bounding set, expected only CAP_NET_BIND_SERVICE";
      res = false;
    }
  }

  return res;
}

std::vector<std::string> ReadMounts() {
  std::string mountinfo;
  if (!base::ReadFileToStringNonBlocking(kProcSelfMountinfoPath, &mountinfo)) {
    return std::vector<std::string>();
  }

  return base::SplitString(mountinfo, "\n", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

bool VerifyNonInitMountNamespace() {
  // It's not really possible for a process to check whether it is inside a
  // non-init mount namespace, since the point of namespaces is for their
  // existence to be transparent to userspace.
  //
  // Work around this by checking whether the root mount has parent ID 1. See
  // https://www.kernel.org/doc/Documentation/filesystems/proc.txt section 3.5
  // for more details.
  auto mounts = ReadMounts();
  for (const auto& mount : mounts) {
    // These entries are of the form:
    // 36 35 98:0 /mnt1 /mnt2 rw,noatime master:1 - ext3 /dev/root rw,noatime
    // (1)(2)(3)   (4)   (5)      (6)      (7)   (8) (9)   (10)         (11)

    // (1) mount ID:  unique identifier of the mount, may be reused after umount
    // (2) parent ID:  ID of parent (or of self for the top of the mount tree)
    std::vector<std::string_view> fields = base::SplitStringPiece(
        mount, base::kWhitespaceASCII, base::KEEP_WHITESPACE,
        base::SPLIT_WANT_NONEMPTY);

    // If the root mount has parent ID 1, the process is running in the init
    // mount namespace.
    if (fields[3] == "/" && fields[4] == "/") {
      int parent_id = std::stoi(std::string(fields[1]));
      if (parent_id == 1) {
        LOG(ERROR) << "Root mount parent ID is " << parent_id << ", expected 1";
      }
      return parent_id != 1;
    }
  }

  // If the root mount is not found, return false.
  return false;
}

bool VerifyNonInitPidNamespace() {
  // It's not really possible for a process to check whether it is inside a
  // non-init PID namespace, since the point of namespaces is for their
  // existence to be transparent to userspace.
  //
  // However, it's extremely unlikely for a userspace process to get PID 2 in
  // the init PID namespace, since this is normally the PID that kthreadd gets.
  //
  // PID namespaces set up by Minijail will give the sandboxed process PID 2
  // because PID 1 inside the namespace will be taken by the namespace init
  // process provided by Minijail.
  //
  // It's not true that any random process inside a non-init PID namespace will
  // have PID 2, since the size of the process tree inside the namespace is only
  // limited by system resources. However, for the purposes of a sandboxing
  // codelab, checking the Minijail case should be sufficient.
  pid_t pid = getpid();

  if (pid != 2) {
    LOG(ERROR) << "PID is " << pid << ", expected 2";
    return false;
  }

  return true;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(VerifyNonRootIds()) << "User/group IDs are not properly set up";

  CHECK(VerifyCapNetBindServiceOnly())
      << "Capabilities are not properly set up";

  CHECK(VerifyNonInitMountNamespace()) << "Running in the init mount namespace";

  CHECK(VerifyNonInitPidNamespace()) << "Running in the init PID namespace";

  LOG(INFO) << "Successfully sandboxed!";

  return 0;
}
