// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "regmon/regmon/regmon_impl.h"

#include <base/logging.h>
#include <metrics/metrics_library.h>

#include "regmon/metrics/metrics_reporter_impl.h"
#include "regmon/proto/policy_violation.pb.h"

namespace regmon {

RegmonImpl::~RegmonImpl() {}

void RegmonImpl::RecordPolicyViolation(
    const RecordPolicyViolationRequest& in_request,
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<RecordPolicyViolationResponse>>
        out_response) {
  RecordPolicyViolationResponse response_body;
  auto* status = response_body.mutable_status();

  PolicyViolation violation = in_request.violation();
  bool uma_sent = false;
  if (!violation.has_policy()) {
    status->set_error_message("No policy found. Please set a policy value.");
  } else if (!violation.has_annotation_hash()) {
    status->set_error_message(
        "No annotation hash found. Please set an annotation hash.");
  } else {
    uma_sent = metrics_reporter_->ReportAnnotationViolation(
        violation.annotation_hash());
  }

  if (!uma_sent) {
    LOG(INFO) << "No UMA sent!\n";
  }

  out_response->Return(response_body);
  return;
}

}  // namespace regmon
