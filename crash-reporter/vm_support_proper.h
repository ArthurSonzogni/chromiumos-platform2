// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_VM_SUPPORT_PROPER_H_
#define CRASH_REPORTER_VM_SUPPORT_PROPER_H_

#include "crash-reporter/vm_support.h"

#include <memory>
#include <string>

#include <vm_protos/proto_bindings/vm_crash.grpc.pb.h>

class VmSupportProper : public VmSupport {
 public:
  VmSupportProper();

  void AddMetadata(UserCollector* collector) override;

  void FinishCrash(const base::FilePath& crash_meta_path) override;

  bool GetMetricsConsent() override;

  bool ShouldDump(pid_t pid, std::string* out_reason) override;

 private:
  std::unique_ptr<vm_tools::cicerone::CrashListener::Stub> stub_;
};

#endif  // CRASH_REPORTER_VM_SUPPORT_PROPER_H_
