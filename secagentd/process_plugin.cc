// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include "absl/status/status.h"
#include "base/logging.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/stringprintf.h"
#include "secagentd/bpf/process.h"
#include "secagentd/plugins.h"
#include "secagentd/skeleton_factory.h"

namespace {

// Kernel arg and env lists use '\0' to delimit elements. Tokenize the string
// and use single quotes (') to designate atomic elements.
// bufsize is the total capacity of buf (used for bounds checking). payload_len
// is the length of actual payload including the final '\0'.
std::string SafeTransformArgvEnvp(const char* buf,
                                  size_t bufsize,
                                  size_t payload_len) {
  std::string str;
  if (payload_len <= 0 || payload_len > bufsize) {
    return str;
  }
  base::CStringTokenizer t(buf, buf + payload_len, std::string("\0", 1));
  while (t.GetNext()) {
    str.append(base::StringPrintf("'%s' ", t.token().c_str()));
  }
  if (str.length() > 0) {
    str.pop_back();
  }
  return str;
}

}  // namespace

namespace secagentd {

static void print_process_start(const bpf::process_start& process_start) {
  LOG(INFO) << "\n[process_start] ppid: " << process_start.ppid
            << " pid:" << process_start.pid << " uid:" << process_start.uid
            << " gid:" << process_start.gid << " cmdline:"
            << SafeTransformArgvEnvp(process_start.commandline,
                                     sizeof(process_start.commandline),
                                     process_start.commandline_len)
            << "\n[ns] pid:" << process_start.spawn_namespace.pid_ns
            << " mnt:" << process_start.spawn_namespace.mnt_ns
            << " cgroup:" << process_start.spawn_namespace.cgroup_ns
            << " usr:" << process_start.spawn_namespace.user_ns
            << " uts:" << process_start.spawn_namespace.uts_ns
            << " net:" << process_start.spawn_namespace.net_ns
            << " ipc:" << process_start.spawn_namespace.ipc_ns
            << "\n[image] pathname:" << process_start.image_info.pathname
            << " mnt_ns:" << process_start.image_info.mnt_ns
            << " inode_device_id:" << process_start.image_info.inode_device_id
            << " inode:" << process_start.image_info.inode
            << " uid:" << process_start.image_info.uid
            << " gid:" << process_start.image_info.gid << " mode:" << std::oct
            << process_start.image_info.mode;
}

static void print_process_change_ns(
    const struct bpf::process_change_namespace& p) {
  LOG(INFO) << "\n[namespace_change]pid:" << p.pid
            << " start_time:" << p.start_time
            << "\n [ns]pid:" << p.new_ns.pid_ns << " mnt:" << p.new_ns.mnt_ns
            << " cgrp:" << p.new_ns.cgroup_ns << " usr:" << p.new_ns.user_ns
            << " uts:" << p.new_ns.uts_ns << " net:" << p.new_ns.net_ns
            << " ipc:" << p.new_ns.ipc_ns;
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
    } else if (pe.type == bpf::process_change_namespace_type) {
      const bpf::process_change_namespace& p = pe.data.process_change_namespace;
      print_process_change_ns(p);
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
