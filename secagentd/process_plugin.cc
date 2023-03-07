// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "google/protobuf/message_lite.h"
#include "missive/proto/record_constants.pb.h"
#include "secagentd/bpf/process.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/policies_features_broker.h"
#include "secagentd/proto/security_xdr_events.pb.h"

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
  return "ProcessPlugin";
}

void ProcessPlugin::HandleRingBufferEvent(const bpf::cros_event& bpf_event) {
  auto atomic_event = std::make_unique<pb::ProcessEventAtomicVariant>();
  if (bpf_event.type != bpf::process_type) {
    LOG(ERROR) << "ProcessBPF: unknown BPF event type.";
    return;
  }
  const bpf::cros_process_event& pe = bpf_event.data.process_event;
  if (pe.type == bpf::process_start_type) {
    const bpf::cros_process_start& process_start = pe.data.process_start;
    // Record the newly spawned process into our cache.
    process_cache_->PutFromBpfExec(process_start);
    auto exec_event = MakeExecEvent(process_start);
    const pb::Process* parent_process =
        exec_event->has_process() ? exec_event->mutable_process() : nullptr;
    const pb::Process* process = exec_event->has_spawn_process()
                                     ? exec_event->mutable_spawn_process()
                                     : nullptr;
    if (process_cache_->IsEventFiltered(parent_process, process)) {
      return;
    }
    atomic_event->set_allocated_process_exec(exec_event.release());
  } else if (pe.type == bpf::process_exit_type) {
    const bpf::cros_process_exit& process_exit = pe.data.process_exit;
    auto terminate_event = MakeTerminateEvent(process_exit);
    if (process_exit.is_leaf) {
      process_cache_->EraseProcess(process_exit.task_info.pid,
                                   process_exit.task_info.start_time);
    }
    const pb::Process* parent_process =
        terminate_event->has_parent_process()
            ? terminate_event->mutable_parent_process()
            : nullptr;
    const pb::Process* process = terminate_event->has_process()
                                     ? terminate_event->mutable_process()
                                     : nullptr;
    if (process_cache_->IsEventFiltered(parent_process, process)) {
      return;
    }
    atomic_event->set_allocated_process_terminate(terminate_event.release());
  } else {
    LOG(ERROR) << "ProcessBPF: unknown BPF process event type.";
    return;
  }
  DeprecatedSendImmediate(std::move(atomic_event));
}

void ProcessPlugin::HandleBpfRingBufferReadReady() const {
  skeleton_wrapper_->ConsumeEvent();
}

absl::Status ProcessPlugin::Activate() {
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

void ProcessPlugin::DeprecatedSendImmediate(
    std::unique_ptr<pb::ProcessEventAtomicVariant> atomic_event) {
  auto xdr_process_event = std::make_unique<pb::XdrProcessEvent>();
  if (atomic_event->has_process_exec()) {
    xdr_process_event->set_allocated_process_exec(
        atomic_event->release_process_exec());
  } else if (atomic_event->has_process_terminate()) {
    xdr_process_event->set_allocated_process_terminate(
        atomic_event->release_process_terminate());
  } else {
    LOG(ERROR) << "Attempted to enqueue an empty event.";
    return;
  }
  message_sender_->SendMessage(reporting::Destination::CROS_SECURITY_PROCESS,
                               xdr_process_event->mutable_common(),
                               std::move(xdr_process_event), std::nullopt);
}

std::unique_ptr<pb::ProcessExecEvent> ProcessPlugin::MakeExecEvent(
    const secagentd::bpf::cros_process_start& process_start) {
  auto process_exec_event = std::make_unique<pb::ProcessExecEvent>();
  FillNamespaces(process_start.spawn_namespace,
                 process_exec_event->mutable_spawn_namespaces());
  // Fetch information on process that was just spawned, the parent process
  // that spawned that process, and its parent process. I.e a total of three.
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  if (hierarchy.empty()) {
    LOG(ERROR) << "PID:" << process_start.task_info.pid
               << " not found in the process cache.";
  }
  if (hierarchy.size() > 0) {
    process_exec_event->set_allocated_spawn_process(hierarchy[0].release());
  }
  if (hierarchy.size() > 1) {
    process_exec_event->set_allocated_process(hierarchy[1].release());
  }
  if (hierarchy.size() > 2) {
    process_exec_event->set_allocated_parent_process(hierarchy[2].release());
  }

  return process_exec_event;
}

std::unique_ptr<pb::ProcessTerminateEvent> ProcessPlugin::MakeTerminateEvent(
    const secagentd::bpf::cros_process_exit& process_exit) {
  auto process_terminate_event = std::make_unique<pb::ProcessTerminateEvent>();
  // Try to fetch from the process cache if possible. The cache has more
  // complete information.
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_exit.task_info.pid, process_exit.task_info.start_time, 2);
  if (hierarchy.size() > 0) {
    process_terminate_event->set_allocated_process(hierarchy[0].release());
  }
  if (hierarchy.size() > 1) {
    process_terminate_event->set_allocated_parent_process(
        hierarchy[1].release());
  }

  // If that fails, fill in the task info that we got from BPF.
  if (hierarchy.size() == 0) {
    ProcessCache::PartiallyFillProcessFromBpfTaskInfo(
        process_exit.task_info, process_terminate_event->mutable_process());
    // Maybe the parent is still alive and in procfs.
    auto parent = process_cache_->GetProcessHierarchy(
        process_exit.task_info.ppid, process_exit.task_info.parent_start_time,
        1);
    if (parent.size() != 0) {
      process_terminate_event->set_allocated_parent_process(
          parent[0].release());
    }
  }
  return process_terminate_event;
}

}  // namespace secagentd
