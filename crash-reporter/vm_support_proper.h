// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_VM_SUPPORT_PROPER_H_
#define CRASH_REPORTER_VM_SUPPORT_PROPER_H_

#include "crash-reporter/vm_support.h"

#include <memory>
#include <string>

#include <base/types/expected.h>
#include <vm_protos/proto_bindings/vm_crash.grpc.pb.h>

namespace brillo {
class KeyValueStore;
}
enum class CrashCollectionStatus;

class VmSupportProper : public VmSupport {
 public:
  VmSupportProper();

  void AddMetadata(UserCollector* collector) override;

  void FinishCrash(const base::FilePath& crash_meta_path) override;

  bool GetMetricsConsent() override;

  base::expected<void, CrashCollectionStatus> ShouldDump(pid_t pid) override;

  static const char kFilterConfigPath[];

 private:
  friend class VmSupportProperTest;

  base::expected<void, CrashCollectionStatus> InRootProcessNamespace(pid_t pid);
  base::expected<void, CrashCollectionStatus> PassesFilterConfig(pid_t pid);

  void ProcessFileData(const base::FilePath& crash_meta_path,
                       const brillo::KeyValueStore& metadata,
                       const std::string& key,
                       vm_tools::cicerone::CrashReport* crash_report);

  std::unique_ptr<vm_tools::cicerone::CrashListener::Stub> stub_;
};

#endif  // CRASH_REPORTER_VM_SUPPORT_PROPER_H_
