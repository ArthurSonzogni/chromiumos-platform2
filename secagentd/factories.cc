// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <memory>
#include <utility>

#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/factories.h"
#include "secagentd/message_sender.h"
#include "secagentd/plugins.h"
#include "secagentd/skeleton_factory.h"

namespace secagentd {

std::unique_ptr<PluginInterface> BpfPluginFactory::CreateProcessPlugin(
    scoped_refptr<MessageSender> message_sender) {
  auto factory = std::make_unique<ProcessBpfSkeletonFactory>();
  return std::make_unique<ProcessPlugin>(std::move(factory), message_sender);
}

std::unique_ptr<BpfSkeletonInterface> ProcessBpfSkeletonFactory::Create(
    BpfCallbacks cbs) {
  static uint32_t instance_count{0};
  if (instance_count > 0) {
    return nullptr;
  }

  auto rv = std::make_unique<ProcessBpfSkeleton>();

  rv->RegisterCallbacks(std::move(cbs));
  absl::Status status = rv->LoadAndAttach();
  if (status.ok()) {
    instance_count += 1;
    return rv;
  }
  LOG(ERROR) << status.message();
  return nullptr;
}

}  // namespace secagentd
