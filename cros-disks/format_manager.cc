// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/format_manager.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>
#include <chromeos/libminijail.h>

#include "cros-disks/filesystem_label.h"
#include "cros-disks/quote.h"
#include "cros-disks/sandboxed_process.h"

namespace cros_disks {
namespace {

struct FormatOptions {
  std::string label;
};

// Expected locations of an external format program
const char* const kFormatProgramPaths[] = {
    "/usr/sbin/mkfs.",
    "/bin/mkfs.",
    "/sbin/mkfs.",
    "/usr/bin/mkfs.",
};

// Supported file systems
const char* const kSupportedFilesystems[] = {
    "vfat",
    "exfat",
    "ntfs",
};

const char kDefaultLabel[] = "UNTITLED";

FormatError LabelErrorToFormatError(LabelError error_code) {
  switch (error_code) {
    case LabelError::kSuccess:
      return FormatError::kSuccess;
    case LabelError::kUnsupportedFilesystem:
      return FormatError::kUnsupportedFilesystem;
    case LabelError::kLongName:
      return FormatError::kLongName;
    case LabelError::kInvalidCharacter:
      return FormatError::kInvalidCharacter;
  }
}

// Turns a flat vector of key value pairs into a format options struct. Returns
// true if a valid options struct could be extracted from the vector.
bool ExtractFormatOptions(const std::vector<std::string>& options,
                          FormatOptions* format_options) {
  if (options.size() % 2 == 1) {
    LOG(WARNING) << "Number of options passed in (" << options.size()
                 << ") is not an even number";
    return false;
  }

  for (int i = 0; i < options.size(); i += 2) {
    if (options[i] == kFormatLabelOption) {
      format_options->label = options[i + 1];
    } else {
      LOG(WARNING) << "Unknown format option " << quote(options[i]);
      return false;
    }
  }

  if (format_options->label.empty()) {
    format_options->label = kDefaultLabel;
  }
  return true;
}

std::vector<std::string> CreateFormatArguments(const std::string& filesystem,
                                               const FormatOptions& options) {
  std::vector<std::string> arguments;
  if (filesystem == "vfat") {
    // Allow to create filesystem across the entire device.
    arguments.push_back("-I");
    // FAT type should be predefined, because mkfs autodetection is faulty.
    arguments.push_back("-F");
    arguments.push_back("32");
    arguments.push_back("-n");
    arguments.push_back(options.label);
  } else if (filesystem == "exfat") {
    arguments.push_back("-n");
    arguments.push_back(options.label);
  } else if (filesystem == "ntfs") {
    // --force is used to allow creating a filesystem on devices without a
    // partition table.
    arguments.push_back("--force");
    arguments.push_back("--quick");
    arguments.push_back("--label");
    arguments.push_back(options.label);
  }
  return arguments;
}

// Initialises the process for formatting and starts it.
FormatError StartFormatProcess(const std::string& device_file,
                               const std::string& format_program,
                               const std::vector<std::string>& arguments,
                               const Platform* platform_,
                               SandboxedProcess* process) {
  process->SetNoNewPrivileges();
  process->NewMountNamespace();
  process->NewIpcNamespace();
  process->NewNetworkNamespace();
  process->SetCapabilities(0);

  if (!process->EnterPivotRoot()) {
    LOG(ERROR) << "Cannot enter pivot root";
    return FormatError::kFormatProgramFailed;
  }

  if (!process->SetUpMinimalMounts()) {
    LOG(ERROR) << "Cannot set up minimal mounts for jail";
    return FormatError::kFormatProgramFailed;
  }

  // Open device_file so we can pass only the fd path to the format program.
  base::File dev_file(base::FilePath(device_file), base::File::FLAG_OPEN |
                                                       base::File::FLAG_READ |
                                                       base::File::FLAG_WRITE);
  if (!dev_file.IsValid()) {
    PLOG(ERROR) << "Cannot open " << quote(device_file) << " for formatting: "
                << base::File::ErrorToString(dev_file.error_details());
    return FormatError::kFormatProgramFailed;
  }

  process->SetSeccompPolicy(
      base::FilePath("/usr/share/policy/mkfs-seccomp.policy"));

  uid_t user_id;
  gid_t group_id;
  const char kFormatUserAndGroupName[] = "mkfs";
  if (!platform_->GetUserAndGroupId(kFormatUserAndGroupName, &user_id,
                                    &group_id)) {
    LOG(ERROR) << "Cannot find user ID and group ID of "
               << quote(kFormatUserAndGroupName);
    return FormatError::kInternalError;
  }

  process->SetUserId(user_id);
  process->SetGroupId(group_id);

  process->AddArgument(format_program);

  for (const std::string& arg : arguments) {
    process->AddArgument(arg);
  }

  process->AddArgument(
      base::StringPrintf("/dev/fd/%d", dev_file.GetPlatformFile()));
  process->PreserveFile(dev_file.GetPlatformFile());

  // Sets an output callback, even if it does nothing, to activate the capture
  // of the generated messages.
  process->SetOutputCallback(base::DoNothing());

  if (!process->Start()) {
    LOG(ERROR) << "Cannot start " << quote(format_program) << " to format "
               << quote(device_file);
    return FormatError::kFormatProgramFailed;
  }

  LOG(INFO) << "Running " << quote(format_program) << " to format "
            << quote(device_file);
  return FormatError::kSuccess;
}

}  // namespace

FormatManager::FormatManager(Platform* const platform,
                             Reaper* const reaper,
                             Metrics* const metrics)
    : platform_(platform), reaper_(reaper), metrics_(metrics) {}

FormatError FormatManager::StartFormatting(
    const std::string& device_path,
    const std::string& device_file,
    const std::string& fs_type,
    const std::vector<std::string>& options) {
  // Check if the file system is supported for formatting
  if (!IsFilesystemSupported(fs_type)) {
    LOG(WARNING) << "Filesystem " << quote(fs_type)
                 << " is not supported for formatting";
    return FormatError::kUnsupportedFilesystem;
  }

  // Localize mkfs on disk
  std::string format_program = GetFormatProgramPath(fs_type);
  if (format_program.empty()) {
    LOG(WARNING) << "Cannot find a format program for filesystem "
                 << quote(fs_type);
    return FormatError::kFormatProgramNotFound;
  }

  FormatOptions format_options;
  if (!ExtractFormatOptions(options, &format_options)) {
    return FormatError::kInvalidOptions;
  }

  if (const LabelError error =
          ValidateVolumeLabel(format_options.label, fs_type);
      error != LabelError::kSuccess) {
    return LabelErrorToFormatError(error);
  }

  const auto [it, ok] = format_process_.try_emplace(device_path);
  SandboxedProcess& process = it->second;

  if (!ok) {
    LOG(WARNING) << "Device " << quote(device_path)
                 << " is already being formatted by "
                 << process.GetProgramName() << "[" << process.pid() << "]";
    return FormatError::kDeviceBeingFormatted;
  }

  base::ElapsedTimer timer;
  if (const FormatError error = StartFormatProcess(
          device_file, format_program,
          CreateFormatArguments(fs_type, format_options), platform_, &process);
      error != FormatError::kSuccess) {
    format_process_.erase(it);
    return error;
  }

  reaper_->WatchForChild(
      FROM_HERE, process.pid(),
      base::BindOnce(&FormatManager::OnDone, weak_ptr_factory_.GetWeakPtr(),
                     fs_type, device_path, std::move(timer)));
  return FormatError::kSuccess;
}

void FormatManager::OnDone(const std::string& fs_type,
                           const std::string& device_path,
                           const base::ElapsedTimer& timer,
                           const siginfo_t& info) {
  const auto node = format_process_.extract(device_path);
  if (!node) {
    LOG(ERROR) << "Cannot find process formatting " << quote(device_path);
    return;
  }

  DCHECK_EQ(node.key(), device_path);
  const SandboxedProcess& process = node.mapped();
  Process::ExitCode exit_code = Process::ExitCode::kNone;

  switch (info.si_code) {
    case CLD_EXITED:
      exit_code = Process::ExitCode(info.si_status);
      if (exit_code == Process::ExitCode::kSuccess) {
        LOG(INFO) << "Program " << quote(process.GetProgramName())
                  << " formatted " << fs_type << " " << quote(device_path)
                  << " successfully";
      } else {
        LOG(ERROR) << "Program " << quote(process.GetProgramName())
                   << " formatting " << fs_type << " " << quote(device_path)
                   << " finished with " << exit_code;
      }
      break;

    case CLD_DUMPED:
    case CLD_KILLED:
      exit_code = Process::ExitCode(MINIJAIL_ERR_SIG_BASE + info.si_status);
      LOG(ERROR) << "Program " << quote(process.GetProgramName())
                 << " formatting " << fs_type << " " << quote(device_path)
                 << " was killed by " << exit_code;
      break;

    default:
      LOG(ERROR) << "Unexpected si_code value " << info.si_code
                 << " for program " << quote(process.GetProgramName())
                 << " formatting " << fs_type << " " << quote(device_path);
      break;
  }

  // Log the captured output, if it hasn't been already logged as it was getting
  // captured.
  if (exit_code != Process::ExitCode::kSuccess && !LOG_IS_ON(INFO)) {
    for (const std::string& s : process.GetCapturedOutput()) {
      LOG(ERROR) << process.GetProgramName() << ": " << s;
    }
  }

  if (metrics_)
    metrics_->RecordAction("Format", fs_type, exit_code, timer.Elapsed());

  if (observer_)
    observer_->OnFormatCompleted(device_path,
                                 exit_code == Process::ExitCode::kSuccess
                                     ? FormatError::kSuccess
                                     : FormatError::kFormatProgramFailed);
}

std::string FormatManager::GetFormatProgramPath(
    const std::string& filesystem) const {
  for (const char* program_path : kFormatProgramPaths) {
    std::string path = program_path + filesystem;
    if (base::PathExists(base::FilePath(path)))
      return path;
  }
  return std::string();
}

bool FormatManager::IsFilesystemSupported(const std::string& filesystem) const {
  for (const char* supported_filesystem : kSupportedFilesystems) {
    if (filesystem == supported_filesystem)
      return true;
  }
  return false;
}

}  // namespace cros_disks
