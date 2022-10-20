// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>
#include <memory>

#include "absl/status/status.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "google/protobuf/message_lite.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "secagentd/bpf/process.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"

namespace {

namespace pb = cros_xdr::reporting;

// Fills a Namespaces proto with contents from bpf namespace_info.
void FillNamespaces(const secagentd::bpf::cros_namespace_info& ns,
                    pb::Namespaces* ns_proto) {
  ns_proto->set_cgroup_ns(ns.cgroup_ns);
  ns_proto->set_pid_ns(ns.pid_ns);
  ns_proto->set_user_ns(ns.user_ns);
  ns_proto->set_uts_ns(ns.uts_ns);
  ns_proto->set_mnt_ns(ns.mnt_ns);
  ns_proto->set_net_ns(ns.net_ns);
  ns_proto->set_ipc_ns(ns.ipc_ns);
}

}  // namespace

namespace secagentd {

static void print_process_change_ns(
    const struct bpf::cros_process_change_namespace& p) {
  LOG(INFO) << "\n[namespace_change]pid:" << p.pid
            << " start_time:" << p.start_time
            << "\n [ns]pid:" << p.new_ns.pid_ns << " mnt:" << p.new_ns.mnt_ns
            << " cgrp:" << p.new_ns.cgroup_ns << " usr:" << p.new_ns.user_ns
            << " uts:" << p.new_ns.uts_ns << " net:" << p.new_ns.net_ns
            << " ipc:" << p.new_ns.ipc_ns;
}

ProcessPlugin::ProcessPlugin(
    scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
    scoped_refptr<MessageSenderInterface> message_sender,
    scoped_refptr<ProcessCacheInterface> process_cache)
    : weak_ptr_factory_(this),
      message_sender_(message_sender),
      process_cache_(process_cache) {
  CHECK(message_sender != nullptr);
  CHECK(process_cache != nullptr);
  CHECK(bpf_skeleton_factory);
  factory_ = std::move(bpf_skeleton_factory);
}

std::string ProcessPlugin::GetName() const {
  return "ProcessBPF";
}

void ProcessPlugin::HandleRingBufferEvent(const bpf::cros_event& bpf_event) {
  std::unique_ptr<google::protobuf::MessageLite> message;
  pb::CommonEventDataFields* mutable_common = nullptr;
  reporting::Destination destination =
      reporting::Destination::CROS_SECURITY_PROCESS;
  if (bpf_event.type == bpf::process_type) {
    const bpf::cros_process_event& pe = bpf_event.data.process_event;
    if (pe.type == bpf::process_start_type) {
      const bpf::cros_process_start& process_start = pe.data.process_start;
      // Record the newly spawned process into our cache.
      process_cache_->PutFromBpfExec(process_start);
      auto exec_event = MakeExecEvent(process_start);
      // Save a pointer to the common field before downcasting.
      mutable_common = exec_event->mutable_common();
      message = std::move(exec_event);
    } else if (pe.type == bpf::process_change_namespace_type) {
      const bpf::cros_process_change_namespace& p =
          pe.data.process_change_namespace;
      print_process_change_ns(p);
    } else {
      LOG(ERROR) << "ProcessBPF: unknown BPF process event type.";
      return;
    }
  } else {
    LOG(ERROR) << "ProcessBPF: unknown BPF event type.";
    return;
  }
  if (message) {
    absl::Status status = message_sender_->SendMessage(
        destination, mutable_common, std::move(message));
    if (status != absl::OkStatus()) {
      LOG(ERROR) << "SendMessage FAILED: Message not sent. status=" << status;
    }
  }
}

void ProcessPlugin::HandleBpfRingBufferReadReady() const {
  skeleton_wrapper_->ConsumeEvent();
}

bool ProcessPlugin::PolicyIsEnabled() const {
  // TODO(b/241578773): Query the device policy to determine whether this BPF
  // should be loaded.
  return true;
}

absl::Status ProcessPlugin::Activate() {
  if (!PolicyIsEnabled()) {
    return absl::InternalError(
        "Failed to load Process BPF: Device policy forbids it.");
  }
  struct BpfCallbacks callbacks;
  callbacks.ring_buffer_event_callback = base::BindRepeating(
      &ProcessPlugin::HandleRingBufferEvent, weak_ptr_factory_.GetWeakPtr());
  callbacks.ring_buffer_read_ready_callback =
      base::BindRepeating(&ProcessPlugin::HandleBpfRingBufferReadReady,
                          weak_ptr_factory_.GetWeakPtr());
  skeleton_wrapper_ =
      factory_->Create(Types::BpfSkeleton::kProcess, std::move(callbacks));
  if (skeleton_wrapper_ == nullptr) {
    return absl::InternalError("Process BPF program loading error.");
  }
  return absl::OkStatus();
}

std::unique_ptr<pb::XdrProcessEvent> ProcessPlugin::MakeExecEvent(
    const secagentd::bpf::cros_process_start& process_start) {
  auto process_event = std::make_unique<pb::XdrProcessEvent>();
  auto process_exec_event = process_event->mutable_process_exec();
  FillNamespaces(process_start.spawn_namespace,
                 process_exec_event->mutable_spawn_namespaces());
  // Fetch information on process that was just spawned, the parent process
  // that spawned that process, and its parent process. I.e a total of three.
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.pid, process_start.start_time, 3);
  if (hierarchy.size() > 0) {
    process_exec_event->set_allocated_spawn_process(hierarchy[0].release());
  }
  if (hierarchy.size() > 1) {
    process_exec_event->set_allocated_process(hierarchy[1].release());
  }
  if (hierarchy.size() > 2) {
    process_exec_event->set_allocated_parent_process(hierarchy[2].release());
  }
  return process_event;
}

}  // namespace secagentd
