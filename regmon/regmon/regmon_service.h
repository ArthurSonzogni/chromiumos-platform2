// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_REGMON_REGMON_SERVICE_H_
#define REGMON_REGMON_REGMON_SERVICE_H_

#include <memory>

#include <brillo/dbus/dbus_method_response.h>
#include <dbus/bus.h>

#include "regmon/proto/policy_violation.pb.h"

namespace regmon {

class RegmonService {
 public:
  RegmonService(const RegmonService&) = delete;
  RegmonService& operator=(const RegmonService&) = delete;
  virtual ~RegmonService() = default;

  virtual void RecordPolicyViolation(
      const RecordPolicyViolationRequest& in_request,
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<RecordPolicyViolationResponse>>
          out_response) = 0;

 protected:
  RegmonService() = default;
};

}  // namespace regmon

#endif  // REGMON_REGMON_REGMON_SERVICE_H_
