// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "missive/client/report_queue.h"
#include "missive/client/report_queue_configuration.h"
#include "missive/client/report_queue_factory.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "missive/util/status.h"
#include "secagentd/bpf/process.h"
#include "secagentd/message_sender.h"

namespace {

// Fills a FileImage proto with contents from bpf image_info.
void FillImage(const secagentd::bpf::image_info& image,
               reporting::FileImage* proto) {
  proto->set_pathname(std::string(image.pathname));
  proto->set_mnt_ns(image.mnt_ns);
  proto->set_inode_device_id(image.inode_device_id);
  proto->set_inode(image.inode);
  proto->set_canonical_uid(image.uid);
  proto->set_canonical_gid(image.gid);
  proto->set_mode(image.mode);
}

// Fills a Namespaces proto with contents from bpf namespace_info.
void FillNamespaces(const secagentd::bpf::namespace_info& namespaces,
                    reporting::Namespaces* proto) {
  proto->set_cgroup_ns(namespaces.cgroup_ns);
  proto->set_pid_ns(namespaces.pid_ns);
  proto->set_user_ns(namespaces.user_ns);
  proto->set_uts_ns(namespaces.uts_ns);
  proto->set_mnt_ns(namespaces.mnt_ns);
  proto->set_net_ns(namespaces.net_ns);
  proto->set_ipc_ns(namespaces.ipc_ns);
}

// Creates a ProcessExec proto from the contents of a bpf event.
std::unique_ptr<reporting::ProcessExecEvent> FillProcessExecEvent(
    const secagentd::bpf::process_start& event) {
  auto process_start = std::make_unique<reporting::ProcessExecEvent>();

  // Fill process fields.
  auto process = process_start->mutable_process();
  process->set_canonical_pid(event.pid);
  process->set_canonical_uid(event.uid);
  process->set_commandline(event.commandline);

  // Fill parent_process fields.
  auto parent_process = process_start->mutable_parent_process();
  parent_process->set_canonical_pid(event.ppid);

  // Fill image fields.
  FillImage(event.image_info, process->mutable_image());

  // Fill namespace fields.
  FillNamespaces(event.spawn_namespace,
                 process_start->mutable_spawn_namespaces());

  return process_start;
}

// Creates a ProcessExit proto from the contents of a bpf event.
std::unique_ptr<reporting::ProcessTerminateEvent> FillProcessExitEvent(
    const secagentd::bpf::process_exit& event) {
  auto process_exit = std::make_unique<reporting::ProcessTerminateEvent>();

  // Fill process fields.
  auto process = process_exit->mutable_process();
  process->set_canonical_pid(event.ppid);

  return process_exit;
}

void EnqueueCallback(secagentd::bpf::process_event_type type,
                     ::reporting::Status status) {
  if (!status.ok()) {
    LOG(ERROR) << type << ", status=" << status;
  }
}

}  // namespace

namespace secagentd {

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

absl::Status MessageSender::SendMessage(const bpf::event& event) {
  switch (event.type) {
    case bpf::process_type: {
      auto it = queue_map_.find(reporting::CROS_SECURITY_PROCESS);
      CHECK(it != queue_map_.end());

      std::unique_ptr<const google::protobuf::MessageLite> proto;

      switch (event.data.process_event.type) {
        case bpf::process_start_type: {
          proto =
              FillProcessExecEvent(event.data.process_event.data.process_start);
          break;
        }
        case bpf::process_exit_type: {
          proto =
              FillProcessExitEvent(event.data.process_event.data.process_exit);
          break;
        }
        default: {
          return absl::InvalidArgumentError(base::StringPrintf(
              "SendMessage: unknown BPF process event type: %d",
              event.data.process_event.type));
        }
      }

      it->second.get()->Enqueue(
          std::move(proto), ::reporting::SECURITY,
          base::BindOnce(&EnqueueCallback, event.data.process_event.type));
      break;
    }
    default: {
      return absl::InvalidArgumentError(base::StringPrintf(
          "SendMessage: unknown BPF event type: %d", event.type));
    }
  }

  return absl::OkStatus();
}

}  // namespace secagentd
