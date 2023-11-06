// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <string_view>

#include <bpf/libbpf.h>
#include <sys/mount.h>
#include <sys/utsname.h>

#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/version.h>
#include <brillo/syslog_logging.h>

#include "patchpanel/bpf/constants.h"

namespace patchpanel {
namespace {

// The minimum kernel version for eBPF programs. The main reason that we choose
// 5.10 here is that CAP_BPF is only supported since 5.8.
constexpr char kBPFMinimumKernelVersion[] = "5.10";

// This path is created in this program.
constexpr char kBPFMountPath[] = "/run/patchpanel/bpf";

// Represents a BPF program to be load by LoadAndPinBPFProgram(). Use `const
// char*` instead of `std::string_view` since all the APIs used in that function
// takes c-style strings.
struct BPFProgramInfo {
  // Absolute path to the BPF object file.
  const char* object_path;
  // Absolute path to the BTF file for the BPF object.
  const char* btf_path;
  // The name of the program to load in the BPF object.
  const char* prog_name;
  // The program will be pinned to this path. The parent folder must be mounted
  // as BPF file system.
  const char* pin_path;
};

constexpr BPFProgramInfo kBPFWebRTCDetection = {
    // These two should match the install path in ebuild.
    .object_path = "/usr/share/patchpanel/webrtc_detection.o",
    .btf_path = "/usr/share/patchpanel/webrtc_detection.min.btf",

    // This should match the function name in the eBPF source code.
    .prog_name = "match_dtls_srtp",

    .pin_path = kWebRTCMatcherPinPath,
};

// Returns an invalid Version object on failure.
base::Version GetKernelVersion() {
  struct utsname buf;
  if (uname(&buf) != 0) {
    return {};
  }

  // The output may looks like "5.15.136-20820-g69a5713cd726". We only need the
  // first part.
  std::string_view version = base::SplitStringPiece(
      buf.release, "-", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)[0];
  return base::Version(version);
}

// This function equivalently performs the following actions:
// - `mkdir $path`;
// - `mount bpffs $path -t bpf -o nosuid,nodev,noexec`.
// Note that we use the existence of the mount path to check if this program has
// already been executed, so that path is created in this function instead of in
// tmpfiles.d.
bool MountPathForBPF(const base::FilePath& path) {
  if (!base::CreateDirectory(path)) {
    PLOG(ERROR) << "Failed to create " << path;
    return false;
  }

  int ret = mount("bpffs", path.value().c_str(), "bpf",
                  MS_NOSUID | MS_NODEV | MS_NOEXEC,
                  /*data=*/nullptr);
  if (ret != 0) {
    PLOG(ERROR) << "Failed to mount bpffs to " << kBPFMountPath;
    return false;
  }
  return true;
}

// This function equivalently performs the following action:
// `bpftool prog load program_info.obj_path $program_info.pin_path type socket`
// Note that we specify the |program_info.prog_name| in the object file while
// bpftool-prog will load the first program.
bool LoadAndPinBPFProgram(BPFProgramInfo program_info) {
  // Teach libbpf the path to our customized BTF file.
  DECLARE_LIBBPF_OPTS(bpf_object_open_opts, open_opts,
                      .btf_custom_path = program_info.btf_path);

  auto* obj = bpf_object__open_file(program_info.object_path, &open_opts);
  if (!obj) {
    // `bpf_object__open_file()` will set errno on failure.
    PLOG(ERROR) << "Failed to open bpf object file "
                << program_info.object_path;
    return false;
  }

  if (int ret = bpf_object__load(obj); ret != 0) {
    LOG(ERROR) << "Failed to load bpf object, ret=" << ret;
    return false;
  }

  auto* prog = bpf_object__find_program_by_name(obj, program_info.prog_name);
  if (!prog) {
    LOG(ERROR) << "Failed to find program " << program_info.prog_name
               << " in the bpf object";
    return false;
  }

  if (int ret = bpf_program__pin(prog, program_info.pin_path); ret != 0) {
    LOG(ERROR) << "Failed to pin program " << program_info.prog_name << " at "
               << program_info.pin_path;
    return false;
  }

  LOG(INFO) << "Pinned bpf program " << program_info.prog_name << " at "
            << program_info.pin_path;
  return true;
}

bool LoadBPF() {
  auto kernel_version = GetKernelVersion();
  if (!kernel_version.IsValid()) {
    LOG(ERROR) << "Failed to read kernel version";
    return false;
  }

  if (kernel_version < base::Version(kBPFMinimumKernelVersion)) {
    LOG(INFO) << "Skip since eBPF is not supported on this kernel";
    return true;
  }

  const base::FilePath mount_path(kBPFMountPath);
  if (base::PathExists(mount_path)) {
    LOG(INFO) << "Skip since eBPF mount path already created";
    return true;
  }

  if (!MountPathForBPF(mount_path)) {
    return false;
  }

  return LoadAndPinBPFProgram(kBPFWebRTCDetection);
}

}  // namespace
}  // namespace patchpanel

// Load the ebpf program for WebRTC detection. This program is supposed to:
// - Only run once after system booted. This is implemented by checking if the
//   bpf path (`kBPFMountPath`) has been created. In development, the developer
//   can umount and remove that folder explicitly to force a reload.
// - Only run on supported kernel versions.
int main() {
  brillo::InitLog(brillo::kLogToSyslog);
  return patchpanel::LoadBPF() ? 0 : 1;
}
