// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_BPF_SKELETON_H_
#define SECAGENTD_TEST_MOCK_BPF_SKELETON_H_

#include <memory>
#include <utility>

#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/factories.h"
#include "testing/gmock/include/gmock/gmock.h"
namespace secagentd {

class MockBpfSkeleton : public BpfSkeletonInterface {
 public:
  MOCK_METHOD(absl::Status, LoadAndAttach, (), (override));
  MOCK_METHOD(void, RegisterCallbacks, (BpfCallbacks cbs), (override));
  MOCK_METHOD(int, ConsumeEvent, (), (override));
};

class MockSkeletonFactory : public BpfSkeletonFactoryInterface {
 public:
  explicit MockSkeletonFactory(std::unique_ptr<BpfSkeletonInterface> bpf_skel)
      : bpf_skel_(std::move(bpf_skel)) {}

  MOCK_METHOD(void,
              MockCreate,
              (BpfSkeletonType type, const BpfCallbacks& cbs));

  std::unique_ptr<BpfSkeletonInterface> Create(BpfSkeletonType type,
                                               BpfCallbacks cbs) override {
    MockCreate(type, cbs);
    // Make a copy of the callbacks so unit tests can invoke them.
    cbs_ = cbs;
    return std::move(bpf_skel_);
  }

  BpfCallbacks cbs_;
  std::unique_ptr<BpfSkeletonInterface> bpf_skel_;
};
}  // namespace secagentd

#endif  // SECAGENTD_TEST_MOCK_BPF_SKELETON_H_
