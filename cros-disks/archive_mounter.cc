// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_mounter.h"

#include <utility>

#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <brillo/scoped_mount_namespace.h>

#include "cros-disks/quote.h"

namespace cros_disks {

namespace {
constexpr char kOptionPassword[] = "password";

bool IsFormatRaw(const std::string& archive_type) {
  return (archive_type == "bz2") || (archive_type == "gz") ||
         (archive_type == "xz");
}

void RecordArchiveTypeMetrics(Metrics* const metrics,
                              const std::string& archive_type,
                              bool format_raw,
                              const std::string& source) {
  if (format_raw) {
    // Discriminate between kArchiveOtherGzip and kArchiveTarGzip, and ditto
    // for the Bzip2 and Xz flavors.
    std::string ext = base::FilePath(source).Extension();
    if (base::LowerCaseEqualsASCII(ext, ".tar.bz2")) {
      metrics->RecordArchiveType("tar.bz2");
      return;
    } else if (base::LowerCaseEqualsASCII(ext, ".tar.gz")) {
      metrics->RecordArchiveType("tar.gz");
      return;
    } else if (base::LowerCaseEqualsASCII(ext, ".tar.xz")) {
      metrics->RecordArchiveType("tar.xz");
      return;
    }
  }
  metrics->RecordArchiveType(archive_type);
}
}  // namespace

ArchiveMounter::ArchiveMounter(
    const Platform* platform,
    brillo::ProcessReaper* process_reaper,
    std::string archive_type,
    Metrics* metrics,
    std::string metrics_name,
    std::vector<int> password_needed_exit_codes,
    std::unique_ptr<SandboxedProcessFactory> sandbox_factory,
    std::vector<std::string> extra_command_line_options)
    : FUSEMounter(
          platform, process_reaper, archive_type + "fs", {.read_only = true}),
      archive_type_(archive_type),
      extension_("." + archive_type),
      metrics_(metrics),
      metrics_name_(std::move(metrics_name)),
      password_needed_exit_codes_(std::move(password_needed_exit_codes)),
      sandbox_factory_(std::move(sandbox_factory)),
      extra_command_line_options_(std::move(extra_command_line_options)),
      format_raw_(IsFormatRaw(archive_type)) {}

ArchiveMounter::~ArchiveMounter() = default;

bool ArchiveMounter::CanMount(const std::string& source,
                              const std::vector<std::string>& /*params*/,
                              base::FilePath* suggested_dir_name) const {
  base::FilePath path(source);
  if (path.IsAbsolute() && base::CompareCaseInsensitiveASCII(
                               path.FinalExtension(), extension_) == 0) {
    *suggested_dir_name = path.BaseName();
    return true;
  }
  return false;
}

MountErrorType ArchiveMounter::InterpretReturnCode(int return_code) const {
  if (metrics_ && !metrics_name_.empty())
    metrics_->RecordFuseMounterErrorCode(metrics_name_, return_code);

  if (base::Contains(password_needed_exit_codes_, return_code))
    return MOUNT_ERROR_NEED_PASSWORD;
  return FUSEMounter::InterpretReturnCode(return_code);
}

std::unique_ptr<SandboxedProcess> ArchiveMounter::PrepareSandbox(
    const std::string& source,
    const base::FilePath& /*target_path*/,
    std::vector<std::string> params,
    MountErrorType* error) const {
  RecordArchiveTypeMetrics(metrics_, archive_type_, format_raw_, source);

  base::FilePath path(source);
  if (!path.IsAbsolute() || path.ReferencesParent()) {
    LOG(ERROR) << "Invalid archive path " << redact(path);
    *error = MOUNT_ERROR_INVALID_ARGUMENT;
    return nullptr;
  }

  auto sandbox = sandbox_factory_->CreateSandboxedProcess();

  std::unique_ptr<brillo::ScopedMountNamespace> mount_ns;
  if (!platform()->PathExists(path.value())) {
    // Try to locate the file in Chrome's mount namespace.
    mount_ns = brillo::ScopedMountNamespace::CreateFromPath(
        base::FilePath(kChromeNamespace));
    if (!mount_ns) {
      PLOG(ERROR) << "Cannot find archive " << redact(path)
                  << " in mount namespace " << quote(kChromeNamespace);

      // TODO(dats): These probably should be MOUNT_ERROR_INVALID_DEVICE_PATH or
      //             something like that, but tast tests expect
      //             MOUNT_ERROR_MOUNT_PROGRAM_FAILED.
      *error = MOUNT_ERROR_MOUNT_PROGRAM_FAILED;
      return nullptr;
    }
    if (!platform()->PathExists(path.value())) {
      PLOG(ERROR) << "Cannot find archive " << redact(path);
      *error = MOUNT_ERROR_MOUNT_PROGRAM_FAILED;
      return nullptr;
    }
  }

  // Archives are typically under /home, /media or /run. To bind-mount the
  // source those directories must be writable, but by default only /run is.
  for (const char* const dir : {"/home", "/media"}) {
    if (!sandbox->Mount("tmpfs", dir, "tmpfs", "mode=0755,size=1M")) {
      LOG(ERROR) << "Cannot mount " << quote(dir);
      *error = MOUNT_ERROR_INTERNAL;
      return nullptr;
    }
  }

  // Is the process "password-aware"?
  if (!password_needed_exit_codes_.empty()) {
    std::string password;
    if (GetParamValue(params, kOptionPassword, &password)) {
      sandbox->SetStdIn(password);
    }
  }

  // Bind-mount parts of a multipart archive if any.
  for (const std::string& part : GetBindPaths(path.value())) {
    if (!sandbox->BindMount(part, part, /* writeable= */ false,
                            /* recursive= */ false)) {
      PLOG(ERROR) << "Cannot bind-mount archive " << redact(part);
      *error = MOUNT_ERROR_INTERNAL;
      return nullptr;
    }
  }

  // Prepare command line arguments.
  sandbox->AddArgument("-o");
  sandbox->AddArgument(base::StringPrintf("ro,umask=0222,uid=%d,gid=%d",
                                          kChronosUID, kChronosAccessGID));

  if (std::string encoding; GetParamValue(params, "encoding", &encoding)) {
    sandbox->AddArgument("-o");
    sandbox->AddArgument("encoding=" + encoding);
  }

  for (const auto& opt : extra_command_line_options_)
    sandbox->AddArgument(opt);

  sandbox->AddArgument(path.value());

  if (mount_ns) {
    // Sandbox will need to enter Chrome's namespace too to access files.
    mount_ns.reset();
    sandbox->EnterExistingMountNamespace(kChromeNamespace);
  }

  *error = MOUNT_ERROR_NONE;
  return sandbox;
}

}  // namespace cros_disks
