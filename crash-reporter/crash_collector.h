// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _CRASH_REPORTER_CRASH_COLLECTOR_H_
#define _CRASH_REPORTER_CRASH_COLLECTOR_H_

#include <sys/stat.h>

#include <map>
#include <string>

#include "base/file_path.h"
#include "gtest/gtest_prod.h"  // for FRIEND_TEST

// User crash collector.
class CrashCollector {
 public:
  typedef void (*CountCrashFunction)();
  typedef bool (*IsFeedbackAllowedFunction)();

  CrashCollector();

  virtual ~CrashCollector();

  // Initialize the crash collector for detection of crashes, given a
  // crash counting function, and metrics collection enabled oracle.
  void Initialize(CountCrashFunction count_crash,
                  IsFeedbackAllowedFunction is_metrics_allowed);

  // TODO(crosbug.com/30268): refactor into separate class.
  bool HandleUdevCrash(const std::string &udev_event);

 protected:
  friend class CrashCollectorTest;
  FRIEND_TEST(CrashCollectorTest, CheckHasCapacityCorrectBasename);
  FRIEND_TEST(CrashCollectorTest, CheckHasCapacityStrangeNames);
  FRIEND_TEST(CrashCollectorTest, CheckHasCapacityUsual);
  FRIEND_TEST(CrashCollectorTest, GetCrashDirectoryInfo);
  FRIEND_TEST(CrashCollectorTest, GetCrashPath);
  FRIEND_TEST(CrashCollectorTest, GetLogContents);
  FRIEND_TEST(CrashCollectorTest, ForkExecAndPipe);
  FRIEND_TEST(CrashCollectorTest, FormatDumpBasename);
  FRIEND_TEST(CrashCollectorTest, Initialize);
  FRIEND_TEST(CrashCollectorTest, IsCommentLine);
  FRIEND_TEST(CrashCollectorTest, MetaData);
  FRIEND_TEST(CrashCollectorTest, ReadKeyValueFile);
  FRIEND_TEST(CrashCollectorTest, Sanitize);
  FRIEND_TEST(CrashCollectorTest, WriteNewFile);
  FRIEND_TEST(ForkExecAndPipeTest, Basic);
  FRIEND_TEST(ForkExecAndPipeTest, NonZeroReturnValue);
  FRIEND_TEST(ForkExecAndPipeTest, BadOutputFile);
  FRIEND_TEST(ForkExecAndPipeTest, ExistingOutputFile);
  FRIEND_TEST(ForkExecAndPipeTest, BadExecutable);
  FRIEND_TEST(ForkExecAndPipeTest, StderrCaptured);
  FRIEND_TEST(ForkExecAndPipeTest, NULLParam);
  FRIEND_TEST(ForkExecAndPipeTest, NoParams);
  FRIEND_TEST(ForkExecAndPipeTest, SegFaultHandling);

  // Set maximum enqueued crashes in a crash directory.
  static const int kMaxCrashDirectorySize;

  // Writes |data| of |size| to |filename|, which must be a new file.
  // If the file already exists or writing fails, return a negative value.
  // Otherwise returns the number of bytes written.
  int WriteNewFile(const FilePath &filename, const char *data, int size);

  // Return a filename that has only [a-z0-1_] characters by mapping
  // all others into '_'.
  std::string Sanitize(const std::string &name);

  // For testing, set the directory always returned by
  // GetCreatedCrashDirectoryByEuid.
  void ForceCrashDirectory(const char *forced_directory) {
    forced_crash_directory_ = forced_directory;
  }

  FilePath GetCrashDirectoryInfo(uid_t process_euid,
                                 uid_t default_user_id,
                                 gid_t default_user_group,
                                 mode_t *mode,
                                 uid_t *directory_owner,
                                 gid_t *directory_group);
  bool GetUserInfoFromName(const std::string &name,
                           uid_t *uid,
                           gid_t *gid);
  // Determines the crash directory for given eud, and creates the
  // directory if necessary with appropriate permissions.  If
  // |out_of_capacity| is not NULL, it is set to indicate if the call
  // failed due to not having capacity in the crash directory. Returns
  // true whether or not directory needed to be created, false on any
  // failure.  If the crash directory is at capacity, returns false.
  bool GetCreatedCrashDirectoryByEuid(uid_t euid,
                                      FilePath *crash_file_path,
                                      bool *out_of_capacity);

  // Format crash name based on components.
  std::string FormatDumpBasename(const std::string &exec_name,
                                 time_t timestamp,
                                 pid_t pid);

  // Create a file path to a file in |crash_directory| with the given
  // |basename| and |extension|.
  FilePath GetCrashPath(const FilePath &crash_directory,
                        const std::string &basename,
                        const std::string &extension);

  // Check given crash directory still has remaining capacity for another
  // crash.
  bool CheckHasCapacity(const FilePath &crash_directory);

  // Checks if the line starts with '#' after optional whitespace.
  static bool IsCommentLine(const std::string &line);

  // Read the given file of form [<key><separator><value>\n...] and return
  // a map of its contents.
  bool ReadKeyValueFile(const FilePath &file,
                        char separator,
                        std::map<std::string, std::string> *dictionary);

  // Write a log applicable to |exec_name| to |output_file| based on the
  // log configuration file at |config_path|.
  bool GetLogContents(const FilePath &config_path,
                      const std::string &exec_name,
                      const FilePath &output_file);

  // Add non-standard meta data to the crash metadata file.  Call
  // before calling WriteCrashMetaData.  Key must not contain "=" or
  // "\n" characters.  Value must not contain "\n" characters.
  void AddCrashMetaData(const std::string &key, const std::string &value);

  // Write a file of metadata about crash.
  void WriteCrashMetaData(const FilePath &meta_path,
                          const std::string &exec_name,
                          const std::string &payload_path);

  // Returns true if the a crash test is currently running.
  bool IsCrashTestInProgress();
  // Returns true if we should consider ourselves to be running on a
  // developer image.
  bool IsDeveloperImage();
  // Returns true if chrome crashes should be handled.
  bool ShouldHandleChromeCrashes();
  // Returns true if user crash directory may be used.
  bool IsUserSpecificDirectoryEnabled();

  CountCrashFunction count_crash_function_;
  IsFeedbackAllowedFunction is_feedback_allowed_function_;
  std::string extra_metadata_;
  const char *forced_crash_directory_;
  const char *lsb_release_;
};

#endif  // _CRASH_REPORTER_CRASH_COLLECTOR_H_
