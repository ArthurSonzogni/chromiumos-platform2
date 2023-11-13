// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGMON_REGMON_REGMON_IMPL_H_
#define REGMON_REGMON_REGMON_IMPL_H_

#include <memory>

#include "regmon/proto/policy_violation.pb.h"
#include "regmon/regmon/regmon_service.h"

namespace regmon {

class RegmonImpl : public RegmonService {
 public:
  RegmonImpl();
  RegmonImpl(const RegmonImpl&) = delete;
  RegmonImpl& operator=(const RegmonImpl&) = delete;
  ~RegmonImpl() override;

  void RecordPolicyViolation(
      const RecordPolicyViolationRequest& in_request,
      std::unique_ptr<
          brillo::dbus_utils::DBusMethodResponse<RecordPolicyViolationResponse>>
          out_response) override;
};

}  // namespace regmon

#endif  // REGMON_REGMON_REGMON_IMPL_H_
