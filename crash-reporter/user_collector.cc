// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/user_collector.h"

#include <grp.h>  // For struct group.
#include <pcrecpp.h>
#include <pcrecpp.h>
#include <pwd.h>  // For struct passwd.
#include <sys/types.h>  // For getpwuid_r, getgrnam_r, WEXITSTATUS.

#include <string>
#include <vector>

#include "base/file_util.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "crash-reporter/system_logging.h"
#include "gflags/gflags.h"

#pragma GCC diagnostic ignored "-Wstrict-aliasing"
DEFINE_bool(core2md_failure, false, "Core2md failure test");
DEFINE_bool(directory_failure, false, "Spool directory failure test");
DEFINE_string(filter_in, "",
              "Ignore all crashes but this for testing");
#pragma GCC diagnostic error "-Wstrict-aliasing"

static const char kCollectionErrorSignature[] =
    "crash_reporter-user-collection";
// This procfs file is used to cause kernel core file writing to
// instead pipe the core file into a user space process.  See
// core(5) man page.
static const char kCorePatternFile[] = "/proc/sys/kernel/core_pattern";
static const char kCorePipeLimitFile[] = "/proc/sys/kernel/core_pipe_limit";
// Set core_pipe_limit to 4 so that we can catch a few unrelated concurrent
// crashes, but finite to avoid infinitely recursing on crash handling.
static const char kCorePipeLimit[] = "4";
static const char kCoreToMinidumpConverterPath[] = "/usr/bin/core2md";
static const char kLeaveCoreFile[] = "/root/.leave_core";

static const char kDefaultLogConfig[] = "/etc/crash_reporter_logs.conf";

const char *UserCollector::kUserId = "Uid:\t";
const char *UserCollector::kGroupId = "Gid:\t";

UserCollector::UserCollector()
    : generate_diagnostics_(false),
      core_pattern_file_(kCorePatternFile),
      core_pipe_limit_file_(kCorePipeLimitFile),
      initialized_(false) {
}

void UserCollector::Initialize(
    UserCollector::CountCrashFunction count_crash_function,
    const std::string &our_path,
    UserCollector::IsFeedbackAllowedFunction is_feedback_allowed_function,
    SystemLogging *logger,
    bool generate_diagnostics) {
  CrashCollector::Initialize(count_crash_function,
                             is_feedback_allowed_function,
                             logger);
  our_path_ = our_path;
  initialized_ = true;
  generate_diagnostics_ = generate_diagnostics;
}

UserCollector::~UserCollector() {
}

std::string UserCollector::GetPattern(bool enabled) const {
  if (enabled) {
    // Combine the three crash attributes into one parameter to try to reduce
    // the size of the invocation line for crash_reporter since the kernel
    // has a fixed-sized (128B) buffer that it will truncate into.  Note that
    // the kernel does not support quoted arguments in core_pattern.
    return StringPrintf("|%s --user=%%p:%%s:%%e", our_path_.c_str());
  } else {
    return "core";
  }
}

bool UserCollector::SetUpInternal(bool enabled) {
  CHECK(initialized_);
  logger_->LogInfo("%s user crash handling",
                   enabled ? "Enabling" : "Disabling");
  if (file_util::WriteFile(FilePath(core_pipe_limit_file_),
                           kCorePipeLimit,
                           strlen(kCorePipeLimit)) !=
      static_cast<int>(strlen(kCorePipeLimit))) {
    logger_->LogError("Unable to write %s", core_pipe_limit_file_.c_str());
    return false;
  }
  std::string pattern = GetPattern(enabled);
  if (file_util::WriteFile(FilePath(core_pattern_file_),
                           pattern.c_str(),
                           pattern.length()) !=
      static_cast<int>(pattern.length())) {
    logger_->LogError("Unable to write %s", core_pattern_file_.c_str());
    return false;
  }
  return true;
}

FilePath UserCollector::GetProcessPath(pid_t pid) {
  return FilePath(StringPrintf("/proc/%d", pid));
}

bool UserCollector::GetSymlinkTarget(const FilePath &symlink,
                                     FilePath *target) {
  int max_size = 32;
  scoped_array<char> buffer;
  while (true) {
    buffer.reset(new char[max_size + 1]);
    ssize_t size = readlink(symlink.value().c_str(), buffer.get(), max_size);
    if (size < 0) {
      int saved_errno = errno;
      logger_->LogError("Readlink failed on %s with %d",
                        symlink.value().c_str(), saved_errno);
      return false;
    }
    buffer[size] = 0;
    if (size == max_size) {
      // Avoid overflow when doubling.
      if (max_size * 2 > max_size) {
        max_size *= 2;
        continue;
      } else {
        return false;
      }
    }
    break;
  }

  *target = FilePath(buffer.get());
  return true;
}

bool UserCollector::GetExecutableBaseNameFromPid(uid_t pid,
                                                 std::string *base_name) {
  FilePath target;
  FilePath process_path = GetProcessPath(pid);
  FilePath exe_path = process_path.Append("exe");
  if (!GetSymlinkTarget(exe_path, &target)) {
    logger_->LogInfo("GetSymlinkTarget failed - Path %s DirectoryExists: %d",
                     process_path.value().c_str(),
                     file_util::DirectoryExists(process_path));
    // Try to further diagnose exe readlink failure cause.
    struct stat buf;
    int stat_result = stat(exe_path.value().c_str(), &buf);
    int saved_errno = errno;
    if (stat_result < 0) {
      logger_->LogInfo("stat %s failed: %d %d", exe_path.value().c_str(),
                       stat_result, saved_errno);
    } else {
      logger_->LogInfo("stat %s succeeded: st_mode=%d",
                       exe_path.value().c_str(), buf.st_mode);
    }
    return false;
  }
  *base_name = target.BaseName().value();
  return true;
}

bool UserCollector::GetIdFromStatus(const char *prefix,
                                    IdKind kind,
                                    const std::string &status_contents,
                                    int *id) {
  // From fs/proc/array.c:task_state(), this file contains:
  // \nUid:\t<uid>\t<euid>\t<suid>\t<fsuid>\n
  std::vector<std::string> status_lines;
  SplitString(status_contents, '\n', &status_lines);
  std::vector<std::string>::iterator line_iterator;
  for (line_iterator = status_lines.begin();
       line_iterator != status_lines.end();
       ++line_iterator) {
    if (line_iterator->find(prefix) == 0)
      break;
  }
  if (line_iterator == status_lines.end()) {
    return false;
  }
  std::string id_substring = line_iterator->substr(strlen(prefix),
                                                   std::string::npos);
  std::vector<std::string> ids;
  SplitString(id_substring, '\t', &ids);
  if (ids.size() != kIdMax || kind < 0 || kind >= kIdMax) {
    return false;
  }
  const char *number = ids[kind].c_str();
  char *end_number = NULL;
  *id = strtol(number, &end_number, 10);
  if (*end_number != '\0')
    return false;
  return true;
}

void UserCollector::EnqueueCollectionErrorLog(pid_t pid,
                                              const std::string &exec) {
  FilePath crash_path;
  logger_->LogInfo("Writing conversion problems as separate crash report.");
  if (!GetCreatedCrashDirectoryByEuid(0, &crash_path, NULL)) {
    logger_->LogError("Could not even get log directory; out of space?");
    return;
  }
  std::string dump_basename = FormatDumpBasename(exec, time(NULL), pid);
  std::string error_log = logger_->get_accumulator();
  FilePath diag_log_path = GetCrashPath(crash_path, dump_basename, "diaglog");
  if (GetLogContents(FilePath(kDefaultLogConfig), kCollectionErrorSignature,
                     diag_log_path)) {
    // We load the contents of diag_log into memory and append it to
    // the error log.  We cannot just append to files because we need
    // to always create new files to prevent attack.
    std::string diag_log_contents;
    file_util::ReadFileToString(diag_log_path, &diag_log_contents);
    error_log.append(diag_log_contents);
    file_util::Delete(diag_log_path, false);
  }
  FilePath log_path = GetCrashPath(crash_path, dump_basename, "log");
  FilePath meta_path = GetCrashPath(crash_path, dump_basename, "meta");
  // We must use WriteNewFile instead of file_util::WriteFile as we do
  // not want to write with root access to a symlink that an attacker
  // might have created.
  WriteNewFile(log_path, error_log.data(), error_log.length());
  AddCrashMetaData("sig", kCollectionErrorSignature);
  WriteCrashMetaData(meta_path, exec, log_path.value());
}

bool UserCollector::CopyOffProcFiles(pid_t pid,
                                     const FilePath &container_dir) {
  if (!file_util::CreateDirectory(container_dir)) {
    logger_->LogError("Could not create %s",
                      container_dir.value().c_str());
    return false;
  }
  FilePath process_path = GetProcessPath(pid);
  if (!file_util::PathExists(process_path)) {
    logger_->LogError("Path %s does not exist", process_path.value().c_str());
    return false;
  }
  static const char *proc_files[] = {
    "auxv",
    "cmdline",
    "environ",
    "maps",
    "status"
  };
  for (unsigned i = 0; i < arraysize(proc_files); ++i) {
    if (!file_util::CopyFile(process_path.Append(proc_files[i]),
                             container_dir.Append(proc_files[i]))) {
      logger_->LogError("Could not copy %s file", proc_files[i]);
      return false;
    }
  }
  return true;
}

bool UserCollector::GetCreatedCrashDirectory(pid_t pid,
                                             FilePath *crash_file_path,
                                             bool *out_of_capacity) {
  FilePath process_path = GetProcessPath(pid);
  std::string status;
  if (FLAGS_directory_failure) {
    logger_->LogError("Purposefully failing to create spool directory");
    return false;
  }
  if (!file_util::ReadFileToString(process_path.Append("status"),
                                   &status)) {
    logger_->LogError("Could not read status file");
    logger_->LogInfo("Path %s DirectoryExists: %d",
                     process_path.value().c_str(),
                     file_util::DirectoryExists(process_path));
    return false;
  }
  int process_euid;
  if (!GetIdFromStatus(kUserId, kIdEffective, status, &process_euid)) {
    logger_->LogError("Could not find euid in status file");
    return false;
  }
  if (!GetCreatedCrashDirectoryByEuid(process_euid,
                                      crash_file_path,
                                      out_of_capacity)) {
    logger_->LogError("Could not create crash directory");
    return false;
  }
  return true;
}

bool UserCollector::CopyStdinToCoreFile(const FilePath &core_path) {
  // Copy off all stdin to a core file.
  FilePath stdin_path("/dev/fd/0");
  if (file_util::CopyFile(stdin_path, core_path)) {
    return true;
  }

  logger_->LogError("Could not write core file");
  // If the file system was full, make sure we remove any remnants.
  file_util::Delete(core_path, false);
  return false;
}

bool UserCollector::RunCoreToMinidump(const FilePath &core_path,
                                      const FilePath &procfs_directory,
                                      const FilePath &minidump_path,
                                      const FilePath &temp_directory) {
  FilePath output_path = temp_directory.Append("output");
  std::vector<const char *> core2md_arguments;
  core2md_arguments.push_back(kCoreToMinidumpConverterPath);
  core2md_arguments.push_back(core_path.value().c_str());
  core2md_arguments.push_back(procfs_directory.value().c_str());
  core2md_arguments.push_back(minidump_path.value().c_str());

  if (FLAGS_core2md_failure) {
    // To test how core2md errors are propagaged, cause an error
    // by forgetting a required argument.
    core2md_arguments.pop_back();
  }

  int errorlevel = ForkExecAndPipe(core2md_arguments,
                                   output_path.value().c_str());

  std::string output;
  file_util::ReadFileToString(output_path, &output);
  if (errorlevel != 0) {
    logger_->LogError("Problem during %s [result=%d]: %s",
                      kCoreToMinidumpConverterPath,
                      errorlevel,
                      output.c_str());
    return false;
  }

  if (!file_util::PathExists(minidump_path)) {
    logger_->LogError("Minidump file %s was not created",
                      minidump_path.value().c_str());
    return false;
  }
  return true;
}

bool UserCollector::ConvertCoreToMinidump(pid_t pid,
                                          const FilePath &container_dir,
                                          const FilePath &core_path,
                                          const FilePath &minidump_path) {
  if (!CopyOffProcFiles(pid, container_dir)) {
    return false;
  }

  if (!CopyStdinToCoreFile(core_path)) {
    return false;
  }

  bool conversion_result = RunCoreToMinidump(
      core_path,
      container_dir,  // procfs directory
      minidump_path,
      container_dir);  // temporary directory

  if (conversion_result) {
    logger_->LogInfo("Stored minidump to %s", minidump_path.value().c_str());
  }

  return conversion_result;
}

bool UserCollector::ConvertAndEnqueueCrash(int pid,
                                           const std::string &exec,
                                           bool *out_of_capacity) {
  FilePath crash_path;
  if (!GetCreatedCrashDirectory(pid, &crash_path, out_of_capacity)) {
    logger_->LogError("Unable to find/create process-specific crash path");
    return false;
  }

  // Directory like /tmp/crash_reporter.1234 which contains the
  // procfs entries and other temporary files used during conversion.
  FilePath container_dir = FilePath("/tmp").Append(
      StringPrintf("crash_reporter.%d", pid));
  // Delete a pre-existing directory from crash reporter that may have
  // been left around for diagnostics from a failed conversion attempt.
  // If we don't, existing files can cause forking to fail.
  file_util::Delete(container_dir, true);
  std::string dump_basename = FormatDumpBasename(exec, time(NULL), pid);
  FilePath core_path = GetCrashPath(crash_path, dump_basename, "core");
  FilePath meta_path = GetCrashPath(crash_path, dump_basename, "meta");
  FilePath minidump_path = GetCrashPath(crash_path, dump_basename, "dmp");
  FilePath log_path = GetCrashPath(crash_path, dump_basename, "log");

  if (GetLogContents(FilePath(kDefaultLogConfig), exec, log_path))
    AddCrashMetaData("log", log_path.value());

  if (!ConvertCoreToMinidump(pid, container_dir, core_path,
                            minidump_path)) {
    logger_->LogInfo("Leaving core file at %s due to conversion error",
                     core_path.value().c_str());
    return false;
  }

  // Here we commit to sending this file.  We must not return false
  // after this point or we will generate a log report as well as a
  // crash report.
  WriteCrashMetaData(meta_path,
                     exec,
                     minidump_path.value());

  if (!file_util::PathExists(FilePath(kLeaveCoreFile))) {
    file_util::Delete(core_path, false);
  } else {
    logger_->LogInfo("Leaving core file at %s due to developer image",
                     core_path.value().c_str());
  }

  file_util::Delete(container_dir, true);
  return true;
}

bool UserCollector::ParseCrashAttributes(const std::string &crash_attributes,
                                         pid_t *pid, int *signal,
                                         std::string *kernel_supplied_name) {
  pcrecpp::RE re("(\\d+):(\\d+):(.*)");
  return re.FullMatch(crash_attributes, pid, signal, kernel_supplied_name);
}

bool UserCollector::HandleCrash(const std::string &crash_attributes,
                                const char *force_exec) {
  CHECK(initialized_);
  int pid = 0;
  int signal = 0;
  std::string kernel_supplied_name;

  if (!ParseCrashAttributes(crash_attributes, &pid, &signal,
                            &kernel_supplied_name)) {
    logger_->LogError("Invalid parameter: --user=%s", crash_attributes.c_str());
    return false;
  }

  std::string exec;
  if (force_exec) {
    exec.assign(force_exec);
  } else if (!GetExecutableBaseNameFromPid(pid, &exec)) {
    // If we cannot find the exec name, use the kernel supplied name.
    // We don't always use the kernel's since it truncates the name to
    // 16 characters.
    exec = StringPrintf("supplied_%s", kernel_supplied_name.c_str());
  }

  // Allow us to test the crash reporting mechanism successfully even if
  // other parts of the system crash.
  if (!FLAGS_filter_in.empty() &&
      (FLAGS_filter_in == "none" ||
       FLAGS_filter_in != exec)) {
    // We use a different format message to make it more obvious in tests
    // which crashes are test generated and which are real.
    logger_->LogWarning("Ignoring crash from %s[%d] while filter_in=%s.",
                        exec.c_str(), pid, FLAGS_filter_in.c_str());
    return true;
  }

  bool feedback = is_feedback_allowed_function_();
  const char *handling_string = "handling";
  if (!feedback) {
    handling_string = "ignoring - no consent";
  }

  // Treat Chrome crashes as if the user opted-out.  We stop counting Chrome
  // crashes towards user crashes, so user crashes really mean non-Chrome
  // user-space crashes.
  if (exec == "chrome" || exec == "supplied_chrome") {
    feedback = false;
    handling_string = "ignoring - chrome crash";
  }

  logger_->LogWarning("Received crash notification for %s[%d] sig %d (%s)",
                      exec.c_str(), pid, signal, handling_string);

  // For developer builds, we always want to keep the crash reports unless
  // we're testing the crash facilities themselves.
  if (file_util::PathExists(FilePath(kLeaveCoreFile)) &&
      !IsCrashTestInProgress()) {
    feedback = true;
  }

  if (feedback) {
    count_crash_function_();

    if (generate_diagnostics_) {
      bool out_of_capacity = false;
      bool convert_and_enqueue_result =
          ConvertAndEnqueueCrash(pid, exec, &out_of_capacity);
      if (!convert_and_enqueue_result) {
        if (!out_of_capacity)
          EnqueueCollectionErrorLog(pid, exec);
        return false;
      }
    }
  }

  return true;
}
