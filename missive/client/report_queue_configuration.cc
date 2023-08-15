// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/report_queue_configuration.h"

#include <string>
#include <string_view>
#include <utility>

#include <base/functional/bind.h>
#include <base/memory/ptr_util.h>

#include "missive/proto/record_constants.pb.h"
#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/statusor.h"

namespace reporting {

ReportQueueConfiguration::ReportQueueConfiguration() = default;
ReportQueueConfiguration::~ReportQueueConfiguration() = default;

StatusOr<std::unique_ptr<ReportQueueConfiguration>>
ReportQueueConfiguration::Create(EventType event_type,
                                 Destination destination,
                                 PolicyCheckCallback policy_check_callback,
                                 int64_t reserved_space) {
  auto config = base::WrapUnique<ReportQueueConfiguration>(
      new ReportQueueConfiguration());

  RETURN_IF_ERROR(config->SetEventType(event_type));
  RETURN_IF_ERROR(config->SetDestination(destination));
  RETURN_IF_ERROR(config->SetPolicyCheckCallback(policy_check_callback));
  RETURN_IF_ERROR(config->SetReservedSpace(reserved_space));

  return config;
}

StatusOr<std::unique_ptr<ReportQueueConfiguration>>
ReportQueueConfiguration::Create(std::string_view dm_token,
                                 Destination destination,
                                 PolicyCheckCallback policy_check_callback,
                                 int64_t reserved_space) {
  auto config_result = Create(/*event_type=*/EventType::kDevice, destination,
                              policy_check_callback, reserved_space);
  if (!config_result.ok()) {
    return config_result;
  }

  std::unique_ptr<ReportQueueConfiguration> config =
      std::move(config_result.ValueOrDie());
  RETURN_IF_ERROR(config->SetDMToken(dm_token));

  return std::move(config);
}

Status ReportQueueConfiguration::SetPolicyCheckCallback(
    PolicyCheckCallback policy_check_callback) {
  if (policy_check_callback.is_null()) {
    return (Status(error::INVALID_ARGUMENT,
                   "PolicyCheckCallback must not be null"));
  }
  policy_check_callback_ = std::move(policy_check_callback);
  return Status::StatusOK();
}

Status ReportQueueConfiguration::SetEventType(EventType event_type) {
  event_type_ = event_type;
  return Status::StatusOK();
}

Status ReportQueueConfiguration::CheckPolicy() const {
  return policy_check_callback_.Run();
}

Status ReportQueueConfiguration::SetDMToken(std::string_view dm_token) {
  dm_token_ = std::string(dm_token);
  return Status::StatusOK();
}

Status ReportQueueConfiguration::SetDestination(Destination destination) {
  if (destination == Destination::UNDEFINED_DESTINATION) {
    return Status(error::INVALID_ARGUMENT, "Destination must be defined");
  }
  destination_ = destination;
  return Status::StatusOK();
}

Status ReportQueueConfiguration::SetReservedSpace(int64_t reserved_space) {
  if (reserved_space < 0L) {
    return Status(error::INVALID_ARGUMENT,
                  "Must reserve non-zero amount of space");
  }
  reserved_space_ = reserved_space;
  return Status::StatusOK();
}
}  // namespace reporting
