// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// UserCollectorBase implements common functionality shared by user collectors.
// (e.g. user_collector, arcpp_cxx_collector).

#ifndef CRASH_REPORTER_USER_COLLECTOR_BASE_H_
#define CRASH_REPORTER_USER_COLLECTOR_BASE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/crash_collector.h"

enum class CrashCollectionStatus;

// UserCollectorBase is the base class for two classes that collect userspace
// crashes passed to us as coredumps (`man 5 core`): UserCollector, which
// collects userspace crashes from the Host OS via
// /proc/sys/kernel/core_pattern, and  ArcppCxxCollector, which collects
// ARC++ system crashes.
//
// Crashes are passed into the |HandleCrash| function of this class, which has
// the high-level flow but defers some decisions on whether or not to handle a
// particular crash to its subclasses (via |ShouldDump| overrides) and defers
// the details of how to turn the coredump into a minidump to its subclasses
// (via the |ConvertCoreToMinidump| overrides).
class UserCollectorBase : public CrashCollector {
 public:
  explicit UserCollectorBase(
      CrashReporterCollector collector,
      CrashDirectorySelectionMethod crash_directory_selection_method,
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);

  void Initialize(bool directory_failure, bool early);

  struct CrashAttributes {
    pid_t pid;
    int signal;
    uid_t uid;
    gid_t gid;
    std::string exec_name;
  };

  // Handle a single userspace crash. The main entry point into this class.
  CrashCollectionStatus HandleCrash(const CrashAttributes& crash_attributes,
                                    const char* force_exec);

  // Attempt to parse a given attributes string into a CrashAttributes struct.
  // The attributes string is generated in the kernel by the core_pattern
  // specification %P:%s:%u:%g:%f, and consists of the pid, the signal
  // responsible for terminating the process, the uid, the gid, and the
  // executable's name, separated by colons.
  // For example, an input string 123456:11:1000:2000:foobar is pid
  // 123456, signal 11, uid 1000, gid 2000, and exec name "foobar".
  // See man 5 core for details on the format.
  static std::optional<CrashAttributes> ParseCrashAttributes(
      const std::string& crash_attributes);

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

  // Helper for the virtual ShouldDump. Returns base::ok() if a crash report
  // should be generated for this crash, base::unexpected() with a reason code
  // if not. Note that UserCollectorBase::HandleCrash does not call this
  // function directly; it's expected that the subclass's override of
  // |ShouldDump(pid_t pid, uid_t uid, const std::string& exec) will call back
  // down to either this function or |ShouldDump()| - but not both.
  base::expected<void, CrashCollectionStatus> ShouldDump(
      std::optional<pid_t> pid) const;

  // Helper for the virtual ShouldDump. Equivalent to calling
  // |ShouldDump(std::nullopt)| (above).
  base::expected<void, CrashCollectionStatus> ShouldDump() const;

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

  // Checks if Rust panic signature was left behind by the ChromeOS panic hook,
  // and if so, returns true and sets |panic_sig|.
  bool GetRustSignature(pid_t pid, std::string* panic_sig);

  bool ClobberContainerDirectory(const base::FilePath& container_dir);

  // Returns the command and arguments for process |pid|. Returns an empty list
  // on failure or if the process is a zombie. Virtual for testing.
  virtual std::vector<std::string> GetCommandLine(pid_t pid) const;

  // Path under which all temporary crash processing occurs.
  const base::FilePath GetCrashProcessingDir();

  bool initialized_ = false;

  static const char* kUserId;
  static const char* kGroupId;

 private:
  FRIEND_TEST(UserCollectorTest, HandleSyscall);

  // Send DBus message announcing the crash. Virtual so that we can mock out
  // during unit tests.
  virtual void AnnounceUserCrash();

  // Called early in HandleCrash, specifically before ShouldDump. This can be
  // overridden by child classes to set up state based on the executable name
  // and directory that is needed in multiple places later in the crash handling
  // process (such as in both ShouldDump and ConvertCoreToMinidump).
  //
  // Default is a no-op.
  virtual void BeginHandlingCrash(pid_t pid,
                                  const std::string& exec,
                                  const base::FilePath& exec_directory);

  // Returns base::ok if we should dump, or a reason code if we should not
  // dump. Note that many reason-not-to-dump codes will be success codes
  // (expected cases where we don't dump), so avoid using IsSuccessCode
  // on the reason enum to determine if the caller should generate a dump.
  virtual base::expected<void, CrashCollectionStatus> ShouldDump(
      pid_t pid, uid_t uid, const std::string& exec) = 0;

  virtual CrashCollectionStatus ConvertCoreToMinidump(
      pid_t pid,
      const base::FilePath& container_dir,
      const base::FilePath& core_path,
      const base::FilePath& minidump_path) = 0;

  // Adds additional metadata for a crash of executable |exec| with |pid|.
  virtual void AddExtraMetadata(const std::string& exec, pid_t pid) {}

  CrashCollectionStatus ConvertAndEnqueueCrash(
      pid_t pid,
      const std::string& exec,
      uid_t supplied_ruid,
      gid_t supplied_rgid,
      int signal,
      const base::TimeDelta& crash_time,
      bool* out_of_capacity);

  // Helper function for populating seccomp related fields from the contents of
  // /proc/<pid>/syscall.
  void HandleSyscall(const std::string& exec, const std::string& contents);

  // Determines the crash directory for given pid based on pid's owner,
  // and creates the directory if necessary with appropriate permissions.
  // Returns kSuccess whether or not directory needed to be created.
  CrashCollectionStatus GetCreatedCrashDirectory(
      pid_t pid,
      uid_t supplied_ruid,
      base::FilePath* crash_file_path,
      bool* out_of_capacity);

  bool directory_failure_ = false;
};

#endif  // CRASH_REPORTER_USER_COLLECTOR_BASE_H_
