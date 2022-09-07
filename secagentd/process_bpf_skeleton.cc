// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <absl/status/status.h>
#include <base/logging.h>
#include <bpf/libbpf.h>
#include <string.h>
#include <utility>

#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/bpf_skeletons/skeleton_process_bpf.h"
#include "secagentd/bpf_utils.h"

namespace secagentd {

ProcessBpfSkeleton::~ProcessBpfSkeleton() {
  // File Controller documentation states that the file descriptor being watched
  // must outlive the controller. In this case rb_watch_readable_ is watching
  // the ring buffer file descriptor so force its destruction before we close
  // the file descriptor.
  rb_watch_readable_ = nullptr;
  if (rb_ != nullptr) {
    // This frees and also closes all ring buffer file descriptors.
    ring_buffer__free(rb_);
  }
  if (skel_ != nullptr) {
    process_bpf__destroy(skel_);
  }
}

void ProcessBpfSkeleton::RegisterCallbacks(BpfCallbacks cbs) {
  callbacks_ = cbs;
}

int ProcessBpfSkeleton::ConsumeEvent() {
  if (rb_ == nullptr) {
    return -1;
  }
  return ring_buffer__consume(rb_);
}

absl::Status ProcessBpfSkeleton::LoadAndAttach() {
  if (callbacks_.ring_buffer_event_callback.is_null() ||
      callbacks_.ring_buffer_read_ready_callback.is_null()) {
    return absl::InternalError(
        "ProcessBPF: LoadAndAttach failed, one or more provided callbacks "
        "are null.");
  }
  libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
  skel_ = process_bpf__open();

  if (!skel_) {
    return absl::InternalError("BPF skeleton failed to open.");
  }
  if (process_bpf__load(skel_)) {
    return absl::InternalError(
        "ProcessBPF: application failed loading and verification.");
  }

  if (process_bpf__attach(skel_)) {
    return absl::InternalError("ProcessBPF: program failed to attach.");
  }

  int map_fd = bpf_map__fd(skel_->maps.rb);
  int epoll_fd{-1};

  // ring_buffer__new will fail with an invalid fd but we explicitly check
  // anyways for code clarity.
  if (map_fd >= 0) {
    rb_ = ring_buffer__new(
        map_fd, indirect_c_callback,
        static_cast<void*>(&callbacks_.ring_buffer_event_callback), nullptr);
    epoll_fd = ring_buffer__epoll_fd(rb_);
  }

  if (map_fd < 0 || !rb_ || epoll_fd < 0) {
    return absl::InternalError("ProcessBPF: Ring buffer creation failed.");
  }

  rb_watch_readable_ = base::FileDescriptorWatcher::WatchReadable(
      epoll_fd, callbacks_.ring_buffer_read_ready_callback);
  return absl::OkStatus();
}
}  // namespace secagentd
