// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_REGMON_REGMON_IMPL_H_
#define REGMON_REGMON_REGMON_IMPL_H_

#include <memory>
#include <utility>

#include <metrics/metrics_library.h>
#include <regmon/proto_bindings/regmon_service.pb.h>

#include "regmon/metrics/metrics_reporter_impl.h"
#include "regmon/regmon/regmon_service.h"

namespace regmon {

class RegmonImpl : public RegmonService {
 public:
  RegmonImpl()
      : RegmonImpl(
            std::make_unique<metrics::MetricsReporterImpl>(metrics_lib_)) {}
  explicit RegmonImpl(
      std::unique_ptr<metrics::MetricsReporter> metrics_reporter)
      : metrics_reporter_(std::move(metrics_reporter)) {}
  RegmonImpl(const RegmonImpl&) = delete;
  RegmonImpl& operator=(const RegmonImpl&) = delete;
  ~RegmonImpl() override;

  void RecordPolicyViolation(
      const RecordPolicyViolationRequest& in_request,
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<RecordPolicyViolationResponse>>
          out_response) override;

 private:
  MetricsLibrary metrics_lib_;
  std::unique_ptr<metrics::MetricsReporter> metrics_reporter_;
};

}  // namespace regmon

#endif  // REGMON_REGMON_REGMON_IMPL_H_
