// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_MESSAGE_SENDER_H_
#define SECAGENTD_MESSAGE_SENDER_H_

#include <memory>
#include <unordered_map>

#include "absl/status/status.h"
#include "base/task/sequenced_task_runner.h"
#include "google/protobuf/message_lite.h"
#include "missive/client/report_queue.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"

namespace secagentd {

class MessageSenderInterface
    : public base::RefCountedThreadSafe<MessageSenderInterface> {
 public:
  virtual absl::Status InitializeQueues() = 0;
  virtual absl::Status SendMessage(
      reporting::Destination destination,
      cros_xdr::reporting::CommonEventDataFields* mutable_common,
      std::unique_ptr<google::protobuf::MessageLite> message) = 0;
  virtual ~MessageSenderInterface() = default;
};

class MessageSender : public MessageSenderInterface {
 public:
  // Initializes a queue for each destination and stores result into queue_map.
  absl::Status InitializeQueues() override;

  // Creates and enqueues a given proto message to the given destination.
  // Populates mutable_common with common fields if not nullptr. mutable_common
  // must be owned within message.
  absl::Status SendMessage(
      reporting::Destination destination,
      cros_xdr::reporting::CommonEventDataFields* mutable_common,
      std::unique_ptr<google::protobuf::MessageLite> message) override;

 private:
  // Map linking each destination to its corresponding Report_Queue.
  std::unordered_map<
      reporting::Destination,
      std::unique_ptr<reporting::ReportQueue, base::OnTaskRunnerDeleter>>
      queue_map_;
};

}  // namespace secagentd

#endif  // SECAGENTD_MESSAGE_SENDER_H_
