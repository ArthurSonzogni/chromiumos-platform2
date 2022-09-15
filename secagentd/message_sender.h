// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_MESSAGE_SENDER_H_
#define SECAGENTD_MESSAGE_SENDER_H_

#include <memory>
#include <unordered_map>

#include "absl/status/status.h"
#include "base/task/sequenced_task_runner.h"
#include "missive/client/report_queue.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "secagentd/bpf/process.h"

namespace secagentd {

class MessageSender : public base::RefCountedThreadSafe<MessageSender> {
 public:
  // Initializes a queue for each destination and stores result into queue_map.
  absl::Status InitializeQueues();

  // Creates and enqueues a proto message with given bpf event.
  absl::Status SendMessage(const bpf::event& event);

 private:
  // Map linking each destination to its corresponding Report_Queue.
  std::unordered_map<
      reporting::Destination,
      std::unique_ptr<reporting::ReportQueue, base::OnTaskRunnerDeleter>>
      queue_map_;
};

}  // namespace secagentd

#endif  // SECAGENTD_MESSAGE_SENDER_H_
