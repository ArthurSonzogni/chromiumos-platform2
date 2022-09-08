// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_BPF_SKELETON_WRAPPERS_H_
#define SECAGENTD_BPF_SKELETON_WRAPPERS_H_

#include <absl/status/status.h>
#include <base/callback.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/memory/weak_ptr.h>
#include <memory>

#include "secagentd/bpf/process.h"
#include "secagentd/bpf_skeletons/skeleton_process_bpf.h"

namespace secagentd {
// The following callback definitions must have void return type since they will
// bind to an object method.
using BpfEventCb = base::RepeatingCallback<void(const bpf::event&)>;
using BpfEventAvailableCb = base::RepeatingCallback<void()>;

// The callbacks a BPF plugins are required to provide.
struct BpfCallbacks {
  // The callback responsible for handling a ring buffer security event.
  BpfEventCb ring_buffer_event_callback;
  // The callback that handles when any ring buffer has data ready for
  // consumption (reading).
  BpfEventAvailableCb ring_buffer_read_ready_callback;
};

class BpfSkeletonInterface {
 public:
  BpfSkeletonInterface() = default;
  explicit BpfSkeletonInterface(const BpfSkeletonInterface&) = delete;
  BpfSkeletonInterface& operator=(const BpfSkeletonInterface&) = delete;
  virtual ~BpfSkeletonInterface() = default;
  virtual absl::Status LoadAndAttach() = 0;

  // Register callbacks to handle:
  // 1 - When a security event from a ring buffer has been consumed and is
  // available for further processing.
  // 2 - When a ring buffer has data available for reading.
  void RegisterCallbacks(BpfCallbacks cbs);

  // Consume one or more events from a BPF ring buffer, ignoring whether a ring
  // buffer has notified that data is available for read.
  virtual int ConsumeEvent() = 0;
};

class ProcessBpfSkeleton : public BpfSkeletonInterface {
 public:
  ~ProcessBpfSkeleton() override;
  absl::Status LoadAndAttach() override;
  void RegisterCallbacks(BpfCallbacks cbs);
  int ConsumeEvent() override;

 private:
  BpfCallbacks callbacks_;
  process_bpf* skel_{nullptr};
  struct ring_buffer* rb_{nullptr};
  std::unique_ptr<base::FileDescriptorWatcher::Controller> rb_watch_readable_;
};
}  //  namespace secagentd
#endif  // SECAGENTD_BPF_SKELETON_WRAPPERS_H_
