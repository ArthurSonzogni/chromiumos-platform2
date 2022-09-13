// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_SKELETON_FACTORY_H_
#define SECAGENTD_SKELETON_FACTORY_H_

#include <memory>

#include "secagentd/bpf_skeleton_wrappers.h"

namespace secagentd {
class BpfSkeletonFactoryInterface {
 public:
  // Creates a BPF Handler class that loads and attaches a BPF application.
  // The passed in callback will be invoked when an event is available from the
  // BPF application.
  virtual std::unique_ptr<BpfSkeletonInterface> Create(BpfCallbacks cbs) = 0;
  virtual ~BpfSkeletonFactoryInterface() = default;
};

class ProcessBpfSkeletonFactory : public BpfSkeletonFactoryInterface {
 public:
  std::unique_ptr<BpfSkeletonInterface> Create(BpfCallbacks cbs) override;
};
}  // namespace secagentd

#endif  // SECAGENTD_SKELETON_FACTORY_H_
