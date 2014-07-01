// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_KERNEL_COLLECTOR_H_
#define CRASH_REPORTER_KERNEL_COLLECTOR_H_

#include <pcrecpp.h>

#include <string>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "crash-reporter/crash_collector.h"

// Kernel crash collector.
class KernelCollector : public CrashCollector {
 public:
  // Enumeration to specify architecture type.
  enum ArchKind {
    archUnknown,
    archArm,
    archX86,
    archX86_64,

    archCount  // Number of architectures.
  };

  KernelCollector();

  virtual ~KernelCollector();

  void OverridePreservedDumpPath(const base::FilePath &file_path);

  // Enable collection.
  bool Enable();

  // Returns true if the kernel collection currently enabled.
  bool IsEnabled() {
    return is_enabled_;
  }

  // Collect any preserved kernel crash dump. Returns true if there was
  // a dump (even if there were problems storing the dump), false otherwise.
  bool Collect();

  // Compute a stack signature string from a kernel dump.
  bool ComputeKernelStackSignature(const std::string &kernel_dump,
                                   std::string *kernel_signature,
                                   bool print_diagnostics);

  // Set the architecture of the crash dumps we are looking at.
  void SetArch(enum ArchKind arch);
  enum ArchKind GetArch() { return arch_; }

 private:
  friend class KernelCollectorTest;
  FRIEND_TEST(KernelCollectorTest, LoadPreservedDump);
  FRIEND_TEST(KernelCollectorTest, StripSensitiveDataBasic);
  FRIEND_TEST(KernelCollectorTest, StripSensitiveDataBulk);
  FRIEND_TEST(KernelCollectorTest, StripSensitiveDataSample);
  FRIEND_TEST(KernelCollectorTest, CollectOK);

  bool LoadPreservedDump(std::string *contents);
  void StripSensitiveData(std::string *kernel_dump);

  void GetRamoopsRecordPath(base::FilePath *path, size_t record);
  virtual bool LoadParameters();
  bool HasMoreRecords();

  // Read a record to string, modified from file_utils since that didn't
  // provide a way to restrict the read length.
  // Return value indicates (only) error state:
  //  * false when we get an error (can't read from dump location).
  //  * true if no error occured.
  // Not finding a valid record is not an error state and is signaled by the
  // record_found output parameter.
  bool ReadRecordToString(std::string *contents,
                          size_t current_record,
                          bool *record_found);

  void ProcessStackTrace(pcrecpp::StringPiece kernel_dump,
                         bool print_diagnostics,
                         unsigned *hash,
                         float *last_stack_timestamp,
                         bool *is_watchdog_crash);
  bool FindCrashingFunction(pcrecpp::StringPiece kernel_dump,
                            bool print_diagnostics,
                            float stack_trace_timestamp,
                            std::string *crashing_function);
  bool FindPanicMessage(pcrecpp::StringPiece kernel_dump,
                        bool print_diagnostics,
                        std::string *panic_message);

  // Returns the architecture kind for which we are built - enum ArchKind.
  enum ArchKind GetCompilerArch(void);

  bool is_enabled_;
  base::FilePath ramoops_dump_path_;
  size_t records_;

  // The architecture of kernel dump strings we are working with.
  enum ArchKind arch_;
};

#endif  // CRASH_REPORTER_KERNEL_COLLECTOR_H_
