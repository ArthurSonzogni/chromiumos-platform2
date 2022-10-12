// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_BPF_SKELETON_WRAPPERS_H_
#define SECAGENTD_BPF_SKELETON_WRAPPERS_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "base/callback.h"
#include "base/files/file_descriptor_watcher_posix.h"
#include "secagentd/bpf/process.h"
#include "secagentd/bpf_skeletons/skeleton_process_bpf.h"

namespace secagentd {

// Directory with min_core_btf payloads. Must match the ebuild.
constexpr char kMinCoreBtfDir[] = "/usr/share/btf/secagentd/";

// The following callback definitions must have void return type since they will
// bind to an object method.
using BpfEventCb = base::RepeatingCallback<void(const bpf::cros_event&)>;
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
  explicit BpfSkeletonInterface(const BpfSkeletonInterface&) = delete;
  BpfSkeletonInterface& operator=(const BpfSkeletonInterface&) = delete;
  virtual ~BpfSkeletonInterface() = default;
  // Consume one or more events from a BPF ring buffer, ignoring whether a ring
  // buffer has notified that data is available for read.
  virtual int ConsumeEvent() = 0;

 protected:
  friend class BpfSkeletonFactory;
  BpfSkeletonInterface() = default;

  virtual absl::Status LoadAndAttach() = 0;

  // Register callbacks to handle:
  // 1 - When a security event from a ring buffer has been consumed and is
  // available for further processing.
  // 2 - When a ring buffer has data available for reading.
  virtual void RegisterCallbacks(BpfCallbacks cbs) = 0;
};

class ProcessBpfSkeleton : public BpfSkeletonInterface {
 public:
  ~ProcessBpfSkeleton() override;
  int ConsumeEvent() override;

 protected:
  friend class BpfSkeletonFactory;

  absl::Status LoadAndAttach() override;
  void RegisterCallbacks(BpfCallbacks cbs) override;

 private:
  BpfCallbacks callbacks_;
  process_bpf* skel_{nullptr};
  struct ring_buffer* rb_{nullptr};
  std::unique_ptr<base::FileDescriptorWatcher::Controller> rb_watch_readable_;
};

class BpfSkeletonFactoryInterface
    : public ::base::RefCounted<BpfSkeletonFactoryInterface> {
 public:
  enum class BpfSkeletonType { kProcess };
  struct SkeletonInjections {
    std::unique_ptr<BpfSkeletonInterface> process;
  };

  // Creates a BPF Handler class that loads and attaches a BPF application.
  // The passed in callback will be invoked when an event is available from the
  // BPF application.
  virtual std::unique_ptr<BpfSkeletonInterface> Create(BpfSkeletonType type,
                                                       BpfCallbacks cbs) = 0;
  virtual ~BpfSkeletonFactoryInterface() = default;
};

namespace Types {
using BpfSkeleton = BpfSkeletonFactoryInterface::BpfSkeletonType;
}  // namespace Types

// Support absl format for BpfSkeletonType.
absl::FormatConvertResult<absl::FormatConversionCharSet::kString>
AbslFormatConvert(const BpfSkeletonFactoryInterface::BpfSkeletonType& type,
                  const absl::FormatConversionSpec& spec,
                  absl::FormatSink* sink);

std::ostream& operator<<(
    std::ostream& out,
    const BpfSkeletonFactoryInterface::BpfSkeletonType& type);

class BpfSkeletonFactory : public BpfSkeletonFactoryInterface {
 public:
  BpfSkeletonFactory() = default;
  explicit BpfSkeletonFactory(SkeletonInjections di) : di_(std::move(di)) {}

  std::unique_ptr<BpfSkeletonInterface> Create(BpfSkeletonType type,
                                               BpfCallbacks cbs) override;

 private:
  SkeletonInjections di_;
  absl::flat_hash_set<BpfSkeletonType> created_skeletons_;
};

}  //  namespace secagentd
#endif  // SECAGENTD_BPF_SKELETON_WRAPPERS_H_
