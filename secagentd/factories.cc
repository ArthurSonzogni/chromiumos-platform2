// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/factories.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"

namespace secagentd {

std::ostream& operator<<(std::ostream& out, const Types::BpfSkeleton& type) {
  static const absl::flat_hash_map<Types::BpfSkeleton, std::string>
      kTypeToString{{Types::BpfSkeleton::kProcess, "Process"}};
  auto i = kTypeToString.find(type);
  out << (i != kTypeToString.end() ? i->second : "Unknown");
  return out;
}

std::unique_ptr<BpfSkeletonInterface> BpfSkeletonFactory::Create(
    BpfSkeletonType type, BpfCallbacks cbs) {
  if (created_skeletons_.contains(type)) {
    return nullptr;
  }

  std::unique_ptr<BpfSkeletonInterface> rv{nullptr};
  switch (type) {
    case BpfSkeletonType::kProcess:
      rv = di_.process ? std::move(di_.process)
                       : std::make_unique<ProcessBpfSkeleton>();
      break;
    default:
      LOG(ERROR) << "Failed to create skeleton: unhandled type " << type;
      return nullptr;
  }

  rv->RegisterCallbacks(std::move(cbs));
  absl::Status status = rv->LoadAndAttach();
  if (!status.ok()) {
    LOG(ERROR) << "Failed to create skeleton of type " << type << ":"
               << status.message();
    return nullptr;
  }
  created_skeletons_.insert(type);
  return rv;
}

std::ostream& operator<<(std::ostream& out, const Types::Plugin& type) {
  static const absl::flat_hash_map<Types::Plugin, std::string> kTypeToString{
      {Types::Plugin::kProcess, "Process"}, {Types::Plugin::kAgent, "Agent"}};

  auto i = kTypeToString.find(type);
  out << (i != kTypeToString.end() ? i->second : "Unknown");
  return out;
}

PluginFactory::PluginFactory() {
  bpf_skeleton_factory_ = ::base::MakeRefCounted<BpfSkeletonFactory>();
}

std::unique_ptr<PluginInterface> PluginFactory::Create(
    PluginType type, scoped_refptr<MessageSenderInterface> message_sender) {
  std::unique_ptr<PluginInterface> rv{nullptr};
  switch (type) {
    case PluginType::kProcess:
      rv = std::make_unique<ProcessPlugin>(bpf_skeleton_factory_,
                                           message_sender);
      break;

    case PluginType::kAgent:
      break;
  }
  return rv;
}

}  // namespace secagentd
