// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_VM_COLLECTOR_H_
#define CRASH_REPORTER_VM_COLLECTOR_H_

#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>
#include <metrics/metrics_library.h>

#include "crash-reporter/crash_collector.h"

// Collector for processing crashes inside a VM. This collector runs on the host
// and is used to write out a crash report to the appropriate location. For the
// code that manages generating reports inside the VM, see VmSupportProper.
class VmCollector : public CrashCollector {
 public:
  explicit VmCollector(
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
  VmCollector(const VmCollector&) = delete;
  VmCollector& operator=(const VmCollector&) = delete;

  bool Collect(pid_t pid);

  // Returns the severity level and product group of the crash.
  CrashCollector::ComputedCrashSeverity ComputeSeverity(
      const std::string& exec_name) override;

  static CollectorInfo GetHandlerInfo(
      bool vm_crash,
      int32_t vm_pid,
      const scoped_refptr<
          base::RefCountedData<std::unique_ptr<MetricsLibraryInterface>>>&
          metrics_lib);
};

#endif  // CRASH_REPORTER_VM_COLLECTOR_H_
