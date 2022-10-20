// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/message_sender.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "base/task/sequenced_task_runner.h"
#include "missive/client/report_queue.h"
#include "missive/client/report_queue_configuration.h"
#include "missive/client/report_queue_factory.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "missive/util/status.h"

namespace {

void EnqueueCallback(reporting::Destination destination,
                     ::reporting::Status status) {
  if (!status.ok()) {
    LOG(ERROR) << destination << ", status=" << status;
  }
}

}  // namespace

namespace secagentd {

namespace pb = cros_xdr::reporting;

absl::Status MessageSender::InitializeQueues() {
  // Array of possible destinations.
  const reporting::Destination kDestinations[] = {
      reporting::CROS_SECURITY_PROCESS, reporting::CROS_SECURITY_AGENT};

  for (auto destination : kDestinations) {
    auto report_queue_result =
        reporting::ReportQueueFactory::CreateSpeculativeReportQueue(
            reporting::EventType::kDevice, destination);

    if (report_queue_result == nullptr) {
      return absl::InternalError(
          "InitializeQueues: Report queue failed to create");
    }
    queue_map_.insert(
        std::make_pair(destination, std::move(report_queue_result)));
  }

  return absl::OkStatus();
}

absl::Status MessageSender::SendMessage(
    reporting::Destination destination,
    pb::CommonEventDataFields* mutable_common,
    std::unique_ptr<google::protobuf::MessageLite> message) {
  auto it = queue_map_.find(destination);
  CHECK(it != queue_map_.end());

  it->second.get()->Enqueue(std::move(message), ::reporting::SECURITY,
                            base::BindOnce(&EnqueueCallback, destination));
  return absl::OkStatus();
}

}  // namespace secagentd
