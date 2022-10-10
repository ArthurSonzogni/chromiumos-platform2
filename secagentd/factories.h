// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_FACTORIES_H_
#define SECAGENTD_FACTORIES_H_

#include <memory>
#include <utility>

#include "base/memory/scoped_refptr.h"
#include "secagentd/plugins.h"

namespace secagentd {

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

class PluginFactoryInterface {
 public:
  enum class PluginType { kAgent, kProcess };

  virtual std::unique_ptr<PluginInterface> Create(
      PluginType type,
      scoped_refptr<MessageSenderInterface> message_sender) = 0;
  virtual ~PluginFactoryInterface() = default;
};

std::ostream& operator<<(std::ostream& out,
                         const PluginFactoryInterface::PluginType& type);

class PluginFactory : public PluginFactoryInterface {
 public:
  PluginFactory();
  explicit PluginFactory(
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory)
      : bpf_skeleton_factory_(bpf_skeleton_factory) {}
  std::unique_ptr<PluginInterface> Create(
      PluginType type,
      scoped_refptr<MessageSenderInterface> message_sender) override;

 private:
  scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory_;
};

namespace Types {
using Plugin = PluginFactoryInterface::PluginType;
using BpfSkeleton = BpfSkeletonFactoryInterface::BpfSkeletonType;
}  // namespace Types

}  // namespace secagentd

#endif  // SECAGENTD_FACTORIES_H_
