// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/user_collector.h"

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>

#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <bits/wordsize.h>
#include <brillo/process/process.h>
#include <chromeos/constants/crash_reporter.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/constants.h"
#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/crash_collector_names.h"
#include "crash-reporter/crash_sending_mode.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/user_collector_base.h"
#include "crash-reporter/util.h"
#include "crash-reporter/vm_support.h"

using base::FilePath;
using base::StringPrintf;

namespace {

// The length of a process name stored in the kernel. Appears on our command
// line and also in /proc/[pid] places. See PR_SET_NAME in
// http://www.kernel.org/doc/man-pages/online/pages/man2/prctl.2.html and also
// TASK_COMM_LEN. Both of those say "16 bytes" which includes the terminating
// nul byte; the strlen is 15 characters.
constexpr int kKernelProcessNameLength = 15;

// This procfs file is used to cause kernel core file writing to
// instead pipe the core file into a user space process.  See
// core(5) man page.
const char kCorePatternFile[] = "/proc/sys/kernel/core_pattern";
const char kCorePipeLimitFile[] = "/proc/sys/kernel/core_pipe_limit";
// Set core_pipe_limit to 4 so that we can catch a few unrelated concurrent
// crashes, but finite to avoid infinitely recursing on crash handling.
const char kCorePipeLimit[] = "4";
const char kCoreToMinidumpConverterPath[] = "/usr/bin/core2md";

const char kFilterPath[] = "/opt/google/crash-reporter/filter";

// Core pattern lock file: only exists on linux-3.18 and earlier.
const char kCorePatternLockFile[] = "/proc/sys/kernel/lock_core_pattern";

// Filename we touch in our state directory when we get enabled.
constexpr char kCrashHandlingEnabledFlagFile[] = "crash-handling-enabled";

// The name of the main chrome executable. Currently, both lacros and ash use
// the same executable name.
constexpr char kChromeExecName[] = "chrome";

// The value of ptype for Chrome's browser process.  Must match the value of
// |ptype_key| if browser_process is true inside InitializeCrashpadImpl() in
// https://source.chromium.org/chromium/chromium/src/+/main:components/crash/core/app/crashpad.cc
constexpr char kChromeProcessTypeBrowserValue[] = "browser";

// The name of the session_manager executable used to compute crash severity.
constexpr char kSessionManagerExecName[] = "session_manager";

// Returns true if the given executable name matches that of Chrome.  This
// includes checks for threads that Chrome has renamed.
bool IsChromeExecName(const std::string& exec);

// This is needed for kernels older than linux-4.4. Once we drop support for
// older kernels (upgrading or going EOL), we can drop this logic.
bool LockCorePattern() {
  base::FilePath core_pattern_lock_file(kCorePatternLockFile);

  // Core pattern lock was only added for kernel versions before 4.4.
  if (!base::PathExists(core_pattern_lock_file)) {
    VLOG(1) << "No core pattern lock available";
    return true;
  }

  if (util::IsDeveloperImage()) {
    LOG(INFO) << "Developer image -- leaving core pattern unlocked";
    return true;
  }

  if (!base::WriteFile(core_pattern_lock_file, "1")) {
    PLOG(ERROR) << "Failed to lock core pattern";
    return false;
  }

  return true;
}

// Given an exec name like "chrome", return the string we'd get in |exec| if
// we're getting the exec name from the kernel. Matches the string manipulation
// in UserCollectorBase::HandleCrash.
std::string ExecNameToSuppliedName(std::string_view name) {
  // When checking a kernel-supplied name, it should be truncated to 15 chars.
  return "supplied_" + std::string(name.data(), 0, kKernelProcessNameLength);
}
}  // namespace

UserCollector::UserCollector(
    const scoped_refptr<
        base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
        metrics_lib)
    : UserCollectorBase(CrashReporterCollector::kUser,
                        kUseNormalCrashDirectorySelectionMethod,
                        metrics_lib),
      core_pattern_file_(kCorePatternFile),
      core_pipe_limit_file_(kCorePipeLimitFile),
      filter_path_(kFilterPath),
      handling_early_chrome_crash_(false),
      core2md_failure_(false) {}

void UserCollector::Initialize(const std::string& our_path,
                               bool core2md_failure,
                               bool directory_failure,
                               bool early) {
  UserCollectorBase::Initialize(directory_failure, early);
  our_path_ = our_path;
  core2md_failure_ = core2md_failure;
}

UserCollector::~UserCollector() {}

CrashCollectionStatus UserCollector::FinishCrash(
    const base::FilePath& meta_path,
    const std::string& exec_name,
    const std::string& payload_name) {
  VmSupport* vm_support = VmSupport::Get();
  if (vm_support) {
    vm_support->AddMetadata(this);
  }

  CrashCollectionStatus status =
      UserCollectorBase::FinishCrash(meta_path, exec_name, payload_name);

  if (vm_support) {
    vm_support->FinishCrash(meta_path);
  }

  return status;
}

CrashCollector::ComputedCrashSeverity UserCollector::ComputeSeverity(
    const std::string& exec_name) {
  if (exec_name == kSessionManagerExecName) {
    return ComputedCrashSeverity{
        .crash_severity = CrashSeverity::kFatal,
        .product_group = Product::kPlatform,
    };
  }

  // When `handling_early_chrome_crash_` is true, the crash can either be
  // an ash or lacros chrome crash which is decided by the value
  // of `early_crash_chrome_product_key_`. The appropriate product group value
  // is associated with the returned ComputedCrashSeverity.
  if (handling_early_chrome_crash_) {
    if (early_crash_chrome_product_key_ ==
        constants::kProductNameChromeLacros) {
      return ComputedCrashSeverity{
          .crash_severity = CrashSeverity::kFatal,
          .product_group = Product::kLacros,
      };
    }
    return ComputedCrashSeverity{
        .crash_severity = CrashSeverity::kFatal,
        .product_group = Product::kUi,
    };
  }

  return ComputedCrashSeverity{
      .crash_severity = CrashSeverity::kError,
      .product_group = Product::kPlatform,
  };
}

// Return the string that should be used for the kernel's core_pattern file.
// Note that if you change the format of the enabled pattern, you'll probably
// also need to change the UserCollectorBase::ParseCrashAttributes function, the
// user_collector_test.cc unittest, the logging_UserCrash.py autotest,
// and the platform.UserCrash tast test.
std::string UserCollector::GetPattern(bool enabled, bool early) const {
  if (enabled) {
    // Combine the crash attributes into one parameter to try to reduce
    // the size of the invocation line for crash_reporter, since the kernel
    // has a fixed-sized (128B) buffer for it (before parameter expansion).
    // Note that the kernel does not support quoted arguments in core_pattern.
    return StringPrintf("|%s %s--user=%%P:%%s:%%u:%%g:%%f", our_path_.c_str(),
                        early ? "--early --log_to_stderr " : "");
  } else {
    return "core";
  }
}

bool UserCollector::SetUpInternal(bool enabled, bool early) {
  CHECK(initialized_);
  LOG(INFO) << (enabled ? "Enabling" : "Disabling") << " user crash handling";

  if (!base::WriteFile(FilePath(core_pipe_limit_file_), kCorePipeLimit)) {
    PLOG(ERROR) << "Unable to write " << core_pipe_limit_file_;
    return false;
  }
  std::string pattern = GetPattern(enabled, early);
  if (!base::WriteFile(FilePath(core_pattern_file_), pattern)) {
    int saved_errno = errno;
    // If the core pattern is locked and we try to reset the |core_pattern|
    // while disabling |user_collector| or resetting it to what it already was,
    // expect failure here with an EPERM.
    bool ignore_error = false;
    if (errno == EPERM && base::PathExists(FilePath(kCorePatternLockFile))) {
      std::string actual_contents;
      if (!base::ReadFileToString(FilePath(core_pattern_file_),
                                  &actual_contents)) {
        PLOG(ERROR) << "Failed to read " << core_pattern_file_;
        actual_contents.clear();
      }
      if (!enabled || base::TrimWhitespaceASCII(
                          actual_contents, base::TRIM_TRAILING) == pattern) {
        ignore_error = true;
        LOG(WARNING) << "Failed to write to locked core pattern; ignoring";
      }
    }
    if (!ignore_error) {
      LOG(ERROR) << "Unable to write " << core_pattern_file_ << ": "
                 << strerror(saved_errno);
      return false;
    }
  }

  // Attempt to lock down |core_pattern|: this only works for kernels older than
  // linux-3.18.
  if (enabled && !early && !LockCorePattern()) {
    LOG(ERROR) << "Failed to lock core pattern on a supported device";
    return false;
  }

  // Set up the base crash processing dir for future users.
  const FilePath dir = GetCrashProcessingDir();

  // First nuke all existing content.  This will take care of deleting any
  // existing paths (files, symlinks, dirs, etc...) for us.
  if (!base::DeletePathRecursively(dir)) {
    PLOG(WARNING) << "Cleanup of directory failed: " << dir.value();
  }

  // This will create the directory with 0700 mode.  Since init is run as root,
  // root will own these too.
  if (!base::CreateDirectory(dir)) {
    PLOG(ERROR) << "Creating directory failed: " << dir.value();
    return false;
  }

  // Write out a flag file for testing to indicate we have started correctly.
  char data[] = "enabled";
  if (!base::WriteFile(base::FilePath(crash_reporter_state_path_)
                           .Append(kCrashHandlingEnabledFlagFile),
                       data)) {
    PLOG(WARNING) << "Unable to create flag file for crash reporter enabled";
  }

  return true;
}

bool UserCollector::CopyOffProcFiles(pid_t pid, const FilePath& container_dir) {
  FilePath process_path = GetProcessPath(pid);
  if (!base::PathExists(process_path)) {
    LOG(ERROR) << "Path " << process_path.value() << " does not exist";
    return false;
  }

  // NB: We can't (yet) use brillo::SafeFD here because it does not support
  // reading /proc files (it sometimes truncates them).
  // TODO(b/216739198): Use SafeFD.
  int processpath_fd;
  if (!ValidatePathAndOpen(process_path, &processpath_fd)) {
    LOG(ERROR) << "Failed to open process path dir: " << process_path.value();
    return false;
  }
  base::ScopedFD scoped_processpath_fd(processpath_fd);

  int containerpath_fd;
  if (!ValidatePathAndOpen(container_dir, &containerpath_fd)) {
    LOG(ERROR) << "Failed to open container dir:" << container_dir.value();
    return false;
  }
  base::ScopedFD scoped_containerpath_fd(containerpath_fd);

  static const char* const kProcFiles[] = {"auxv", "cmdline", "environ",
                                           "maps", "status",  "syscall"};
  for (const auto& proc_file : kProcFiles) {
    int source_fd = HANDLE_EINTR(
        openat(processpath_fd, proc_file, O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (source_fd < 0) {
      PLOG(ERROR) << "Failed to open " << process_path << "/" << proc_file;
      return false;
    }
    base::File source(source_fd);

    int dest_fd = HANDLE_EINTR(
        openat(containerpath_fd, proc_file,
               O_CREAT | O_WRONLY | O_TRUNC | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
               constants::kSystemCrashFilesMode));
    if (dest_fd < 0) {
      PLOG(ERROR) << "Failed to open " << container_dir << "/" << proc_file;
      return false;
    }
    base::File dest(dest_fd);

    if (!base::CopyFileContents(source, dest)) {
      LOG(ERROR) << "Failed to copy " << proc_file;
      return false;
    }
  }
  return true;
}

bool UserCollector::ValidateProcFiles(const FilePath& container_dir) const {
  // Check if the maps file is empty, which could be due to the crashed
  // process being reaped by the kernel before finishing a core dump.
  int64_t file_size = 0;
  if (!base::GetFileSize(container_dir.Append("maps"), &file_size)) {
    PLOG(ERROR) << "Could not get the size of maps file";
    return false;
  }
  if (file_size == 0) {
    LOG(ERROR) << "maps file is empty";
    return false;
  }
  return true;
}

CrashCollectionStatus UserCollector::ValidateCoreFile(
    const FilePath& core_path) const {
  int fd = HANDLE_EINTR(open(core_path.value().c_str(), O_RDONLY));
  if (fd < 0) {
    PLOG(ERROR) << "Could not open core file " << core_path.value();
    return CrashCollectionStatus::kFailureOpeningCoreFile;
  }

  char e_ident[EI_NIDENT];
  bool read_ok = base::ReadFromFD(fd, e_ident);
  IGNORE_EINTR(close(fd));
  if (!read_ok) {
    LOG(ERROR) << "Could not read header of core file";
    return CrashCollectionStatus::kFailureReadingCoreFileHeader;
  }

  if (e_ident[EI_MAG0] != ELFMAG0 || e_ident[EI_MAG1] != ELFMAG1 ||
      e_ident[EI_MAG2] != ELFMAG2 || e_ident[EI_MAG3] != ELFMAG3) {
    LOG(ERROR) << "Invalid core file";
    return CrashCollectionStatus::kBadCoreFileMagic;
  }

#if __WORDSIZE == 64
  // TODO(benchan, mkrebs): Remove this check once core2md can
  // handles both 32-bit and 64-bit ELF on a 64-bit platform.
  if (e_ident[EI_CLASS] == ELFCLASS32) {
    LOG(ERROR) << "Conversion of 32-bit core file on 64-bit platform is "
               << "currently not supported";
    return CrashCollectionStatus::kFailureUnsupported32BitCoreFile;
  }
#endif

  return CrashCollectionStatus::kSuccess;
}

bool UserCollector::CopyStdinToCoreFile(const base::FilePath& core_path) {
  return CopyPipeToCoreFile(STDIN_FILENO, core_path);
}

bool UserCollector::CopyPipeToCoreFile(int input_fd,
                                       const base::FilePath& core_path) {
  // We need to write to an actual file here for core2md.
  // If we're in memfd mode, fail out.
  if (crash_sending_mode_ == CrashSendingMode::kCrashLoop) {
    LOG(ERROR) << "Cannot call CopyFdToNewFile in CrashSendingMode::kCrashLoop";
    return false;
  }

  if (handling_early_chrome_crash_) {
    int max_core_size = kMaxChromeCoreSize;
    if (util::UseLooseCoreSizeForChromeCrashEarly()) {
      max_core_size = kMaxChromeCoreSizeLoose;
    }

    // See comments for kMaxChromeCoreSize in the header for why we do this.
    std::optional<int> res =
        CopyFirstNBytesOfFdToNewFile(input_fd, core_path, max_core_size);
    if (!res) {
      LOG(ERROR) << "Could not write core file " << core_path.value();
      if (!base::DeleteFile(core_path)) {
        LOG(ERROR) << "And could not delete the core file either";
      }
      return false;
    }

    // Check that we wrote out the entire core file. Partial core files aren't
    // going to usable.
    if (res.value() < max_core_size) {
      return true;
    }

    // If res.value() == max_core_size, then we can only tell if we wrote
    // out all the input by trying to read one more byte and seeing if we were
    // at EOF.
    char n_plus_one_byte;
    if (read(input_fd, &n_plus_one_byte, 1) == 0) {
      // Core was exactly max_core_size.
      return true;
    }

    LOG(ERROR) << "Core file too big; write aborted";
    if (!base::DeleteFile(core_path)) {
      LOG(ERROR) << "And could not delete partial core file afterwards";
    }
    return false;
  }

  // We don't directly create a ScopedFD with input_fd because the
  // destructor would close() that file descriptor. In non-test-scenarios,
  // input_fd is stdin and we don't want to close stdin.
  base::ScopedFD input_fd_copy(dup(input_fd));
  if (!input_fd_copy.is_valid()) {
    return false;
  }
  if (CopyFdToNewFile(std::move(input_fd_copy), core_path)) {
    return true;
  }

  PLOG(ERROR) << "Could not write core file " << core_path.value();
  // If the file system was full, make sure we remove any remnants.
  if (!base::DeleteFile(core_path)) {
    LOG(ERROR) << "And could not delete the core file either";
  }
  return false;
}

bool UserCollector::RunCoreToMinidump(const FilePath& core_path,
                                      const FilePath& procfs_directory,
                                      const FilePath& minidump_path,
                                      const FilePath& temp_directory) {
  FilePath output_path = temp_directory.Append("output");
  brillo::ProcessImpl core2md;
  core2md.RedirectOutput(output_path);
  core2md.AddArg(kCoreToMinidumpConverterPath);
  core2md.AddArg(core_path.value());
  core2md.AddArg(procfs_directory.value());

  if (!core2md_failure_) {
    core2md.AddArg(minidump_path.value());
  } else {
    // To test how core2md errors are propagaged, cause an error
    // by forgetting a required argument.
  }

  int errorlevel = core2md.Run();

  std::string output;
  base::ReadFileToString(output_path, &output);
  if (errorlevel != 0) {
    LOG(ERROR) << "Problem during " << kCoreToMinidumpConverterPath
               << " [result=" << errorlevel << "]: " << output;
    return false;
  }

  // Change the minidump to be not-world-readable. chmod will change permissions
  // on symlinks. Use fchmod instead.
  base::ScopedFD minidump(
      open(minidump_path.value().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
  if (!minidump.is_valid()) {
    PLOG(ERROR) << "Could not open minidump file: " << minidump_path.value();
    return false;
  }
  if (fchmod(minidump.get(), constants::kSystemCrashFilesMode) < 0) {
    PLOG(ERROR) << "Couldn't chmod minidump file: " << minidump_path.value();
    return false;
  }
  return true;
}

bool UserCollector::RunFilter(pid_t pid) {
  int mode;
  int exec_mode = base::FILE_PERMISSION_EXECUTE_BY_USER |
                  base::FILE_PERMISSION_EXECUTE_BY_GROUP |
                  base::FILE_PERMISSION_EXECUTE_BY_OTHERS;
  if (!base::GetPosixFilePermissions(base::FilePath(filter_path_), &mode) ||
      (mode & exec_mode) != exec_mode) {
    // Filter does not exist or is not executable.
    return true;
  }

  brillo::ProcessImpl filter;
  filter.AddArg(filter_path_);
  filter.AddArg(StringPrintf("%d", pid));

  return filter.Run() == 0;
}

bool UserCollector::ShouldCaptureEarlyChromeCrash(const std::string& exec,
                                                  pid_t pid) {
  // Rules:
  //   1. Only the main browser process needs to be captured this way. Crashpad
  //      can capture very early crashes in subprocesses.
  //   2. Only capture if the process has not initialized crashpad. We rely
  //      on the browser process creating /run/crash_reporter/crashpad_ready/pid
  //      to let us know when crashpad is ready.
  //      Note: In guest mode, Chrome is running in a private mount and we
  //            can't see the write to /run/crash_reporter/crashpad_ready/.
  //            For end-users, we'll never get here because IsFeedbackAllowed()
  //            will return false, so we don't need a separate check for guest
  //            mode.
  if (exec != kChromeExecName &&
      exec != ExecNameToSuppliedName(kChromeExecName)) {
    return false;  // Doesn't meet rule #1.
  }

  base::FilePath crashpad_ready_path =
      paths::Get(crash_reporter::kCrashpadReadyDirectory)
          .Append(base::NumberToString(pid));
  if (base::PathExists(crashpad_ready_path)) {
    return false;  // Doesn't meet rule #2.
  }

  base::FilePath process_path = GetProcessPath(pid);
  base::FilePath cmdline_path(process_path.Append("cmdline"));
  std::string cmdline;
  if (!base::ReadFileToString(cmdline_path, &cmdline)) {
    LOG(WARNING) << "Could not read " << cmdline_path.value();
    return false;  // Can't tell if it meets rule #1.
  }

  // https://man7.org/linux/man-pages/man5/proc.5.html says the command line
  // arguments are separated by '\0's. But  When Chrome's processes spawn and
  // override their cmdlines, they can end up with spaces between the args
  // instead of the expected \0s. Check both ways.
  for (char separator :
       {kNormalCmdlineSeparator, kChromeSubprocessCmdlineSeparator}) {
    std::vector<std::string_view> argv = base::SplitStringPiece(
        cmdline, std::string(1, separator), base::TRIM_WHITESPACE,
        base::SPLIT_WANT_NONEMPTY);

    for (std::string_view arg : argv) {
      if (base::StartsWith(arg, "--type=")) {
        return false;  // Not the browser process. Doesn't meet rule #1.
      }
    }
  }

  return true;
}

// static
const char* UserCollector::GuessChromeProductName(
    const base::FilePath& exec_directory) {
  if (exec_directory.empty()) {
    // Guess Chrome_ChromeOS for lack of a better choice.
    LOG(WARNING) << "Exectuable directory not known; assuming ash";
    return constants::kProductNameChromeAsh;
  }

  const base::FilePath kAshChromeDirectory(paths::Get("/opt/google/chrome"));
  if (kAshChromeDirectory == exec_directory) {
    return constants::kProductNameChromeAsh;
  }

  // Lacros can be in several different directories. Sometimes it runs from
  // rootfs, sometimes from stateful. Just look for the "lacros" string.
  if (exec_directory.value().find("lacros") != std::string::npos) {
    return constants::kProductNameChromeLacros;
  }

  LOG(WARNING) << exec_directory.value()
               << " does not match Ash or Lacros paths";
  // Guess Chrome_ChromeOS for lack of a better choice.
  return constants::kProductNameChromeAsh;
}

void UserCollector::BeginHandlingCrash(pid_t pid,
                                       const std::string& exec,
                                       const base::FilePath& exec_directory) {
  // Check for early Chrome crashes; if this is an early Chrome crash, start
  // the special handling. Don't use the special handling if
  // ShouldHandleChromeCrashes() returns true, because that indicates we want to
  // use the normal handling code path for Chrome crashes.
  if (!ShouldHandleChromeCrashes() && IsChromeExecName(exec) &&
      ShouldCaptureEarlyChromeCrash(exec, pid)) {
    handling_early_chrome_crash_ = true;
    // Change product name to Chrome_ChromeOS or Chrome_Lacros.
    early_crash_chrome_product_key_ = GuessChromeProductName(exec_directory);
    AddCrashMetaUploadData(constants::kUploadDataKeyProductKey,
                           early_crash_chrome_product_key_);
    AddCrashMetaUploadData("early_chrome_crash", "true");

    // Add the "ptype=browser" normally added by InitializeCrashpadImpl(). Since
    // we reject any process with a "--type" flag, this should always be a
    // browser process.
    AddCrashMetaUploadData(constants::kChromeProcessTypeKey,
                           kChromeProcessTypeBrowserValue);
    // Get the Chrome version if we can, so that the crashes show up correctly
    // on the "crashes in the latest dev release" dashboards.
    base::FilePath chrome_metadata_path =
        exec_directory.Append("metadata.json");
    if (std::optional<std::string> version_maybe =
            util::ExtractChromeVersionFromMetadata(chrome_metadata_path);
        version_maybe) {
      AddCrashMetaUploadData("ver", *version_maybe);
    }

    // TODO(b/234500620): We should also check for crash-loop mode and activate
    // it here if appropriate. Otherwise we risk losing crashes if there's an
    // early crash loading a user's profile info.

    LOG(INFO) << "Activating early Chrome crash mode for "
              << early_crash_chrome_product_key_;
  }
}

base::expected<void, CrashCollectionStatus> UserCollector::ShouldDump(
    pid_t pid, bool handle_chrome_crashes, const std::string& exec) {
  // Treat Chrome crashes as if the user opted-out.  We stop counting Chrome
  // crashes towards user crashes, so user crashes really mean non-Chrome
  // user-space crashes.
  if (!handle_chrome_crashes && !handling_early_chrome_crash_ &&
      IsChromeExecName(exec)) {
    return base::unexpected(CrashCollectionStatus::kChromeCrashInUserCollector);
  }

  if (!RunFilter(pid)) {
    return base::unexpected(CrashCollectionStatus::kFilteredOut);
  }

  return UserCollectorBase::ShouldDump(pid);
}

base::expected<void, CrashCollectionStatus> UserCollector::ShouldDump(
    pid_t pid, uid_t, const std::string& exec) {
  return ShouldDump(pid, ShouldHandleChromeCrashes(), exec);
}

CrashCollectionStatus UserCollector::ConvertCoreToMinidump(
    pid_t pid,
    const FilePath& container_dir,
    const FilePath& core_path,
    const FilePath& minidump_path) {
  // If proc files are unusable, we continue to read the core file from stdin,
  // but only skip the core-to-minidump conversion, so that we may still use
  // the core file for debugging.
  bool proc_files_usable =
      CopyOffProcFiles(pid, container_dir) && ValidateProcFiles(container_dir);

  if (!CopyStdinToCoreFile(core_path)) {
    return CrashCollectionStatus::kFailureCopyingCoreData;
  }

  if (!proc_files_usable) {
    LOG(INFO) << "Skipped converting core file to minidump due to "
              << "unusable proc files";
    return CrashCollectionStatus::kUnusableProcFiles;
  }

  CrashCollectionStatus status = ValidateCoreFile(core_path);
  if (!IsSuccessCode(status)) {
    return status;
  }

  if (!RunCoreToMinidump(core_path,
                         container_dir,  // procfs directory
                         minidump_path,
                         container_dir)) {  // temporary directory
    return CrashCollectionStatus::kFailureCore2MinidumpConversion;
  }

  return CrashCollectionStatus::kSuccess;
}

namespace {

bool IsChromeExecName(const std::string& exec) {
  static const char* const kChromeNames[] = {
      kChromeExecName,
      // These are additional thread names seen in http://crash/
      "MediaPipeline",
      // These come from the use of base::PlatformThread::SetName() directly
      "CrBrowserMain", "CrRendererMain", "CrUtilityMain", "CrPPAPIMain",
      "CrPPAPIBrokerMain", "CrPluginMain", "CrWorkerMain", "CrGpuMain",
      "BrokerEvent", "CrVideoRenderer", "CrShutdownDetector", "UsbEventHandler",
      "CrNaClMain", "CrServiceMain",
      // These thread names come from the use of base::Thread
      "Gamepad polling thread", "Chrome_InProcGpuThread",
      "Chrome_DragDropThread", "Renderer::FILE", "VC manager",
      "VideoCaptureModuleImpl", "JavaBridge", "VideoCaptureManagerThread",
      "Geolocation", "Geolocation_wifi_provider",
      "Device orientation polling thread", "Chrome_InProcRendererThread",
      "NetworkChangeNotifier", "Watchdog", "inotify_reader",
      "cf_iexplore_background_thread", "BrowserWatchdog",
      "Chrome_HistoryThread", "Chrome_SyncThread", "Chrome_ShellDialogThread",
      "Printing_Worker", "Chrome_SafeBrowsingThread", "SimpleDBThread",
      "D-Bus thread", "AudioThread", "NullAudioThread", "V4L2Thread",
      "ChromotingClientDecodeThread", "Profiling_Flush", "worker_thread_ticker",
      "AudioMixerAlsa", "AudioMixerCras", "FakeAudioRecordingThread",
      "CaptureThread", "Chrome_WebSocketproxyThread", "ProcessWatcherThread",
      "Chrome_CameraThread", "import_thread", "NaCl_IOThread",
      "Chrome_CloudPrintJobPrintThread", "Chrome_CloudPrintProxyCoreThread",
      "DaemonControllerFileIO", "ChromotingMainThread",
      "ChromotingEncodeThread", "ChromotingDesktopThread", "ChromotingIOThread",
      "ChromotingFileIOThread", "Chrome_libJingle_WorkerThread",
      "Chrome_ChildIOThread", "GLHelperThread", "RemotingHostPlugin",
      // "PAC thread #%d",  // not easy to check because of "%d"
      "Chrome_DBThread", "Chrome_WebKitThread", "Chrome_FileThread",
      "Chrome_FileUserBlockingThread", "Chrome_ProcessLauncherThread",
      "Chrome_CacheThread", "Chrome_IOThread", "Cache Thread", "File Thread",
      "ServiceProcess_IO", "ServiceProcess_File", "extension_crash_uploader",
      "gpu-process_crash_uploader", "plugin_crash_uploader",
      "renderer_crash_uploader",
      // These come from the use of webkit_glue::WebThreadImpl
      "Compositor", "Browser Compositor",
      // "WorkerPool/%d",  // not easy to check because of "%d"
      // These come from the use of base::Watchdog
      "Startup watchdog thread Watchdog", "Shutdown watchdog thread Watchdog",
      // These come from the use of AudioDeviceThread::Start
      "AudioDevice", "AudioInputDevice", "AudioOutputDevice",
      // These come from the use of MessageLoopFactory::GetMessageLoop
      "GpuVideoDecoder", "RtcVideoDecoderThread", "PipelineThread",
      "AudioDecoderThread", "VideoDecoderThread",
      // These come from the use of MessageLoopFactory::GetMessageLoopProxy
      "CaptureVideoDecoderThread", "CaptureVideoDecoder",
      // These come from the use of base::SimpleThread
      "LocalInputMonitor/%d",  // "%d" gets lopped off for kernel-supplied
      // These come from the use of base::DelegateSimpleThread
      "ipc_channel_nacl reader thread/%d", "plugin_audio_input_thread/%d",
      "plugin_audio_thread/%d",
      // These come from the use of base::SequencedWorkerPool
      "BrowserBlockingWorker%d/%d",  // "%d" gets lopped off for kernel-supplied
  };
  static std::unordered_set<std::string> chrome_names;

  // Initialize a set of chrome names, for efficient lookup
  if (chrome_names.empty()) {
    for (std::string check_name : kChromeNames) {
      chrome_names.insert(check_name);
      chrome_names.insert(ExecNameToSuppliedName(check_name));
    }
  }

  return base::Contains(chrome_names, exec);
}

}  // namespace
