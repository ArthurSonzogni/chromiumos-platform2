// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_USER_COLLECTOR_BASE_H_
#define CRASH_REPORTER_USER_COLLECTOR_BASE_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>

#include "crash-reporter/crash_collector.h"

// Common functionality shared by user collectors.
class UserCollectorBase : public CrashCollector {
 public:
  UserCollectorBase(const char* tag, bool force_user_crash_dir);

  void Initialize(CountCrashFunction count_crash,
                  IsFeedbackAllowedFunction is_metrics_allowed,
                  bool generate_diagnostics,
                  bool directory_failure,
                  const std::string& filter_in);

  // Handle a specific user crash.  Returns true on success.
  bool HandleCrash(const std::string& crash_attributes, const char* force_exec);

 protected:
  // Enumeration to pass to GetIdFromStatus.  Must match the order
  // that the kernel lists IDs in the status file.
  enum IdKind : int {
    kIdReal = 0,        // uid and gid
    kIdEffective = 1,   // euid and egid
    kIdSet = 2,         // suid and sgid
    kIdFileSystem = 3,  // fsuid and fsgid
    kIdMax
  };

  enum ErrorType {
    kErrorNone,
    kErrorSystemIssue,
    kErrorReadCoreData,
    kErrorUnusableProcFiles,
    kErrorInvalidCoreFile,
    kErrorUnsupported32BitCoreFile,
    kErrorCore2MinidumpConversion,
  };

  bool ParseCrashAttributes(const std::string& crash_attributes,
                            pid_t* pid,
                            int* signal,
                            uid_t* uid,
                            std::string* exec_name);

  bool ShouldDump(bool has_owner_consent,
                  bool is_developer,
                  std::string* reason) const;

  // Logs a |message| detailing a crash, along with the |reason| for which the
  // collector handled or ignored it.
  void LogCrash(const std::string& message, const std::string& reason) const;

  // Returns, via |line|, the first line in |lines| that starts with |prefix|.
  // Returns true if a line is found, or false otherwise.
  bool GetFirstLineWithPrefix(const std::vector<std::string>& lines,
                              const char* prefix,
                              std::string* line);

  // Returns the identifier of |kind|, via |id|, found in |status_lines| on
  // the line starting with |prefix|. |status_lines| contains the lines in
  // the status file. Returns true if the identifier can be determined.
  bool GetIdFromStatus(const char* prefix,
                       IdKind kind,
                       const std::vector<std::string>& status_lines,
                       int* id);

  // Returns the process state, via |state|, found in |status_lines|, which
  // contains the lines in the status file. Returns true if the process state
  // can be determined.
  bool GetStateFromStatus(const std::vector<std::string>& status_lines,
                          std::string* state);

  bool ClobberContainerDirectory(const base::FilePath& container_dir);

  void EnqueueCollectionErrorLog(pid_t pid,
                                 ErrorType error_type,
                                 const std::string& exec_name);

  // Returns the command and arguments for process |pid|. Returns an empty list
  // on failure or if the process is a zombie. Virtual for testing.
  virtual std::vector<std::string> GetCommandLine(pid_t pid) const;

  // Path under which all temporary crash processing occurs.
  const base::FilePath GetCrashProcessingDir();

  bool initialized_ = false;

  static const char* kUserId;
  static const char* kGroupId;

 private:
  virtual bool ShouldDump(pid_t pid,
                          uid_t uid,
                          const std::string& exec,
                          std::string* reason) = 0;

  virtual ErrorType ConvertCoreToMinidump(
      pid_t pid,
      const base::FilePath& container_dir,
      const base::FilePath& core_path,
      const base::FilePath& minidump_path) = 0;

  // Adds additional metadata for a crash of executable |exec| with |pid|.
  virtual void AddExtraMetadata(const std::string& exec, pid_t pid) {}

  ErrorType ConvertAndEnqueueCrash(pid_t pid,
                                   const std::string& exec,
                                   uid_t supplied_ruid,
                                   const base::TimeDelta& crash_time,
                                   bool* out_of_capacity);

  // Returns an error type signature for a given |error_type| value,
  // which is reported to the crash server along with the
  // crash_reporter-user-collection signature.
  std::string GetErrorTypeSignature(ErrorType error_type) const;

  // Determines the crash directory for given pid based on pid's owner,
  // and creates the directory if necessary with appropriate permissions.
  // Returns true whether or not directory needed to be created, false on
  // any failure.
  bool GetCreatedCrashDirectory(pid_t pid,
                                uid_t supplied_ruid,
                                base::FilePath* crash_file_path,
                                bool* out_of_capacity);

#if USE_DIRENCRYPTION
  // Joins the session keyring to get the directory encryption keys.
  void JoinSessionKeyring();
#endif  // USE_DIRENCRYPTION

  // Prepended to log messages to differentiate between collectors.
  const char* const tag_;

  bool generate_diagnostics_ = false;
  bool directory_failure_ = false;
  std::string filter_in_;
};

#endif  // CRASH_REPORTER_USER_COLLECTOR_BASE_H_
