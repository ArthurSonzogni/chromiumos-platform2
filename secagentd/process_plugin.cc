// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/status/status.h>
#include <base/logging.h>

#include "secagentd/plugins.h"
#include "secagentd/skeleton_factory.h"

namespace secagentd {

static void print_process_start(const bpf::process_start& process_start) {
  LOG(INFO) << "\nppid: " << process_start.ppid << " pid:" << process_start.pid
            << " fname:" << process_start.filename
            << " cmdline:" << process_start.command_line
            << "\n [ns]pid:" << process_start.spawn_namespace.pid_ns
            << " mnt:" << process_start.spawn_namespace.mnt_ns
            << " cgrp:" << process_start.spawn_namespace.cgroup_ns
            << " usr:" << process_start.spawn_namespace.user_ns
            << " uts:" << process_start.spawn_namespace.uts_ns
            << " net:" << process_start.spawn_namespace.net_ns
            << " ipc:" << process_start.spawn_namespace.ipc_ns;
}

ProcessPlugin::ProcessPlugin(
    std::unique_ptr<BpfSkeletonFactoryInterface> factory)
    : weak_ptr_factory_(this) {
  factory_ = std::move(factory);
}

std::string ProcessPlugin::GetPluginName() const {
  return "ProcessBPF";
}

void ProcessPlugin::HandleRingBufferEvent(const bpf::event& bpf_event) const {
  if (bpf_event.type == bpf::process_type) {
    const bpf::process_event& pe = bpf_event.data.process_event;
    if (pe.type == bpf::process_start_type) {
      const bpf::process_start& process_start = pe.data.process_start;
      // TODO(b/241578709): This plugin should immediately send events over to
      // the reporting pipe. The reporting pipe can enforce the event specific
      // caching policy.
      print_process_start(process_start);
    } else {
      LOG(ERROR) << "ProcessBPF: unknown BPF process event type.";
    }
  } else {
    LOG(ERROR) << "ProcessBPF: unknown BPF event type.";
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
absl::Status ProcessPlugin::LoadAndRun() {
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
  skeleton_wrapper_ = factory_->Create(std::move(callbacks));
  if (skeleton_wrapper_ == nullptr) {
    return absl::InternalError("Process BPF program loading error.");
  }
  return absl::OkStatus();
}

}  // namespace secagentd
