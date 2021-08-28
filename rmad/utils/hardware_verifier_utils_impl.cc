// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/hardware_verifier_utils_impl.h"

#include <string>
#include <vector>

#include <base/process/launch.h>
#include <hardware_verifier/hardware_verifier.pb.h>
#include <runtime_probe/proto_bindings/runtime_probe.pb.h>

namespace {

std::string GetComponentIdentifier(
    const hardware_verifier::ComponentInfo& info) {
  return runtime_probe::ProbeRequest::SupportCategory_Name(
      info.component_category());
}

}  // namespace

namespace rmad {

const char kHardwareVerifierCmdPath[] = "/usr/bin/hardware_verifier";

bool HardwareVerifierUtilsImpl::GetHardwareVerificationResult(
    HardwareVerificationResult* result) const {
  hardware_verifier::HwVerificationReport report;
  if (!RunHardwareVerifier(&report)) {
    return false;
  }

  result->set_is_compliant(report.is_compliant());
  std::string error_str;
  for (int i = 0; i < report.found_component_infos_size(); ++i) {
    const hardware_verifier::ComponentInfo& info =
        report.found_component_infos(i);
    if (info.qualification_status() != hardware_verifier::QUALIFIED) {
      error_str += GetComponentIdentifier(info);
      error_str += "\n";
    }
  }
  result->set_error_str(error_str);
  return true;
}

bool HardwareVerifierUtilsImpl::RunHardwareVerifier(
    hardware_verifier::HwVerificationReport* report) const {
  std::string proto_output;
  if (!base::GetAppOutput(std::vector<std::string>{kHardwareVerifierCmdPath},
                          &proto_output)) {
    LOG(INFO) << "GetAppOutput failed";
    return false;
  }

  LOG(INFO) << "GetAppOutput success";

  if (!report->ParseFromString(proto_output)) {
    LOG(INFO) << "Parse failed";
    return false;
  }

  LOG(INFO) << "Parse success";
  return true;
}

}  // namespace rmad
