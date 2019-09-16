// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CHROME_COLLECTOR_H_
#define CRASH_REPORTER_CHROME_COLLECTOR_H_

#include <map>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/crash_collector.h"

// Chrome crash collector.
class ChromeCollector : public CrashCollector {
 public:
  explicit ChromeCollector(CrashSendingMode crash_sending_mode);
  ~ChromeCollector() override;

  // Magic string to let Chrome know the crash report succeeded.
  static const char kSuccessMagic[];

  // Handle a specific chrome crash.  Returns true on success.
  bool HandleCrash(const base::FilePath& file_path,
                   pid_t pid,
                   uid_t uid,
                   const std::string& exe_name);

  // Handle a specific chrome crash through a memfd instead of a file.
  // Returns true on success.
  bool HandleCrashThroughMemfd(int memfd,
                               pid_t pid,
                               uid_t uid,
                               const std::string& exe_name,
                               const std::string& dump_dir);

  void set_max_upload_bytes_for_test(int max_upload_bytes) {
    max_upload_bytes_ = max_upload_bytes;
  }

 private:
  friend class ChromeCollectorTest;
  FRIEND_TEST(ChromeCollectorTest, GoodValues);
  FRIEND_TEST(ChromeCollectorTest, BadValues);
  FRIEND_TEST(ChromeCollectorTest, Newlines);
  FRIEND_TEST(ChromeCollectorTest, File);
  FRIEND_TEST(ChromeCollectorTest, HandleCrash);
  FRIEND_TEST(ChromeCollectorTest, HandleCrashWithEmbeddedNuls);

  // Handle a specific chrome crash with dump data.
  // Returns true on success.
  bool HandleCrashWithDumpData(const std::string& data,
                               pid_t pid,
                               uid_t uid,
                               const std::string& exe_name,
                               const std::string& dump_dir);

  // Crashes are expected to be in a TLV-style format of:
  // <name>:<length>:<value>
  // Length is encoded as a decimal number. It can be zero, but must consist of
  // at least one character
  // For file values, name actually contains both a description and a filename,
  // in a fixed format of: <description>"; filename="<filename>"
  bool ParseCrashLog(const std::string& data,
                     const base::FilePath& dir,
                     const base::FilePath& minidump,
                     const std::string& basename);

  // Gets the GPU's error state from debugd and writes it to |error_state_path|.
  // Returns true on success.
  bool GetDriErrorState(const base::FilePath& error_state_path);

  // Writes additional logs for |exe_name| to files based on |basename| within
  // |dir|. Crash report metadata key names and the corresponding file paths are
  // returned.
  std::map<std::string, base::FilePath> GetAdditionalLogs(
      const base::FilePath& dir,
      const std::string& basename,
      const std::string& exe_name);

  // Add the (|log_map_key|, |complete_file_name|) pair to |logs| if we are not
  // over kDefaultMaxUploadBytes. If we are over kDefaultMaxUploadBytes,
  // delete the file |complete_file_name| instead and don't change |logs|.
  // |complete_file_name| must be a file created by
  // CrashCollector::WriteNewFile() or CrashCollector::WriteNewCompressedFile()
  // so that CrashCollector::RemoveNewFile() works on it.
  void AddLogIfNotTooBig(const char* log_map_key,
                         const base::FilePath& complete_file_name,
                         std::map<std::string, base::FilePath>* logs);

  // The file where we write our special "done" marker (to indicate to Chrome
  // that we are finished dumping). Always stdout in production.
  FILE* output_file_ptr_;

  // We skip uploading the supplemental files (logs, i915_error_state) if it
  // would make the report larger than max_upload_bytes_. In production, this
  // is always kDefaultMaxUploadBytes.
  int max_upload_bytes_;

  DISALLOW_COPY_AND_ASSIGN(ChromeCollector);
};

#endif  // CRASH_REPORTER_CHROME_COLLECTOR_H_
